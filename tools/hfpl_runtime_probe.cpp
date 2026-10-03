#include <QDir>
#include <QDirIterator>
#include <QColor>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLDebugLogger>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QThread>

#include <cstring>
#include <array>
#include <QElapsedTimer>
#include <cmath>

#include "plugin/NativePlugin.h"
#include "plugin/NativeEffectRender.h"
#include "composition/Composition.h"

namespace {

QString g_bufferGlState;
QStringList g_scratchTrace;
bool g_forceDiagnosticMatrices = false;

struct ProbeVbo
{
    GLuint id = 0;
    quint32 target = 0;
    qint32 byteCount = 0;
    quint32 usage = 0;
    bool reserved = false;
};

QVector<ProbeVbo> g_probeVbos;
QVector<GLuint> g_probeRenderbuffers;
QStringList g_renderbufferTrace;
qint32 g_probeFrameDimension = 1;

QString currentProgramValues(QOpenGLExtraFunctions* gl)
{
    GLint program = 0;
    GLint framebuffer = 0;
    GLint uniformCount = 0;
    gl->glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    // ChromaKey unbinds its program before requesting the next scratch target.
    // Its fifth pass is program 52 in the isolated probe context; inspect that
    // retained object so a deferred GPU stall can be tied to concrete uniforms.
    if (program == 0 && gl->glIsProgram(52)) {
        program = 52;
    }
    if (program == 0) {
        return QStringLiteral("program=0/fbo=%1").arg(framebuffer);
    }
    gl->glGetProgramiv(GLuint(program), GL_ACTIVE_UNIFORMS, &uniformCount);
    QStringList values;
    for (GLint i = 0; i < uniformCount; ++i) {
        std::array<GLchar, 128> name {};
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        gl->glGetActiveUniform(GLuint(program), GLuint(i), GLsizei(name.size()),
                               &length, &size, &type, name.data());
        const GLint location = gl->glGetUniformLocation(GLuint(program), name.data());
        if (location < 0) {
            continue;
        }
        QString value;
        if (type == GL_FLOAT || type == GL_FLOAT_VEC2 || type == GL_FLOAT_VEC3
            || type == GL_FLOAT_VEC4) {
            std::array<GLfloat, 4> fields {};
            gl->glGetUniformfv(GLuint(program), location, fields.data());
            const int fieldCount = type == GL_FLOAT ? 1 : type == GL_FLOAT_VEC2 ? 2
                                   : type == GL_FLOAT_VEC3 ? 3 : 4;
            QStringList formatted;
            for (int field = 0; field < fieldCount; ++field) {
                formatted.append(QString::number(fields[field], 'g', 7));
            }
            value = formatted.join(QLatin1Char(','));
        } else if (type == GL_INT || type == GL_BOOL || type == GL_SAMPLER_2D) {
            GLint field = 0;
            gl->glGetUniformiv(GLuint(program), location, &field);
            value = QString::number(field);
        } else {
            continue;
        }
        values.append(QString::fromLatin1(name.data(), length) + QLatin1Char('=') + value);
    }
    return QStringLiteral("program=%1/fbo=%2/%3")
        .arg(program).arg(framebuffer).arg(values.join(QLatin1Char('|')));
}

QString msvcStringAtRva(const void* moduleBase, quintptr rva)
{
    const auto* object = static_cast<const char*>(moduleBase) + rva;
    quint64 length = 0;
    quint64 capacity = 0;
    std::memcpy(&length, object + 0x10, sizeof(length));
    std::memcpy(&capacity, object + 0x18, sizeof(capacity));
    if (length > 512) {
        return QStringLiteral("<invalid>");
    }
    const char* data = object;
    if (capacity >= 16) {
        std::memcpy(&data, object, sizeof(data));
    }
    return QString::fromLatin1(data, qsizetype(length));
}

quint64 createVertexArray(quint64 host, quint64, quint64, quint64,
                          quint64, quint64, quint64, quint64)
{
    Q_UNUSED(host);
    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    GLuint vao = 0;
    gl->glGenVertexArrays(1, &vao);
    gl->glBindVertexArray(vao);
    return vao;
}

quint32 createBuffer(quint64 host, quint32 target, qint32 byteCount, const void* data,
                     quint32 usage, qint32* actualByteCount, quint32)
{
    Q_UNUSED(host);
    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    GLuint buffer = 0;
    for (ProbeVbo& candidate : g_probeVbos) {
        if (!candidate.reserved && candidate.target == target
            && candidate.byteCount == byteCount && candidate.usage == usage) {
            candidate.reserved = true;
            buffer = candidate.id;
            break;
        }
    }
    const bool created = buffer == 0;
    if (created) {
        gl->glGenBuffers(1, &buffer);
    }
    gl->glBindBuffer(GLenum(target), buffer);
    if (created) {
        gl->glBufferData(GLenum(target), GLsizeiptr(byteCount), data, GLenum(usage));
        g_probeVbos.append({buffer, target, byteCount, usage, true});
    } else if (data && byteCount > 0) {
        gl->glBufferSubData(GLenum(target), 0, GLsizeiptr(byteCount), data);
    }
    if (actualByteCount) {
        *actualByteCount = byteCount;
    }
    GLint program = 0;
    GLint linked = 0;
    GLint validated = 0;
    GLint uniformCount = 0;
    GLint attributeCount = 0;
    GLint fbo = 0;
    gl->glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    QStringList uniforms;
    if (program != 0) {
        gl->glGetProgramiv(GLuint(program), GL_LINK_STATUS, &linked);
        gl->glValidateProgram(GLuint(program));
        gl->glGetProgramiv(GLuint(program), GL_VALIDATE_STATUS, &validated);
        gl->glGetProgramiv(GLuint(program), GL_ACTIVE_UNIFORMS, &uniformCount);
        for (GLint i = 0; i < uniformCount; ++i) {
            std::array<GLchar, 128> name {};
            GLsizei length = 0;
            GLint size = 0;
            GLenum type = 0;
            gl->glGetActiveUniform(GLuint(program), GLuint(i), GLsizei(name.size()),
                                   &length, &size, &type, name.data());
            const QByteArray uniformName(name.data(), length);
            const GLint location = gl->glGetUniformLocation(GLuint(program), name.data());
            QString value;
            if (type == GL_FLOAT_MAT4) {
                std::array<GLfloat, 16> matrix {};
                gl->glGetUniformfv(GLuint(program), location, matrix.data());
                value = QStringLiteral("[%1,%2,%3,%4;%5,%6,%7,%8]")
                            .arg(matrix[0]).arg(matrix[5]).arg(matrix[10]).arg(matrix[15])
                            .arg(matrix[12]).arg(matrix[13]).arg(matrix[14]).arg(matrix[3]);
            } else if (type == GL_SAMPLER_2D) {
                GLint unit = -1;
                gl->glGetUniformiv(GLuint(program), location, &unit);
                value = QStringLiteral("=%1").arg(unit);
            }
            uniforms.append(QString::fromLatin1(uniformName) + QStringLiteral("@%1").arg(location)
                            + value);
        }
        gl->glGetProgramiv(GLuint(program), GL_ACTIVE_ATTRIBUTES, &attributeCount);
        for (GLint i = 0; i < attributeCount; ++i) {
            std::array<GLchar, 128> name {};
            GLsizei length = 0;
            GLint size = 0;
            GLenum type = 0;
            gl->glGetActiveAttrib(GLuint(program), GLuint(i), GLsizei(name.size()),
                                  &length, &size, &type, name.data());
            uniforms.append(QStringLiteral("attrib:")
                            + QString::fromLatin1(name.data(), length)
                            + QStringLiteral("@%1").arg(
                                gl->glGetAttribLocation(GLuint(program), name.data())));
        }
        if (g_forceDiagnosticMatrices) {
            const std::array<float, 16> projection {
                2.0f, 0.0f,  0.0f, 0.0f,
                0.0f, 2.0f,  0.0f, 0.0f,
                0.0f, 0.0f, -1.0f, 0.0f,
               -1.0f,-1.0f,  0.0f, 1.0f
            };
            const std::array<float, 16> identity {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
            const GLint projectionLocation = gl->glGetUniformLocation(
                GLuint(program), "vbaef96411e8ba5638375b23c8abbbc15");
            const GLint modelViewLocation = gl->glGetUniformLocation(
                GLuint(program), "va96ae5182131c77c37e85aa3cbfd6f1b");
            gl->glUniformMatrix4fv(projectionLocation, 1, GL_FALSE,
                                   projection.data());
            gl->glUniformMatrix4fv(modelViewLocation, 1, GL_FALSE,
                                   identity.data());
        }
    }
    QString dataPreview;
    if (data && byteCount > 0 && byteCount % int(sizeof(float)) == 0) {
        const auto* values = static_cast<const float*>(data);
        const int count = qMin(byteCount / int(sizeof(float)), 16);
        QStringList fields;
        for (int i = 0; i < count; ++i) {
            fields.append(QString::number(values[i], 'g', 6));
        }
        dataPreview = fields.join(QLatin1Char(','));
    }
    g_bufferGlState =
        QStringLiteral("target=0x%1/bytes=%2/usage=0x%3/program=%4/linked=%5/validated=%6/fbo=%7/uniforms=%8/data=%9")
            .arg(target, 0, 16).arg(byteCount).arg(usage, 0, 16)
            .arg(program).arg(linked).arg(validated).arg(fbo)
            .arg(uniforms.join(QLatin1Char('|')), dataPreview);
    if (target == GL_ARRAY_BUFFER && byteCount == 4 * 4 * sizeof(float)) {
        gl->glEnableVertexAttribArray(0);
        gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        gl->glEnableVertexAttribArray(1);
        gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                                  reinterpret_cast<const void*>(2 * sizeof(float)));
    }
    return buffer;
}

int deleteBuffer(quint64 host, qint32 buffer)
{
    Q_UNUSED(host);
    for (ProbeVbo& candidate : g_probeVbos) {
        if (candidate.id == GLuint(buffer)) {
            candidate.reserved = false;
            return 0;
        }
    }
    return -15;
}

quint32 createRenderbuffer(quint64, quint64, qint32 width, qint32 height,
                           qint32 internalFormat, qint32 precision, quint32)
{
    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    GLuint renderbuffer = 0;
    gl->glGenRenderbuffers(1, &renderbuffer);
    gl->glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    const GLenum format = internalFormat > 0 ? GLenum(internalFormat)
                                              : GLenum(GL_DEPTH24_STENCIL8);
    gl->glRenderbufferStorage(GL_RENDERBUFFER, format, qMax(1, width), qMax(1, height));
    const GLenum error = gl->glGetError();
    if (error == GL_NO_ERROR) {
        g_probeRenderbuffers.append(renderbuffer);
    } else {
        gl->glDeleteRenderbuffers(1, &renderbuffer);
        renderbuffer = 0;
    }
    g_renderbufferTrace.append(
        QStringLiteral("id=%1,size=%2x%3,format=0x%4,precision=%5,error=0x%6")
            .arg(renderbuffer).arg(width).arg(height).arg(internalFormat, 0, 16)
            .arg(precision).arg(error, 0, 16));
    return renderbuffer;
}

int clearRenderbuffer(quint64, quint64, qint32 renderbuffer)
{
    return g_probeRenderbuffers.contains(GLuint(renderbuffer)) ? 0 : -16;
}

void putService(QByteArray& api, int offset, void* callback)
{
    std::memcpy(api.data() + offset, &callback, sizeof(callback));
}

float defaultScalarParameter(quint64, const char* key, quint32)
{
    const QByteArray name = key ? QByteArray(key) : QByteArray();
    const auto traced = [&](float value) {
        if (qEnvironmentVariableIsSet("HFPL_PROBE_TRACE_VALUES")) {
            std::fprintf(stderr, "[hfpl] scalar %s=%g\n", name.constData(), value);
            std::fflush(stderr);
        }
        return value;
    };
    if (name == "screenGain" || name == "hueBalance" || name == "matteMax"
        || name == "gamma" || name == "colorDistMax") {
        return traced(1.0f);
    }
    if (name == "screenBalance" || name == "colorDistSoftness"
        || name.endsWith("Saturation") || name.endsWith("Lightness")) {
        return traced(0.5f);
    }
    if (name == "spillSuppress") {
        return traced(100.0f);
    }
    if (name == "spillReplaceSourceLayerBlur") {
        return traced(40.0f);
    }
    return traced(0.0f);
}

int defaultIntegerParameter(quint64, const char*, quint32)
{
    return 0;
}

bool defaultBooleanParameter(quint64, const char*, quint32)
{
    return false;
}

int defaultSetBooleanParameter(quint64, const char*, qint32, bool, qint32)
{
    return 0;
}

int defaultStringParameterLength(quint64, const char*, quint32, int* length)
{
    if (length) {
        *length = 0;
    }
    return length ? 0 : -10;
}

int defaultStringParameter(quint64, const char*, quint32, wchar_t* destination)
{
    if (destination) {
        destination[0] = L'\0';
    }
    return destination ? 0 : -10;
}

void defaultColorParameter(quint64, const char* key, quint32,
                           float* red, float* green, float* blue)
{
    const QByteArray name = key ? QByteArray(key) : QByteArray();
    const float component = name == "screenColor" ? 0.0f : 128.0f / 255.0f;
    if (red) {
        *red = component;
    }
    if (green) {
        *green = component;
    }
    if (blue) {
        *blue = component;
    }
}

void defaultPoint2dParameter(quint64, const char*, quint32, float* x, float* y)
{
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
}

void defaultPoint3dParameter(quint64, const char*, quint32,
                             float* x, float* y, float* z)
{
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    if (z) *z = 0.0f;
}

void defaultLayerId(quint64, const char*, quint32, char* layerId)
{
    if (layerId) {
        std::memset(layerId, 0, 0x28);
    }
}

int defaultLayerInfo(quint64, const char*, quint32, void* info)
{
    if (info) {
        std::memset(info, 0, 0x54);
    }
    return -5;
}

int defaultTimelineInfo(quint64, void* info)
{
    if (!info) {
        return -5;
    }
    std::memset(info, 0, 0x30);
    const qint32 width = g_probeFrameDimension;
    const qint32 height = g_probeFrameDimension;
    const double pixelAspect = 1.0;
    const qint32 frameCount = 1;
    const qint32 roundedFrameRate = 30;
    const double frameRate = 30.0;
    const qint32 sampleRate = 48000;
    const qint32 channelLayout = 2;
    const qint32 sampleDepth = 16;
    std::memcpy(static_cast<char*>(info) + 0x00, &width, sizeof(width));
    std::memcpy(static_cast<char*>(info) + 0x04, &height, sizeof(height));
    std::memcpy(static_cast<char*>(info) + 0x08, &pixelAspect, sizeof(pixelAspect));
    std::memcpy(static_cast<char*>(info) + 0x10, &frameCount, sizeof(frameCount));
    std::memcpy(static_cast<char*>(info) + 0x14, &roundedFrameRate,
                sizeof(roundedFrameRate));
    std::memcpy(static_cast<char*>(info) + 0x18, &frameRate, sizeof(frameRate));
    std::memcpy(static_cast<char*>(info) + 0x20, &sampleRate, sizeof(sampleRate));
    std::memcpy(static_cast<char*>(info) + 0x24, &channelLayout,
                sizeof(channelLayout));
    std::memcpy(static_cast<char*>(info) + 0x28, &sampleDepth, sizeof(sampleDepth));
    return 0;
}

int defaultNotifyProgress(quint64, qint32, qint32)
{
    return 0;
}

quint32 probeScratchTexture(quint64, quint64, qint32 width, qint32 height,
                            quint32 internalFormat, quint32 pixelFormat,
                            quint32 allocationQualifier, qint32, quint64,
                            qint32* actualWidth, qint32* actualHeight, quint32)
{
    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    const GLenum errorBeforeAllocation = gl->glGetError();
    width = qMax(1, width);
    height = qMax(1, height);
    GLuint texture = 0;
    gl->glGenTextures(1, &texture);
    gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl->glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    gl->glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    gl->glBindTexture(GL_TEXTURE_2D, texture);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const GLint storageFormat = internalFormat == GL_RGBA || internalFormat == GL_RGBA8
                                    || internalFormat == GL_RGBA16F
                                    || internalFormat == GL_RGBA32F
                                    || internalFormat == GL_DEPTH_COMPONENT
                                    || internalFormat == GL_DEPTH_COMPONENT16
                                    || internalFormat == GL_DEPTH_COMPONENT24
                                ? GLint(internalFormat) : GLint(GL_RGBA8);
    const GLenum uploadFormat = pixelFormat == GL_RGB || pixelFormat == GL_RGBA
                                    || pixelFormat == GL_RED || pixelFormat == GL_DEPTH_COMPONENT
                                ? GLenum(pixelFormat) : GLenum(GL_RGBA);
    const GLenum uploadType = GL_UNSIGNED_BYTE;
    gl->glTexImage2D(GL_TEXTURE_2D, 0, storageFormat, width, height, 0,
                     uploadFormat, uploadType, nullptr);
    g_scratchTrace.append(
        QStringLiteral("id=%1,size=%2x%3,internal=0x%4,pixel=0x%5,qualifier=0x%6,pre=0x%7,post=0x%8")
            .arg(texture).arg(width).arg(height)
            .arg(internalFormat, 0, 16).arg(pixelFormat, 0, 16)
            .arg(allocationQualifier, 0, 16).arg(errorBeforeAllocation, 0, 16)
            .arg(gl->glGetError(), 0, 16));
    if (qEnvironmentVariableIsSet("HFPL_PROBE_SYNC_PASSES")) {
        const QByteArray marker = QByteArray("[hfpl] scratch sync ")
                                  + QByteArray::number(g_scratchTrace.size()) + " "
                                  + g_scratchTrace.constLast().toUtf8() + " "
                                  + currentProgramValues(gl).toUtf8() + "\n";
        std::fwrite(marker.constData(), 1, size_t(marker.size()), stderr);
        std::fflush(stderr);
        gl->glFinish();
        std::fputs("[hfpl] scratch sync complete\n", stderr);
        std::fflush(stderr);
    }
    // Tannen always reports the allocation selected by the renderer. Several
    // multi-pass plugins use these outputs immediately for their viewport and
    // texture-coordinate calculations; leaving them untouched can turn a
    // successful allocation into an invalid deferred GL command stream.
    if (actualWidth) {
        *actualWidth = width;
    }
    if (actualHeight) {
        *actualHeight = height;
    }
    return texture;
}

int probeClearScratchTexture(quint64, quint64, qint32 texture)
{
    return texture > 0 ? 0 : -8;
}

QString inferredDependencyDirectory(const QString& pluginPath)
{
    QDir dir = QFileInfo(pluginPath).absoluteDir(); // category
    if (dir.dirName().compare(QStringLiteral("Plugins"), Qt::CaseInsensitive) != 0) {
        dir.cdUp(); // Plugins
    }
    if (dir.dirName().compare(QStringLiteral("Plugins"), Qt::CaseInsensitive) == 0) {
        dir.cdUp(); // application directory beside Plugins
        return dir.absolutePath();
    }
    return QFileInfo(pluginPath).absolutePath();
}

} // namespace

int main(int argc, char** argv)
{
    std::fputs("[hfpl] process entry\n", stderr);
    std::fflush(stderr);
    QGuiApplication app(argc, argv);
    std::fputs("[hfpl] QGuiApplication ready\n", stderr);
    std::fflush(stderr);
    QTextStream out(stdout);
    QTextStream err(stderr);
    const QStringList args = app.arguments();
    if (args.size() < 2 || args.size() > 4) {
        err << "Usage: hfpl_runtime_probe <plugin-or-directory> [dependency-directory] "
               "[message[,message...]|metadata|parameters|render|transition|audio|audio-transition|render-time|behavior|behavior-curve|behavior-stack|behavior-subobject|geometry|customui|track]\n";
        return 2;
    }

    const QFileInfo input(args.at(1));
    QStringList files;
    if (input.isDir()) {
        QDirIterator it(input.absoluteFilePath(), openvegas::plugin::nativePluginNameFilters(),
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            files.append(it.next());
        }
        files.sort(Qt::CaseInsensitive);
    } else {
        files.append(input.absoluteFilePath());
    }

    QList<int> messages;
    const bool metadataOnly = args.size() == 4
                              && args.at(3).compare(QStringLiteral("metadata"),
                                                   Qt::CaseInsensitive) == 0;
    const bool transitionApi = args.size() == 4
                               && args.at(3).compare(QStringLiteral("transition"),
                                                    Qt::CaseInsensitive) == 0;
    const bool audioApi = args.size() == 4
                          && args.at(3).compare(QStringLiteral("audio"),
                                                Qt::CaseInsensitive) == 0;
    const bool audioTransitionApi = args.size() == 4
                                    && args.at(3).compare(
                                           QStringLiteral("audio-transition"),
                                           Qt::CaseInsensitive) == 0;
    const bool behaviorApi = args.size() == 4
                             && args.at(3).compare(QStringLiteral("behavior"),
                                                   Qt::CaseInsensitive) == 0;
    const bool behaviorGraphApi = args.size() == 4
                                  && args.at(3).compare(QStringLiteral("behavior-graph"),
                                                        Qt::CaseInsensitive) == 0;
    const bool behaviorStackApi = args.size() == 4
                                  && args.at(3).compare(
                                         QStringLiteral("behavior-stack"),
                                         Qt::CaseInsensitive) == 0;
    const bool behaviorSubObjectApi = args.size() == 4
                                      && args.at(3).compare(
                                             QStringLiteral("behavior-subobject"),
                                             Qt::CaseInsensitive) == 0;
    const bool geometryApi = args.size() == 4
                             && args.at(3).compare(QStringLiteral("geometry"),
                                                   Qt::CaseInsensitive) == 0;
    const bool customUiApi = args.size() == 4
                             && args.at(3).compare(QStringLiteral("customui"),
                                                   Qt::CaseInsensitive) == 0;
    const bool trackApi = args.size() == 4
                          && args.at(3).compare(QStringLiteral("track"),
                                                Qt::CaseInsensitive) == 0;
    const bool rendererApi = args.size() == 4
                             && (args.at(3).compare(QStringLiteral("render"),
                                                   Qt::CaseInsensitive) == 0
                                 || transitionApi);
    const bool parametersOnly = args.size() == 4
                                && args.at(3).compare(QStringLiteral("parameters"),
                                                     Qt::CaseInsensitive) == 0;
    if (args.size() == 4 && !metadataOnly && !parametersOnly && !rendererApi
        && !audioApi && !audioTransitionApi && !behaviorApi && !behaviorGraphApi
        && !behaviorStackApi && !behaviorSubObjectApi && !geometryApi && !customUiApi
        && !trackApi) {
        for (const QString& value : args.at(3).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            bool ok = false;
            const int message = value.toInt(&ok, 0);
            if (ok) {
                messages.append(message);
            }
        }
    }

    if (behaviorSubObjectApi) {
        bool frameSpecified = false;
        const int requestedFrame = qEnvironmentVariableIntValue(
            "OPENVEGAS_HFPL_PROBE_FRAME", &frameSpecified);
        const int probeFrame = frameSpecified ? qBound(0, requestedFrame, 120) : 15;
        out << "file\tnotify105\tfirst-eight-opacity\tfirst-eight-clips\tfirst-four-records\tthread-match\n";
        int failures = 0;
        for (const QString& file : files) {
            const QString dependencyDir = QFileInfo(args.at(2)).absoluteFilePath();
            const auto metadata = openvegas::plugin::loadNativePluginMetadata(
                file, dependencyDir);
            QStringList parameterValues;
            for (const auto& parameter : metadata.parameters) {
                parameterValues.append(parameter.defaultValue);
            }
            const openvegas::core::Identifier id(
                QStringLiteral("probe.native.behavior.subobject"));
            openvegas::plugin::clearNativeEffectModules();
            openvegas::plugin::registerNativeBehaviorModule(
                id, file, dependencyDir, true, metadata.parameters);
            openvegas::plugin::NativeSubObjectResult result;
            result.transformations.resize(8);
            result.clipValues.resize(8);
            result.opacities.fill(1.0f, 8);
            const bool ok = openvegas::plugin::evaluateNativeSubObjectBehavior(
                result, probeFrame, probeFrame, 120, 1920, 1080, 30.0, id,
                parameterValues, {});
            openvegas::plugin::NativeSubObjectResult threaded;
            threaded.transformations.resize(8);
            threaded.clipValues.resize(8);
            threaded.opacities.fill(1.0f, 8);
            bool threadOk = false;
            QThread* renderThread = QThread::create([&]() {
                threadOk = openvegas::plugin::evaluateNativeSubObjectBehavior(
                    threaded, probeFrame, probeFrame, 120, 1920, 1080, 30.0, id,
                    parameterValues, {});
                openvegas::plugin::releaseNativeEffectThreadRenderer();
            });
            renderThread->start();
            renderThread->wait();
            delete renderThread;
            bool same = threadOk == ok;
            if (ok && threadOk) {
                for (int i = 0; i < 8; ++i) {
                    same = same && qAbs(result.opacities.at(i)
                                        - threaded.opacities.at(i)) < 0.00001f;
                    const float* mainMatrix = result.transformations.at(i).constData();
                    const float* workerMatrix = threaded.transformations.at(i).constData();
                    for (int j = 0; j < 16; ++j) {
                        same = same && qAbs(mainMatrix[j] - workerMatrix[j]) < 0.0001f;
                    }
                    same = same && result.clipValues.at(i).enabled
                                       == threaded.clipValues.at(i).enabled
                                && result.clipValues.at(i).secondary
                                       == threaded.clipValues.at(i).secondary;
                    for (int j = 0; j < 4; ++j) {
                        same = same
                               && qAbs(result.clipValues.at(i).values[j]
                                       - threaded.clipValues.at(i).values[j]) < 0.0001f;
                    }
                }
            }
            const bool typewriter = QFileInfo(file).baseName().compare(
                QStringLiteral("Typewriter"), Qt::CaseInsensitive) == 0;
            if (typewriter && probeFrame == 15 && (!ok || !same
                               || result.opacities.at(0) != 1.0f
                               || result.opacities.at(4) <= 0.0f
                               || result.opacities.at(4) >= 1.0f
                               || result.opacities.at(5) != 0.0f)) {
                ++failures;
            }
            const QString baseName = QFileInfo(file).baseName();
            const auto planarMatrix = [](const QMatrix4x4& matrix) {
                const float* m = matrix.constData();
                for (const int index : {2, 3, 6, 7, 8, 9, 11, 14}) {
                    if (qAbs(m[index]) > 0.0001f) return false;
                }
                return qAbs(m[10] - 1.0f) < 0.0001f
                       && qAbs(m[15] - 1.0f) < 0.0001f;
            };
            static const QStringList planarTextBehaviors {
                QStringLiteral("CentralSpiral"),
                QStringLiteral("CinemaStyle"),
                QStringLiteral("DoomoDesigns"),
                QStringLiteral("Random"),
                QStringLiteral("Random2"),
                QStringLiteral("RichTick"),
                QStringLiteral("ShuffleIn"),
                QStringLiteral("WavyStyle")
            };
            static const QStringList clippedTextBehaviors {
                QStringLiteral("DownDirInsert"),
                QStringLiteral("LeftDirInsert"),
                QStringLiteral("RightDirInsert"),
                QStringLiteral("UpDirInsert"),
                QStringLiteral("Push")
            };
            if (clippedTextBehaviors.contains(baseName, Qt::CaseInsensitive)) {
                if (!ok || !same || (probeFrame == 15
                                     && !result.clipValues.at(0).enabled)) {
                    ++failures;
                } else {
                    for (int i = 0; i < result.transformations.size(); ++i) {
                        const auto& clip = result.clipValues.at(i);
                        bool finiteClip = true;
                        for (float value : clip.values) {
                            finiteClip = finiteClip && qIsFinite(value);
                        }
                        if (!planarMatrix(result.transformations.at(i))
                            || !qIsFinite(result.opacities.at(i))
                            || !finiteClip) {
                            ++failures;
                            break;
                        }
                    }
                }
            }
            if (planarTextBehaviors.contains(baseName, Qt::CaseInsensitive)) {
                if (!ok || !same) {
                    ++failures;
                } else {
                    for (int i = 0; i < result.transformations.size(); ++i) {
                        if (result.clipValues.at(i).enabled
                            || result.clipValues.at(i).secondary
                            || !planarMatrix(result.transformations.at(i))
                            || !qIsFinite(result.opacities.at(i))
                            || result.opacities.at(i) < 0.0f
                            || result.opacities.at(i) > 1.0f) {
                            ++failures;
                            break;
                        }
                    }
                }
            }
            if (probeFrame == 15
                && baseName.compare(QStringLiteral("DropInByChar"),
                                    Qt::CaseInsensitive) == 0
                && (!ok || !same || result.clipValues.at(0).enabled
                    || !planarMatrix(result.transformations.at(0))
                    || result.opacities.at(0) != 1.0f
                    || result.transformations.at(0).constData()[13] >= -1.0f)) {
                ++failures;
            }
            if (probeFrame == 15
                && baseName.compare(QStringLiteral("StringFade"),
                                    Qt::CaseInsensitive) == 0
                && (!ok || !same || result.clipValues.at(0).enabled
                    || !planarMatrix(result.transformations.at(0))
                    || qAbs(result.opacities.at(0) - 0.5f) > 0.01f
                    || result.transformations.at(0).constData()[0] <= 1.0f)) {
                ++failures;
            }
            QStringList opacities;
            QStringList clips;
            QStringList records;
            for (int i = 0; i < 8; ++i) {
                opacities.append(QString::number(result.opacities.at(i)));
                const auto& clip = result.clipValues.at(i);
                clips.append(QStringLiteral("%1/%2")
                                 .arg(clip.enabled ? 1 : 0)
                                 .arg(clip.secondary ? 1 : 0));
                if (i < 4) {
                    const float* matrix = result.transformations.at(i).constData();
                    records.append(QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8")
                                       .arg(matrix[12]).arg(matrix[13])
                                       .arg(matrix[0]).arg(clip.values[0])
                                       .arg(clip.values[1]).arg(clip.values[2])
                                       .arg(clip.values[3]).arg(clip.secondary ? 1 : 0));
                }
            }
            out << QFileInfo(file).fileName() << '\t' << (ok ? 1 : 0)
                << '\t' << opacities.join(QLatin1Char(','))
                << '\t' << clips.join(QLatin1Char(','))
                << '\t' << records.join(QLatin1Char(';'))
                << '\t' << (same ? 1 : 0) << '\n';
            openvegas::plugin::releaseNativeEffectThreadRenderer();
        }
        out.flush();
        return failures == 0 ? 0 : 1;
    }

    if (behaviorGraphApi) {
        openvegas::plugin::clearNativeEffectModules();
        const QString file = files.value(0);
        const QString dependencyDir = QFileInfo(args.at(2)).absoluteFilePath();
        const auto metadata = openvegas::plugin::loadNativePluginMetadata(file, dependencyDir);
        openvegas::composition::Composition composition;
        auto& source = composition.addLayer(QStringLiteral("Same name"));
        source.transform.position = QPointF(0.0, 0.0);
        const auto sourceId = source.id;
        auto& target = composition.addLayer(QStringLiteral("Same name"));
        target.transform.position = QPointF(240.0, 80.0);
        const auto targetId = target.id;
        QStringList values;
        bool foundLayer = false;
        for (const auto& parameter : metadata.parameters) {
            if (parameter.type == QStringLiteral("layer")) {
                values.append(targetId.value());
                foundLayer = true;
            } else {
                values.append(parameter.defaultValue);
            }
        }
        const openvegas::core::Identifier id(QStringLiteral("probe.native.behavior.graph"));
        openvegas::plugin::registerNativeBehaviorModule(
            id, file, dependencyDir, true, metadata.parameters);
        openvegas::plugin::NativeBehaviorResult result;
        const bool rendered = openvegas::plugin::evaluateNativeBehavior(
            result, 30, 30, 120, 1920, 1080, 30.0, id, values,
            &composition, sourceId);
        const float* matrix = result.transformation.constData();
        const bool moved = std::abs(matrix[12]) > 0.001f
                           || std::abs(matrix[13]) > 0.001f
                           || std::abs(matrix[14]) > 0.001f;
        const QVector<openvegas::plugin::NativeBehaviorRequest> requests {{id, values}};
        openvegas::plugin::NativeBehaviorResult stacked;
        const bool stackedOk = openvegas::plugin::simulateNativeBehaviorStack(
            stacked, 30, 30, 120, 1920, 1080, 30.0, requests,
            &composition, sourceId);
        const float* stackedMatrix = stacked.transformation.constData();
        bool same = rendered && stackedOk;
        for (int axis = 12; axis <= 14; ++axis) {
            same = same && std::abs(matrix[axis] - stackedMatrix[axis]) < 0.001f;
        }
        composition.swapLayers(0, 1);
        openvegas::plugin::NativeBehaviorResult reordered;
        const bool reorderedOk = openvegas::plugin::simulateNativeBehaviorStack(
            reordered, 30, 30, 120, 1920, 1080, 30.0, requests,
            &composition, sourceId);
        const float* reorderedMatrix = reordered.transformation.constData();
        for (int axis = 12; axis <= 14; ++axis) {
            same = same && std::abs(matrix[axis] - reorderedMatrix[axis]) < 0.001f;
        }
        same = same && reorderedOk;
        openvegas::plugin::NativeBehaviorResult threaded;
        bool threadedOk = false;
        QThread* renderThread = QThread::create([&]() {
            threadedOk = openvegas::plugin::simulateNativeBehaviorStack(
                threaded, 30, 30, 120, 1920, 1080, 30.0, requests,
                &composition, sourceId);
            openvegas::plugin::releaseNativeEffectThreadRenderer();
        });
        renderThread->start();
        renderThread->wait();
        delete renderThread;
        const float* threadedMatrix = threaded.transformation.constData();
        for (int axis = 12; axis <= 14; ++axis) {
            same = same && std::abs(matrix[axis] - threadedMatrix[axis]) < 0.001f;
        }
        same = same && threadedOk;
        out << "layer-picker\trendered\tmoved\tstack-and-reorder\tposition"
               "\tstack\treordered\n"
            << foundLayer << '\t' << rendered << '\t' << moved << '\t'
            << same << '\t' << matrix[12] << ',' << matrix[13] << ','
            << matrix[14] << '\t' << stackedMatrix[12] << ','
            << stackedMatrix[13] << ',' << stackedMatrix[14] << '\t'
            << reorderedMatrix[12] << ',' << reorderedMatrix[13] << ','
            << reorderedMatrix[14] << '\n';
        out.flush();
        openvegas::plugin::releaseNativeEffectThreadRenderer();
        return rendered && moved && same ? 0 : 1;
    }

    if (behaviorStackApi) {
        openvegas::plugin::clearNativeEffectModules();
        QVector<openvegas::plugin::NativeBehaviorRequest> requests;
        const QSet<QString> wanted {QStringLiteral("acceleration"),
                                    QStringLiteral("drag"),
                                    QStringLiteral("gravity")};
        for (const QString& file : files) {
            const QString baseName = QFileInfo(file).baseName().toLower();
            if (!wanted.contains(baseName)) continue;
            const QString dependencyDir = args.size() >= 3
                                              ? QFileInfo(args.at(2)).absoluteFilePath()
                                              : inferredDependencyDirectory(file);
            const auto metadata = openvegas::plugin::loadNativePluginMetadata(
                file, dependencyDir);
            openvegas::plugin::NativeBehaviorRequest request;
            request.id = openvegas::core::Identifier(
                QStringLiteral("probe.native.behavior.%1").arg(baseName));
            for (const auto& parameter : metadata.parameters) {
                request.parameterValues.append(parameter.defaultValue);
            }
            openvegas::plugin::registerNativeBehaviorModule(
                request.id, file, dependencyDir, true, metadata.parameters);
            requests.push_back(request);
        }

        openvegas::plugin::NativeBehaviorResult mainResult;
        bool ok = requests.size() == wanted.size()
                  && openvegas::plugin::simulateNativeBehaviorStack(
                      mainResult, 30, 30, 120, 1920, 1080, 30.0, requests);
        openvegas::plugin::NativeBehaviorResult threadedResult;
        bool threadedOk = false;
        QThread* renderThread = QThread::create([&]() {
            threadedOk = openvegas::plugin::simulateNativeBehaviorStack(
                threadedResult, 30, 30, 120, 1920, 1080, 30.0, requests);
            openvegas::plugin::releaseNativeEffectThreadRenderer();
        });
        renderThread->start();
        renderThread->wait();
        delete renderThread;

        const float* mainMatrix = mainResult.transformation.constData();
        const float* threadedMatrix = threadedResult.transformation.constData();
        for (int i = 0; i < 16; ++i) {
            ok = ok && std::abs(mainMatrix[i] - threadedMatrix[i]) < 0.0001f;
        }
        // Drag's default value must reduce both accelerated axes while keeping
        // their direction. The exact limits also catch a reset velocity or a
        // second independent simulation pass.
        ok = ok && threadedOk && mainMatrix[12] > 0.0f
             && mainMatrix[12] < 186.0f && mainMatrix[13] < 0.0f
             && mainMatrix[13] > -258.333333f;
        out << "behaviors\trendered\tposition\tthread-position\n"
            << requests.size() << '\t' << (ok ? 1 : 0) << '\t'
            << mainMatrix[12] << ',' << mainMatrix[13] << ',' << mainMatrix[14]
            << '\t' << threadedMatrix[12] << ',' << threadedMatrix[13] << ','
            << threadedMatrix[14] << '\n';
        out.flush();
        openvegas::plugin::releaseNativeEffectThreadRenderer();
        return ok ? 0 : 1;
    }

    if (args.size() == 4 && args.at(3) == QLatin1String("render-time")) {
        // Whether a 2D effect moves by itself: a 64x64 pattern through the
        // module with its defaults at frame 0 and at frame 30 (layer frame
        // 30 of 120, 30 fps), and the pixels that differ between them.
        out << "file\trendered\tchanged-by-time\n";
        for (const QString& file : files) {
            const QString dependencyDir = QFileInfo(args.at(2)).absoluteFilePath();
            const auto metadata = openvegas::plugin::loadNativePluginMetadata(file, dependencyDir);
            QStringList values;
            for (const auto& parameter : metadata.parameters) values.append(parameter.defaultValue);
            const openvegas::core::Identifier id(QStringLiteral("probe.native.time"));
            openvegas::plugin::clearNativeEffectModules();
            openvegas::plugin::registerNativeEffectModule(id, file, dependencyDir, true,
                                                          metadata.parameters);
            QImage pattern(64, 64, QImage::Format_RGBA8888);
            for (int y = 0; y < pattern.height(); ++y)
                for (int x = 0; x < pattern.width(); ++x)
                    pattern.setPixelColor(x, y, QColor((x * 23 + ((x / 4 + y / 4) % 2 ? 97 : 11)) % 256,
                                                       (y * 31 + 40) % 256, ((x + y) * 13) % 256));
            const auto renderAt = [&](int frame, bool* ok) {
                QImage image = pattern;
                openvegas::plugin::NativeFrameTime time;
                time.frame = frame;
                time.layerFrame = frame;
                time.layerFrames = 120;
                *ok = openvegas::plugin::applyNativeEffectToImage(image, id, values, time);
                return image;
            };
            bool first = false, second = false;
            const QImage a = renderAt(0, &first);
            const QImage b = renderAt(30, &second);
            int changed = 0;
            for (int y = 0; y < a.height(); ++y)
                for (int x = 0; x < a.width(); ++x)
                    if (a.pixel(x, y) != b.pixel(x, y)) ++changed;
            out << QFileInfo(file).baseName() << '\t' << (first && second ? 1 : 0) << '\t'
                << changed << '\n';
            out.flush();
            openvegas::plugin::releaseNativeEffectThreadRenderer();
        }
        return 0;
    }

    if (args.size() == 4 && args.at(3) == QLatin1String("behavior-curve")) {
        // A behaviour's result over a 120-frame layer at 30 fps: a 400x100
        // layer standing at (300, 100) in a 1920x1080 frame (non-identity
        // matrix entries and the opacity at a few frames).
        for (const QString& file : files) {
            const QString dependencyDir = QFileInfo(args.at(2)).absoluteFilePath();
            const auto metadata = openvegas::plugin::loadNativePluginMetadata(file, dependencyDir);
            QStringList values;
            for (const auto& parameter : metadata.parameters) values.append(parameter.defaultValue);
            const openvegas::core::Identifier id(QStringLiteral("probe.native.curve"));
            openvegas::plugin::clearNativeEffectModules();
            openvegas::plugin::registerNativeBehaviorModule(id, file, dependencyDir, true, metadata.parameters);
            out << QFileInfo(file).baseName();
            for (int frame : {0, 5, 15, 30, 60, 90, 105, 119}) {
                openvegas::plugin::NativeBehaviorResult r;
                openvegas::plugin::NativeBehaviorLayer layer;
                layer.world[12] = 300.0f;
                layer.world[13] = 100.0f;
                layer.bounds = {-200.0f, -50.0f, 200.0f, 50.0f};
                layer.composition = QSize(1920, 1080);
                const bool ok = openvegas::plugin::evaluateNativeBehaviorFrame(
                    r, frame, frame, 120, 1920, 1080, 30.0, id, values, nullptr, {}, layer);
                const float* m = r.transformation.constData();
                out << "  f" << frame << (ok ? "" : "!") << " o=" << QString::number(r.opacity, 'f', 2) << " m=";
                static const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                for (int i = 0; i < 16; ++i)
                    if (std::abs(m[i] - identity[i]) > 0.005f) out << i << ':' << QString::number(m[i], 'g', 3) << ' ';
            }
            out << '\n';
            out.flush();
            openvegas::plugin::releaseNativeEffectThreadRenderer();
        }
        return 0;
    }

    if (behaviorApi) {
        out << "file\trendered\tbehavior\n";
        int failures = 0;
        for (const QString& file : files) {
            const QString dependencyDir = args.size() >= 3
                                              ? QFileInfo(args.at(2)).absoluteFilePath()
                                              : inferredDependencyDirectory(file);
            const auto metadata = openvegas::plugin::loadNativePluginMetadata(
                file, dependencyDir);
            QStringList parameterValues;
            for (const auto& parameter : metadata.parameters) {
                parameterValues.append(parameter.defaultValue);
            }
            const openvegas::core::Identifier id(QStringLiteral("probe.native.behavior"));
            openvegas::plugin::clearNativeEffectModules();
            openvegas::plugin::registerNativeBehaviorModule(
                id, file, dependencyDir, true, metadata.parameters);

            const auto describe = [](const openvegas::plugin::NativeBehaviorResult& value) {
                const float* m = value.transformation.constData();
                QStringList fields;
                for (int i = 0; i < 16; ++i) {
                    fields.append(QString::number(m[i], 'g', 7));
                }
                return QStringLiteral("opacity=%1;matrix=%2")
                    .arg(value.opacity, 0, 'g', 7)
                    .arg(fields.join(QLatin1Char(',')));
            };

            openvegas::plugin::NativeBehaviorResult mainResult;
            bool ok = openvegas::plugin::evaluateNativeBehavior(
                mainResult, 30, 30, 120, 1920, 1080, 30.0, id, parameterValues);
            openvegas::plugin::NativeBehaviorResult threadedResult;
            bool threadedOk = false;
            QThread* renderThread = QThread::create([&]() {
                threadedOk = openvegas::plugin::evaluateNativeBehavior(
                    threadedResult, 30, 30, 120, 1920, 1080, 30.0, id,
                    parameterValues);
                openvegas::plugin::releaseNativeEffectThreadRenderer();
            });
            renderThread->start();
            renderThread->wait();
            delete renderThread;
            const QString mainDescription = describe(mainResult);
            const QString threadedDescription = describe(threadedResult);
            ok = ok && threadedOk && mainDescription == threadedDescription;
            const QString baseName = QFileInfo(file).baseName().toLower();
            const float* matrix = mainResult.transformation.constData();
            if (baseName == QStringLiteral("acceleration")) {
                ok = ok && std::abs(matrix[12] - 186.0f) < 0.01f;
            } else if (baseName == QStringLiteral("gravity")) {
                ok = ok && std::abs(matrix[13] + 258.333333f) < 0.01f;
            }
            out << QDir::toNativeSeparators(file) << '\t' << (ok ? 1 : 0)
                << '\t' << mainDescription << ";thread=" << threadedDescription
                << '\n';
            if (!ok) ++failures;
            openvegas::plugin::releaseNativeEffectThreadRenderer();
        }
        out.flush();
        return failures == 0 ? 0 : 1;
    }

    if (geometryApi) {
        // A 100x100 square with a 50x50 hole, both loops double-sided, as
        // Flux hands a glyph outline to Geometry modules (Notify 101).
        const auto squareShape = [] {
            openvegas::plugin::NativeGeometryBatch batch;
            const auto loop = [&batch](float minimum, float maximum, bool reverse) {
                const float corners[4][2] = {{minimum, minimum}, {maximum, minimum},
                                             {maximum, maximum}, {minimum, maximum}};
                openvegas::plugin::NativeGeometryPolygon polygon;
                for (int corner = 0; corner < 4; ++corner) {
                    const int source = reverse ? 3 - corner : corner;
                    openvegas::plugin::NativeGeometryVertex vertex;
                    vertex.position[0] = corners[source][0];
                    vertex.position[1] = corners[source][1];
                    vertex.uv[0] = corners[source][0] / 100.0f;
                    vertex.uv[1] = corners[source][1] / 100.0f;
                    polygon.indices.append(qint32(batch.vertices.size()));
                    batch.vertices.append(vertex);
                }
                batch.polygons.append(polygon);
            };
            loop(0.0f, 100.0f, false);
            loop(25.0f, 75.0f, true);
            batch.extents[0] = 0.0f;
            batch.extents[1] = 100.0f;
            batch.extents[2] = 0.0f;
            batch.extents[3] = 100.0f;
            return QVector<openvegas::plugin::NativeGeometryBatch> {batch};
        };
        const auto describe = [](const QVector<openvegas::plugin::NativeGeometryBatch>& shape) {
            QStringList parts;
            for (const auto& batch : shape) {
                float minimum[3] {1e30f, 1e30f, 1e30f};
                float maximum[3] {-1e30f, -1e30f, -1e30f};
                for (const auto& vertex : batch.vertices) {
                    for (int axis = 0; axis < 3; ++axis) {
                        minimum[axis] = qMin(minimum[axis], vertex.position[axis]);
                        maximum[axis] = qMax(maximum[axis], vertex.position[axis]);
                    }
                }
                QStringList flags;
                for (const auto& polygon : batch.polygons) {
                    flags.append(QStringLiteral("%1:%2").arg(polygon.indices.size())
                                     .arg(polygon.flags));
                }
                parts.append(QStringLiteral("v=%1 t=%2 p=%3[%4] box=(%5,%6,%7)-(%8,%9,%10)")
                                 .arg(batch.vertices.size()).arg(batch.triangles.size())
                                 .arg(batch.polygons.size()).arg(flags.join(QLatin1Char(' ')))
                                 .arg(minimum[0], 0, 'f', 1).arg(minimum[1], 0, 'f', 1)
                                 .arg(minimum[2], 0, 'f', 1).arg(maximum[0], 0, 'f', 1)
                                 .arg(maximum[1], 0, 'f', 1).arg(maximum[2], 0, 'f', 1));
            }
            return parts.join(QStringLiteral(" | "));
        };
        // Defaults leave Extrude/Bevel at zero depth and RotateGeometry at
        // zero angles; OPENVEGAS_HFPL_PROBE_VALUES=name=value;... overrides.
        QHash<QString, QString> overrides {
            {QStringLiteral("extrusion"), QStringLiteral("20")},
            {QStringLiteral("bevelSize"), QStringLiteral("5")},
            {QStringLiteral("yRotation"), QStringLiteral("90")},
        };
        for (const QString& pair : qEnvironmentVariable("OPENVEGAS_HFPL_PROBE_VALUES")
                                       .split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const qsizetype equals = pair.indexOf(QLatin1Char('='));
            if (equals > 0) overrides.insert(pair.left(equals), pair.mid(equals + 1));
        }
        out << "file\tnotify101\tparameters\tbefore\tafter\tthread-match\n";
        int failures = 0;
        for (const QString& file : files) {
            const QString dependencyDir = args.size() >= 3
                                              ? QFileInfo(args.at(2)).absoluteFilePath()
                                              : inferredDependencyDirectory(file);
            const auto metadata = openvegas::plugin::loadNativePluginMetadata(
                file, dependencyDir);
            QStringList parameterValues;
            QStringList parameterNames;
            for (const auto& parameter : metadata.parameters) {
                parameterValues.append(overrides.value(parameter.name, parameter.defaultValue));
                parameterNames.append(parameter.name + QLatin1Char('=')
                                      + parameterValues.constLast());
            }
            const openvegas::core::Identifier id(QStringLiteral("probe.native.geometry"));
            openvegas::plugin::clearNativeEffectModules();
            openvegas::plugin::registerNativeGeometryModule(
                id, file, dependencyDir, true, metadata.parameters);
            auto shape = squareShape();
            const QString before = describe(shape);
            const bool ok = openvegas::plugin::applyNativeGeometryEffect(
                shape, id, parameterValues, 15, 15, 120, 30.0);
            auto threaded = squareShape();
            bool threadOk = false;
            QThread* renderThread = QThread::create([&]() {
                threadOk = openvegas::plugin::applyNativeGeometryEffect(
                    threaded, id, parameterValues, 15, 15, 120, 30.0);
                openvegas::plugin::releaseNativeEffectThreadRenderer();
            });
            renderThread->start();
            renderThread->wait();
            delete renderThread;
            const QString after = describe(shape);
            const bool same = ok == threadOk && after == describe(threaded);
            out << QDir::toNativeSeparators(file) << '\t' << (ok ? 1 : 0) << '\t'
                << parameterNames.join(QLatin1Char(',')) << '\t' << before << '\t'
                << after << '\t' << (same ? 1 : 0) << '\n';
            if (!ok || !same) ++failures;
            openvegas::plugin::releaseNativeEffectThreadRenderer();
        }
        out.flush();
        return failures == 0 ? 0 : 1;
    }

    if (trackApi) {
        // MotionTrack's analysis as the host drives it: "Motion From" set to a
        // footage layer (Notify 7), then Notify(18) for as long as the module
        // asks for background processing. The footage is a random texture
        // drifting 2 px right and 1 px down a frame.
        out << "file\tstep\thandled\tbackground\tdelay\tstatus\tframes\n";
        int failures = 0;
        constexpr int kFrames = 30;
        const QSize kSize(320, 180);
        static const QString sourceLayer = QStringLiteral("11111111-2222-3333-4444-000000000001");
        static const QString ownLayer = QStringLiteral("11111111-2222-3333-4444-000000000002");
        QImage texture(kSize.width() + 2 * kFrames + 8, kSize.height() + kFrames + 8,
                       QImage::Format_RGBA8888);
        quint32 seed = 12345u;
        for (int y = 0; y < texture.height(); ++y) {
            for (int x = 0; x < texture.width(); ++x) {
                seed = seed * 1664525u + 1013904223u;
                const int v = int((seed >> 24) & 0xff);
                texture.setPixelColor(x, y, QColor(v, v, v));
            }
        }
        texture = texture.scaled(texture.size() * 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        // Blurred noise: corners KLT likes.
        {
            QImage reduced = texture.scaled(texture.width() / 4, texture.height() / 4,
                                          Qt::IgnoreAspectRatio, Qt::FastTransformation);
            texture = reduced.scaled(texture.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        openvegas::plugin::NativeSourceHost host;
        host.layerInfo = [](const QString& id) {
            openvegas::plugin::NativeLayerInfo info;
            if (id == sourceLayer || id == ownLayer) {
                info.valid = true; info.type = 0; info.startFrame = 0; info.durationFrames = kFrames;
                info.size = QSize(320, 180);

            }
            return info;
        };
        host.assetInfo = [](const QString& id) {
            openvegas::plugin::NativeAssetInfo info;
            if (id == sourceLayer) {
                info.valid = true; info.frameCount = kFrames; info.frameRate = 30.0;
                info.type = 0; info.key = QStringLiteral("probe-footage");
            }
            return info;
        };
        int requested = 0;
        host.assetFrame = [&texture, &requested, kSize](const QString& id, int frame) {
            if (id != sourceLayer || frame < 0 || frame >= kFrames) return QImage();
            ++requested;
            QImage picture = texture.copy(QRect(QPoint(2 * kFrames - 2 * frame, kFrames - frame), kSize));
            if (qEnvironmentVariableIsSet("OPENVEGAS_PROBE_STILL_TOP")) {
                // The top half holds still: only the bottom half moves.
                QPainter painter(&picture);
                painter.drawImage(QPoint(0, 0), texture.copy(QRect(QPoint(2 * kFrames, kFrames),
                                                                   QSize(kSize.width(), kSize.height() / 2))));
            }
            return picture;
        };
        openvegas::plugin::setNativeSourceHost(host);
        for (const QString& file : files) {
            const QString dependencyDir = args.size() >= 3
                                              ? QFileInfo(args.at(2)).absoluteFilePath()
                                              : inferredDependencyDirectory(file);
            const auto metadata = openvegas::plugin::loadNativePluginMetadata(file, dependencyDir);
            QStringList values;
            int sourceIndex = -1, statusIndex = -1;
            for (int i = 0; i < metadata.parameters.size(); ++i) {
                values.append(metadata.parameters.at(i).defaultValue);
                if (metadata.parameters.at(i).name == QLatin1String("motionFromLayer")) sourceIndex = i;
                if (metadata.parameters.at(i).name == QLatin1String("analysisStatus")) statusIndex = i;
            }
            if (sourceIndex < 0) {
                out << QDir::toNativeSeparators(file) << "\tparameters\t0\t0\t0\tno motionFromLayer\t0\n";
                ++failures;
                continue;
            }
            values[sourceIndex] = sourceLayer;
            const openvegas::core::Identifier id(QStringLiteral("probe.native.track"));
            openvegas::plugin::clearNativeEffectModules();
            openvegas::plugin::clearNativeTrackedFeatures();
            openvegas::plugin::registerNativeBehaviorModule(id, file, dependencyDir, false,
                                                             metadata.parameters);
            openvegas::plugin::NativeCustomUiView view;
            view.layerId = openvegas::core::Identifier(ownLayer);
            view.area = kSize;
            view.layerFrameEnd = kFrames;
            view.frameRate = 30.0;
            view.instanceKey = QStringLiteral("probe");
            QString status;
            const auto apply = [&](const openvegas::plugin::NativeCustomUiResult& result) {
                for (auto it = result.values.cbegin(); it != result.values.cend(); ++it)
                    if (it.key() >= 0 && it.key() < values.size()) values[it.key()] = it.value();
                if (statusIndex >= 0) status = values.at(statusIndex);
            };
            const auto report = [&](const QString& step,
                                    const openvegas::plugin::NativeCustomUiResult& result) {
                out << QDir::toNativeSeparators(file) << '\t' << step << '\t'
                    << (result.handled ? 1 : 0) << '\t' << (result.backgroundRequested ? 1 : 0)
                    << '\t' << result.backgroundDelayMs << '\t' << status << '\t'
                    << openvegas::plugin::nativeTrackedFeatures(QStringLiteral("probe-footage")).size();
                // Controls the module enabled or disabled (SetPropertyState).
                for (auto it = result.enabled.cbegin(); it != result.enabled.cend(); ++it)
                    out << '\t' << it.key() << (it.value() ? "=on" : "=off");
                out << '\n';
                out.flush();
            };
            // OPENVEGAS_PROBE_NO_SETUP: the application's instance host sends
            // property changes before the viewer ever sets the custom UI up.
            if (!qEnvironmentVariableIsSet("OPENVEGAS_PROBE_NO_SETUP"))
                report(QStringLiteral("setup"), openvegas::plugin::nativeCustomUiSetup(id, values, view));
            auto result = openvegas::plugin::nativePropertyChanged(id, values, view,
                                                                    QStringLiteral("motionFromLayer"));
            apply(result);
            report(QStringLiteral("motionFromLayer"), result);
            QString lastStatus = status;
            int calls = 0;
            QElapsedTimer clock;
            clock.start();
            // Values the module changed itself come back as property changes,
            // as Tannen notifies them.
            // Values the module sets itself are not echoed back as property
            // changes (it asks for the work it needs on its own).
            const auto changed = [&](const openvegas::plugin::NativeCustomUiResult& from) {
                for (auto it = from.values.cbegin(); it != from.values.cend(); ++it) {
                    const QString key = metadata.parameters.value(it.key()).name;
                    if (key.isEmpty() || key == QLatin1String("analysisStatus")) continue;
                    report(QStringLiteral("set %1=%2").arg(key, it.value()), from);
                }
            };
            const auto runBackground = [&]() {
                while (result.backgroundRequested && calls < 20000 && clock.elapsed() < 180000) {
                    if (result.backgroundDelayMs > 0)
                        QThread::msleep(quint32(qMin(result.backgroundDelayMs, 200)));
                    result = openvegas::plugin::nativeBackgroundProcess(id, values, view);
                    apply(result);
                    ++calls;
                    if (status != lastStatus || !result.handled) {
                        report(QStringLiteral("background %1").arg(calls), result);
                        lastStatus = status;
                    }
                    changed(result);
                }
            };
            runBackground();
            if (status.contains(QLatin1String("Draw"), Qt::CaseInsensitive)) {
                // Draw around the middle of the frame, as the viewer would
                // deliver it: press, a closed path of moves, release.
                openvegas::plugin::NativeCustomUiPointer pointer;
                pointer.button = 1; pointer.buttons = 1; pointer.clicks = 1; pointer.pressed = true;
                // The loop has to close by crossing its own start, as a hand
                // drawing around an area does.
                QVector<QPoint> path {{100, 50}, {160, 50}, {220, 50}, {220, 90}, {220, 130},
                                      {160, 130}, {100, 130}, {100, 90}, {100, 60}, {130, 40}};
                if (qEnvironmentVariableIsSet("OPENVEGAS_PROBE_LASSO_TOP"))   // canvas pixels, Y down
                    path = {{100, 15}, {160, 15}, {220, 15}, {220, 40}, {220, 70}, {160, 70},
                            {100, 70}, {100, 40}, {100, 25}, {130, 8}};
                pointer.position = path.first();
                auto step = openvegas::plugin::nativeCustomUiMouse(
                    id, values, view, openvegas::plugin::NativeCustomUiMouse::Press, pointer);
                apply(step);
                report(QStringLiteral("press"), step);
                for (const QPoint& point : path) {
                    pointer.position = point;
                    step = openvegas::plugin::nativeCustomUiMouse(
                        id, values, view, openvegas::plugin::NativeCustomUiMouse::Move, pointer);
                    apply(step);
                }
                pointer.pressed = false; pointer.buttons = 0;
                step = openvegas::plugin::nativeCustomUiMouse(
                    id, values, view, openvegas::plugin::NativeCustomUiMouse::Release, pointer);
                apply(step);
                report(QStringLiteral("release"), step);
                {
                    openvegas::plugin::NativeCustomUiResult drawn;
                    const QImage overlay = openvegas::plugin::nativeCustomUiRender(id, values, view, &drawn);
                    int inside = 0, outside = 0;
                    for (int y = 0; y < overlay.height(); ++y)
                        for (int x = 0; x < overlay.width(); ++x)
                            if (qAlpha(overlay.pixel(x, y)) > 0)
                                (QRect(100, 50, 121, 81).contains(x, y) ? inside : outside)++;
                    overlay.save(QDir::temp().filePath(QStringLiteral("motiontrack-selection.png")));
                    out << QDir::toNativeSeparators(file) << "\tselection overlay\t" << overlay.width()
                        << 'x' << overlay.height() << " inside=" << inside << " outside=" << outside << '\n';
                }
                if (step.backgroundRequested) result = step;
                changed(step);
                runBackground();
                {
                    openvegas::plugin::NativeCustomUiResult drawn;
                    const QImage overlay = openvegas::plugin::nativeCustomUiRender(id, values, view, &drawn);
                    QHash<QRgb, int> colors;
                    for (int y = 0; y < overlay.height(); ++y)
                        for (int x = 0; x < overlay.width(); ++x)
                            if (qAlpha(overlay.pixel(x, y)) > 0) ++colors[overlay.pixel(x, y)];
                    overlay.save(QDir::temp().filePath(QStringLiteral("motiontrack-final.png")));
                    out << QDir::toNativeSeparators(file) << "\tfinal overlay colours";
                    for (auto it = colors.cbegin(); it != colors.cend(); ++it)
                        if (it.value() > 20) out << ' ' << Qt::hex << it.key() << Qt::dec << '=' << it.value();
                    out << '\n';
                }
            }
            report(QStringLiteral("done after %1 calls, %2 frames read").arg(calls).arg(requested),
                   result);
            std::array<float, 16> matrix {};
            std::array<float, 16> last {};
            bool transformed = false;
            for (int frame : {0, 10, 20, 29}) {
                if (openvegas::plugin::nativeInstanceTransformation(id, values, view, frame, frame, &matrix)) {
                    last = matrix;
                    transformed = true;
                    out << QDir::toNativeSeparators(file) << "\ttransform " << frame << '\t';
                    for (float v : matrix) out << v << ' ';
                    out << '\n';
                } else {
                    out << QDir::toNativeSeparators(file) << "\ttransform " << frame << "\tnone\n";
                }
            }
            const auto tracked = openvegas::plugin::nativeTrackedFeatures(QStringLiteral("probe-footage"));
            double dx = 0, dy = 0; int points = 0;
            for (auto it = tracked.cbegin(); it != tracked.cend(); ++it) {
                for (qsizetype i = 0; i < it->from.size(); ++i) {
                    dx += it->to[i].x() - it->from[i].x();
                    dy += it->to[i].y() - it->from[i].y();
                    ++points;
                }
            }
            out << QDir::toNativeSeparators(file) << "\tfeatures\t" << tracked.size() << " frames, "
                << points << " points, mean step " << (points ? dx / points : 0.0) << ", "
                << (points ? dy / points : 0.0) << '\n';
            for (int frame : {0, 1, 15}) {
                const auto it = tracked.constFind(frame);
                if (it == tracked.cend()) continue;
                out << QDir::toNativeSeparators(file) << "\tframe " << frame << " affine";
                for (float v : it->affine) out << ' ' << v;
                if (!it->from.isEmpty())
                    out << " first " << it->from.first().x() << ',' << it->from.first().y() << " -> "
                        << it->to.first().x() << ',' << it->to.first().y();
                out << '\n';
            }
            const QByteArray data = openvegas::plugin::nativeInstanceData(id, values, view);
            out << QDir::toNativeSeparators(file) << "\tinstance data\t" << data.size() << " bytes\n";
            if (tracked.size() < kFrames / 2) ++failures;
            // The footage moved 2 px right and 1 px down a frame: by frame 29
            // the layer follows it 58 px right and 29 px down (Y up). With
            // OPENVEGAS_PROBE_STILL_TOP/LASSO_TOP the tracked area holds still.
            const bool stillTop = qEnvironmentVariableIsSet("OPENVEGAS_PROBE_STILL_TOP");
            const double wantX = stillTop ? 0.0 : 58.0, wantY = stillTop ? 0.0 : -29.0;
            const bool followed = transformed && std::abs(last[12] - wantX) < 1.0
                                  && std::abs(last[13] - wantY) < 1.0;
            out << QDir::toNativeSeparators(file) << "	result	"
                << (followed ? "follows the footage" : "WRONG") << '\n';
            if (!followed) ++failures;
            openvegas::plugin::nativeCustomUiShutdown(id, values, view);
            openvegas::plugin::releaseNativeEffectThreadRenderer();
        }
        openvegas::plugin::setNativeSourceHost({});
        out.flush();
        return failures == 0 ? 0 : 1;
    }

    if (customUiApi) {
        // The viewer's custom UI messages on a 640x360 canvas, as the Viewer
        // sends them while the effect is selected (Notify 1001..1013).
        out << "file\tstep\thandled\tredraw\tcursor\tbackground\textra\n";
        int failures = 0;
        for (const QString& file : files) {
            const QString dependencyDir = args.size() >= 3
                                              ? QFileInfo(args.at(2)).absoluteFilePath()
                                              : inferredDependencyDirectory(file);
            const auto metadata = openvegas::plugin::loadNativePluginMetadata(file, dependencyDir);
            QStringList values;
            for (const auto& parameter : metadata.parameters) values.append(parameter.defaultValue);
            const openvegas::core::Identifier id(QStringLiteral("probe.native.customui"));
            openvegas::plugin::clearNativeEffectModules();
            openvegas::plugin::registerNativeBehaviorModule(id, file, dependencyDir, false,
                                                             metadata.parameters);
            if (!openvegas::plugin::nativeEffectHasCustomUi(id)) {
                out << QDir::toNativeSeparators(file) << "\tregistry\t0\t0\t0\t0\tno custom UI\n";
                continue;
            }
            openvegas::plugin::NativeCustomUiView view;
            view.area = QSize(640, 360);
            view.frame = view.parameterFrame = view.layerFrame = 15;
            view.layerFrameEnd = 120;
            const auto report = [&](const char* step,
                                    const openvegas::plugin::NativeCustomUiResult& result,
                                    const QString& extra = QString()) {
                out << QDir::toNativeSeparators(file) << '\t' << step << '\t'
                    << (result.handled ? 1 : 0) << '\t' << (result.redraw ? 1 : 0) << '\t'
                    << result.cursor << '\t' << (result.backgroundRequested ? 1 : 0) << '\t'
                    << extra << '\n';
            };
            report("setup", openvegas::plugin::nativeCustomUiSetup(id, values, view));
            report("focus", openvegas::plugin::nativeCustomUiFocus(id, values, view, true));
            const quint32 control = openvegas::plugin::nativeKeysym(Qt::Key_Control, QString());
            const auto ctrlDown = openvegas::plugin::nativeCustomUiKey(
                id, values, view, openvegas::plugin::NativeCustomUiKey::Press, control, QString());
            report("ctrl-down", ctrlDown);
            report("ctrl-up", openvegas::plugin::nativeCustomUiKey(
                id, values, view, openvegas::plugin::NativeCustomUiKey::Release, control, QString()));
            openvegas::plugin::NativeCustomUiPointer pointer;
            pointer.position = QPoint(320, 180);
            report("move", openvegas::plugin::nativeCustomUiMouse(
                id, values, view, openvegas::plugin::NativeCustomUiMouse::Move, pointer));
            pointer.pressed = true; pointer.button = 1; pointer.buttons = 1; pointer.clicks = 1;
            report("press", openvegas::plugin::nativeCustomUiMouse(
                id, values, view, openvegas::plugin::NativeCustomUiMouse::Press, pointer));
            pointer.position = QPoint(360, 200);
            report("drag", openvegas::plugin::nativeCustomUiMouse(
                id, values, view, openvegas::plugin::NativeCustomUiMouse::Move, pointer));
            pointer.pressed = false; pointer.buttons = 0;
            report("release", openvegas::plugin::nativeCustomUiMouse(
                id, values, view, openvegas::plugin::NativeCustomUiMouse::Release, pointer));
            openvegas::plugin::NativeCustomUiResult drawn;
            const QImage overlay = openvegas::plugin::nativeCustomUiRender(id, values, view, &drawn);
            if (!overlay.isNull())
                overlay.save(QDir::temp().filePath(QFileInfo(file).completeBaseName() + QStringLiteral("-customui.png")));
            int painted = 0;
            for (int y = 0; y < overlay.height(); ++y)
                for (int x = 0; x < overlay.width(); ++x)
                    if (qAlpha(overlay.pixel(x, y)) > 0) ++painted;
            report("render", drawn, QStringLiteral("%1x%2 painted=%3")
                                        .arg(overlay.width()).arg(overlay.height()).arg(painted));
            report("context", openvegas::plugin::nativeCustomUiHasContextMenu(id, values, view));
            report("blur", openvegas::plugin::nativeCustomUiFocus(id, values, view, false));
            report("shutdown", openvegas::plugin::nativeCustomUiShutdown(id, values, view));
            // MotionTrack sets its Ctrl state and asks for a redraw.
            if (QFileInfo(file).baseName().compare(QStringLiteral("MotionTrack"),
                                                   Qt::CaseInsensitive) == 0
                && !ctrlDown.redraw) {
                ++failures;
            }
            openvegas::plugin::releaseNativeEffectThreadRenderer();
        }
        out.flush();
        return failures == 0 ? 0 : 1;
    }

    if (audioApi || audioTransitionApi) {
        out << "file\trendered\taudio\n";
        int failures = 0;
        for (const QString& file : files) {
            const QString dependencyDir = args.size() >= 3
                                              ? QFileInfo(args.at(2)).absoluteFilePath()
                                              : inferredDependencyDirectory(file);
            const auto metadata = openvegas::plugin::loadNativePluginMetadata(
                file, dependencyDir);
            QStringList parameterValues;
            for (const auto& parameter : metadata.parameters) {
                parameterValues.append(parameter.defaultValue);
            }
            const openvegas::core::Identifier id(QStringLiteral("probe.native.audio"));
            openvegas::plugin::clearNativeEffectModules();
            if (audioTransitionApi) {
                openvegas::plugin::registerNativeAudioTransitionModule(
                    id, file, dependencyDir, true, metadata.parameters);
            } else {
                openvegas::plugin::registerNativeAudioEffectModule(
                    id, file, dependencyDir, true, metadata.parameters);
            }

            constexpr int channels = 2;
            // Effects get half a second so reverbs, Echo and Reverse have a
            // layer long enough for their GetSampleRanges requests.
            const int frameCount = audioTransitionApi ? 2048 : 24000;
            QVector<qint16> from(frameCount * channels);
            QVector<qint16> to(frameCount * channels);
            for (int frame = 0; frame < frameCount; ++frame) {
                const qint16 left = qint16(audioTransitionApi
                    ? ((frame * 97) % 24000) - 12000
                    : std::lround(6000.0 * std::sin(frame * 0.0576)
                                  + 3000.0 * std::sin(frame * 0.1701)));
                const qint16 right = qint16(audioTransitionApi
                    ? 9000 - ((frame * 53) % 18000)
                    : std::lround(5000.0 * std::sin(frame * 0.0333)
                                  + 2500.0 * std::sin(frame * 0.2618)));
                from[frame * channels] = left;
                from[frame * channels + 1] = right;
                to[frame * channels] = qint16(-left / 2);
                to[frame * channels + 1] = qint16(-right / 2);
            }
            const openvegas::plugin::NativeAudioLayer layer {frameCount, 30.0};
            const auto renderEffect = [&](QVector<qint16>& samples, int blockFrames,
                                          const QString& instanceKey) {
                return openvegas::plugin::applyNativeAudioEffectBlocks(
                    samples, channels, 48000, 0, id,
                    [&](qint64) { return parameterValues; }, instanceKey,
                    blockFrames, layer);
            };
            QVector<qint16> rendered = from;
            QElapsedTimer renderTimer;
            renderTimer.start();
            bool ok = audioTransitionApi
                          ? openvegas::plugin::applyNativeAudioTransition(
                                rendered, from, to, channels, 0, frameCount, id,
                                parameterValues)
                          : renderEffect(rendered, 480, {});
            // Realtime budget: frameCount/48 ms of audio in 480-frame blocks.
            const qint64 renderMs = renderTimer.elapsed();
            const auto checksum = [](const QVector<qint16>& values) {
                quint64 sum = 1469598103934665603ULL;
                for (qint16 value : values) {
                    sum ^= quint16(value);
                    sum *= 1099511628211ULL;
                }
                return sum;
            };
            const quint64 mainChecksum = checksum(rendered);

            QVector<qint16> threaded = from;
            bool threadedOk = false;
            QThread* renderThread = QThread::create([&]() {
                threadedOk = audioTransitionApi
                                 ? openvegas::plugin::applyNativeAudioTransition(
                                       threaded, from, to, channels, 0, frameCount,
                                       id, parameterValues)
                                 : renderEffect(threaded, 480, {});
                openvegas::plugin::releaseNativeEffectThreadRenderer();
            });
            renderThread->start();
            renderThread->wait();
            delete renderThread;
            ok = ok && threadedOk && mainChecksum == checksum(threaded);
            // Realtime uses 480-frame blocks, offline export may use larger
            // ones. With exact source ranges the result should not depend on
            // the block size; the difference is reported rather than fatal.
            // Silence from a non-silent input means the ranges were wrong.
            QString blockComparison;
            if (ok && !audioTransitionApi) {
                QVector<qint16> large = from;
                const bool largeOk = renderEffect(large, 4096,
                                                  QStringLiteral("probe-large"));
                int maxDifference = 0;
                for (int i = 0; i < large.size() && i < rendered.size(); ++i) {
                    maxDifference = qMax(maxDifference,
                                         qAbs(int(large.at(i)) - int(rendered.at(i))));
                }
                const auto rms = [](const QVector<qint16>& values) {
                    double sum = 0.0;
                    for (qint16 value : values) sum += double(value) * value;
                    return values.isEmpty() ? 0.0 : std::sqrt(sum / values.size());
                };
                const double inputRms = rms(from);
                const double outputRms = rms(rendered);
                blockComparison = QStringLiteral(";large=%1;maxdiff=%2;rms=%3->%4;ms=%5/%6")
                                      .arg(largeOk ? QStringLiteral("ok")
                                                   : QStringLiteral("failed"))
                                      .arg(maxDifference)
                                      .arg(inputRms, 0, 'f', 1)
                                      .arg(outputRms, 0, 'f', 1)
                                      .arg(renderMs).arg(frameCount / 48);
                ok = largeOk && outputRms > 1.0;
            }
            out << QDir::toNativeSeparators(file) << '\t' << (ok ? 1 : 0)
                << '\t' << QStringLiteral("0x%1;thread=0x%2;first=%3,%4;last=%5,%6")
                                  .arg(mainChecksum, 16, 16, QLatin1Char('0'))
                                  .arg(checksum(threaded), 16, 16, QLatin1Char('0'))
                                  .arg(rendered.value(0)).arg(rendered.value(1))
                                  .arg(rendered.value(rendered.size() - 2))
                                  .arg(rendered.value(rendered.size() - 1))
                << blockComparison << '\n';
            if (!ok) ++failures;
            openvegas::plugin::releaseNativeEffectThreadRenderer();
        }
        out.flush();
        return failures == 0 ? 0 : 1;
    }

    if (parametersOnly) {
        out << "file\tchecked\tresult\tfault\tparameters\terror\n";
        int failures = 0;
        const auto oneLineField = [](QString value) {
            value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
            value.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
            value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
            value.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
            value.replace(QLatin1Char('|'), QStringLiteral("\\x7c"));
            value.replace(QLatin1Char(';'), QStringLiteral("\\x3b"));
            return value;
        };
        for (const QString& file : files) {
            const QString dependencyDir = args.size() >= 3
                                              ? QFileInfo(args.at(2)).absoluteFilePath()
                                              : inferredDependencyDirectory(file);
            const auto metadata = openvegas::plugin::loadNativePluginMetadata(
                file, dependencyDir);
            QStringList descriptions;
            for (const auto& parameter : metadata.parameters) {
                descriptions.append(
                    QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
                        .arg(oneLineField(parameter.name),
                             oneLineField(parameter.displayName),
                             oneLineField(parameter.type),
                             oneLineField(parameter.defaultValue),
                             oneLineField(parameter.unit),
                             oneLineField(parameter.group))
                        .arg(parameter.minimum, 0, 'g', 8)
                        .arg(parameter.maximum, 0, 'g', 8)
                        .arg(oneLineField(parameter.choices.join(QLatin1Char(',')))));
            }
            out << QDir::toNativeSeparators(file) << '\t'
                << (metadata.parametersChecked ? 1 : 0) << '\t'
                << metadata.notifyParametersResult << '\t'
                << QStringLiteral("0x%1").arg(quint32(metadata.parametersFaultCode),
                                                8, 16, QLatin1Char('0'))
                << '\t' << descriptions.join(QLatin1Char(';')) << '\t'
                << metadata.parametersError << '\n';
            if (!metadata.parametersChecked || metadata.parametersFaultCode != 0) {
                ++failures;
            }
        }
        out.flush();
        return failures == 0 ? 0 : 1;
    }

    if (rendererApi) {
        out << "file\trendered\tpixel\n";
        int failures = 0;
        for (const QString& file : files) {
            const QString dependencyDir = args.size() >= 3
                                              ? QFileInfo(args.at(2)).absoluteFilePath()
                                              : inferredDependencyDirectory(file);
            const openvegas::core::Identifier id(QStringLiteral("probe.native"));
            QVector<openvegas::plugin::EffectParameterSpec> parameterSpecs;
            QStringList parameterValues;
            const QString baseName = QFileInfo(file).completeBaseName();
            if (baseName.compare(QStringLiteral("BrightnessContrast"),
                                 Qt::CaseInsensitive) == 0) {
                openvegas::plugin::EffectParameterSpec brightness;
                brightness.name = QStringLiteral("brightness");
                brightness.defaultValue = QStringLiteral("100");
                openvegas::plugin::EffectParameterSpec contrast;
                contrast.name = QStringLiteral("contrast");
                contrast.defaultValue = QStringLiteral("0");
                parameterSpecs = {brightness, contrast};
                parameterValues = {QStringLiteral("100"), QStringLiteral("0")};
            } else if (baseName.compare(QStringLiteral("Gamma"),
                                        Qt::CaseInsensitive) == 0) {
                for (const QString& name : {QStringLiteral("redGamma"),
                                            QStringLiteral("greenGamma"),
                                            QStringLiteral("blueGamma")}) {
                    openvegas::plugin::EffectParameterSpec channel;
                    channel.name = name;
                    channel.defaultValue = QStringLiteral("2");
                    parameterSpecs.append(channel);
                    parameterValues.append(QStringLiteral("2"));
                }
            } else if (baseName.compare(QStringLiteral("Fill"),
                                        Qt::CaseInsensitive) == 0) {
                openvegas::plugin::EffectParameterSpec color;
                color.name = QStringLiteral("fillColor");
                color.defaultValue = QStringLiteral("#ff0000");
                openvegas::plugin::EffectParameterSpec amount;
                amount.name = QStringLiteral("blendAmount");
                amount.defaultValue = QStringLiteral("100");
                parameterSpecs = {color, amount};
                parameterValues = {QStringLiteral("#ff0000"), QStringLiteral("100")};
            } else if (baseName.compare(QStringLiteral("ColorTemperature"),
                                        Qt::CaseInsensitive) == 0) {
                openvegas::plugin::EffectParameterSpec temperature;
                temperature.name = QStringLiteral("temperatureShift");
                temperature.defaultValue = QStringLiteral("1500");
                parameterSpecs = {temperature};
                parameterValues = {QStringLiteral("1500")};
            } else if (baseName.compare(QStringLiteral("CrushBlacksWhites"),
                                        Qt::CaseInsensitive) == 0) {
                openvegas::plugin::EffectParameterSpec black;
                black.name = QStringLiteral("inputBlack");
                black.defaultValue = QStringLiteral("0");
                openvegas::plugin::EffectParameterSpec white;
                white.name = QStringLiteral("inputWhite");
                white.defaultValue = QStringLiteral("1");
                parameterSpecs = {black, white};
                parameterValues = {QStringLiteral("0.2"), QStringLiteral("0.8")};
            } else if (baseName.compare(QStringLiteral("FindEdges"),
                                 Qt::CaseInsensitive) == 0) {
                openvegas::plugin::EffectParameterSpec inverted;
                inverted.name = QStringLiteral("isInverted");
                inverted.defaultValue = QStringLiteral("false");
                parameterSpecs = {inverted};
                parameterValues = {QStringLiteral("false")};
            } else if (baseName.compare(
                    QStringLiteral("Threshold"), Qt::CaseInsensitive) == 0) {
                openvegas::plugin::EffectParameterSpec threshold;
                threshold.name = QStringLiteral("threshold");
                threshold.defaultValue = QStringLiteral("5");
                openvegas::plugin::EffectParameterSpec color1;
                color1.name = QStringLiteral("color1");
                color1.defaultValue = QStringLiteral("#ff0000");
                openvegas::plugin::EffectParameterSpec color2;
                color2.name = QStringLiteral("color2");
                color2.defaultValue = QStringLiteral("#0000ff");
                openvegas::plugin::EffectParameterSpec source;
                source.name = QStringLiteral("source");
                source.defaultValue = QStringLiteral("Lightness");
                source.choices = {QStringLiteral("Red"), QStringLiteral("Green"),
                                  QStringLiteral("Blue"), QStringLiteral("Luminance"),
                                  QStringLiteral("Lightness"), QStringLiteral("Average")};
                parameterSpecs = {threshold, color1, color2, source};
                parameterValues = {QStringLiteral("5"), QStringLiteral("#ff0000"),
                                   QStringLiteral("#0000ff"),
                                   QStringLiteral("Lightness")};
            }
            if (parameterSpecs.isEmpty()) {
                const auto metadata = openvegas::plugin::loadNativePluginMetadata(
                    file, dependencyDir);
                parameterSpecs = metadata.parameters;
                for (const auto& parameter : parameterSpecs) {
                    parameterValues.append(parameter.defaultValue);
                }
                if (baseName.compare(QStringLiteral("AlphaBrightnessContrast"),
                                     Qt::CaseInsensitive) == 0
                    && parameterValues.size() >= 2) {
                    parameterValues[0] = QStringLiteral("-100");
                    parameterValues[1] = QStringLiteral("0");
                } else if (baseName.compare(QStringLiteral("Exposure"),
                                            Qt::CaseInsensitive) == 0
                           && parameterValues.size() >= 3) {
                    parameterValues[0] = QStringLiteral("1");
                } else {
                    for (int i = 0; i < parameterSpecs.size(); ++i) {
                        const auto& parameter = parameterSpecs.at(i);
                        if (!parameter.choices.isEmpty()) {
                            const int current = parameter.choices.indexOf(parameterValues.at(i));
                            parameterValues[i] = parameter.choices.at(
                                (qMax(current, 0) + 1) % parameter.choices.size());
                            break;
                        }
                        if (parameter.type.compare(QStringLiteral("bool"),
                                                   Qt::CaseInsensitive) == 0) {
                            parameterValues[i] = parameterValues.at(i) == QLatin1String("true")
                                                     ? QStringLiteral("false")
                                                     : QStringLiteral("true");
                            break;
                        }
                        if (parameter.type.compare(QStringLiteral("color"),
                                                   Qt::CaseInsensitive) == 0) {
                            parameterValues[i] = parameterValues.at(i).compare(
                                                     QStringLiteral("#ff0000"),
                                                     Qt::CaseInsensitive) == 0
                                                     ? QStringLiteral("#0000ff")
                                                     : QStringLiteral("#ff0000");
                            break;
                        }
                        if (parameter.type.compare(QStringLiteral("point2d"),
                                                   Qt::CaseInsensitive) == 0
                            || parameter.type.compare(QStringLiteral("point3d"),
                                                      Qt::CaseInsensitive) == 0
                            || parameter.type.compare(QStringLiteral("orientation"),
                                                      Qt::CaseInsensitive) == 0) {
                            QStringList components = parameterValues.at(i).split(QLatin1Char(','));
                            if (components.isEmpty()) components.append(QStringLiteral("0"));
                            components[0] = QString::number(components.at(0).toDouble() + 25.0,
                                                            'g', 8);
                            parameterValues[i] = components.join(QLatin1Char(','));
                            break;
                        }
                        if (parameter.type.compare(QStringLiteral("angle"),
                                                   Qt::CaseInsensitive) == 0) {
                            const double current = parameterValues.at(i).toDouble();
                            parameterValues[i] = QString::number(current + 25.0, 'g', 8);
                            break;
                        }
                        if (parameter.type.compare(QStringLiteral("double"),
                                                   Qt::CaseInsensitive) == 0
                            || parameter.type.compare(QStringLiteral("int"),
                                                      Qt::CaseInsensitive) == 0) {
                            const double current = parameterValues.at(i).toDouble();
                            const double candidate = qAbs(current - parameter.maximum) > 0.000001
                                                         ? parameter.maximum : parameter.minimum;
                            parameterValues[i] = QString::number(candidate, 'g', 8);
                            break;
                        }
                    }
                }
            }
            const auto setProbeParameter = [&](const QString& key,
                                               const QString& value) {
                for (int i = 0; i < parameterSpecs.size(); ++i) {
                    if (parameterSpecs.at(i).name.compare(key, Qt::CaseInsensitive) == 0) {
                        parameterValues[i] = value;
                        return;
                    }
                }
            };
            if (baseName.compare(QStringLiteral("AutoLightFlares"),
                                 Qt::CaseInsensitive) == 0) {
                setProbeParameter(QStringLiteral("threshold"), QStringLiteral("0"));
                setProbeParameter(QStringLiteral("intensity"), QStringLiteral("5"));
            } else if (baseName.compare(QStringLiteral("ColorAdjustment"),
                                        Qt::CaseInsensitive) == 0) {
                setProbeParameter(QStringLiteral("colorPicker"),
                                  QStringLiteral("#00ff00"));
                setProbeParameter(QStringLiteral("softness"), QStringLiteral("100"));
                setProbeParameter(QStringLiteral("correctionHue"), QStringLiteral("180"));
                setProbeParameter(QStringLiteral("correctionStrength"),
                                  QStringLiteral("100"));
            } else if (baseName.compare(QStringLiteral("Colorama"),
                                        Qt::CaseInsensitive) == 0) {
                setProbeParameter(QStringLiteral("phaseShift"), QStringLiteral("90"));
            } else if (baseName.compare(QStringLiteral("FisheyeWarp2"),
                                        Qt::CaseInsensitive) == 0) {
                setProbeParameter(QStringLiteral("preset"), QStringLiteral("-"));
                setProbeParameter(QStringLiteral("fov"), QStringLiteral("120"));
                setProbeParameter(QStringLiteral("center"), QStringLiteral("16,16"));
            } else if (baseName.compare(QStringLiteral("Sphere"),
                                        Qt::CaseInsensitive) == 0) {
                setProbeParameter(QStringLiteral("radius"), QStringLiteral("14"));
                setProbeParameter(QStringLiteral("transformationPosition"),
                                  QStringLiteral("16,16"));
            } else if (baseName.compare(QStringLiteral("ScanLines"),
                                        Qt::CaseInsensitive) == 0) {
                setProbeParameter(QStringLiteral("frequency"), QStringLiteral("8"));
                setProbeParameter(QStringLiteral("sharpness"), QStringLiteral("1"));
                setProbeParameter(QStringLiteral("color1"), QStringLiteral("#ff0000"));
                setProbeParameter(QStringLiteral("color2"), QStringLiteral("#0000ff"));
            } else if (baseName.compare(QStringLiteral("Solarize"),
                                        Qt::CaseInsensitive) == 0) {
                setProbeParameter(QStringLiteral("threshold"), QStringLiteral("0.3"));
                setProbeParameter(QStringLiteral("saturation"), QStringLiteral("2"));
                setProbeParameter(QStringLiteral("invert"), QStringLiteral("true"));
            } else if (baseName.compare(QStringLiteral("Tiles"),
                                        Qt::CaseInsensitive) == 0) {
                setProbeParameter(QStringLiteral("scale"), QStringLiteral("15"));
                setProbeParameter(QStringLiteral("center"), QStringLiteral("16,16"));
            } else if (baseName.compare(QStringLiteral("Wireframe"),
                                        Qt::CaseInsensitive) == 0) {
                setProbeParameter(QStringLiteral("wireframeType"),
                                  QStringLiteral("Grid"));
                setProbeParameter(QStringLiteral("lineWidth"), QStringLiteral("2"));
                setProbeParameter(QStringLiteral("gridSize"), QStringLiteral("4"));
                setProbeParameter(QStringLiteral("lineColor"),
                                  QStringLiteral("#ff0000"));
                setProbeParameter(QStringLiteral("lineOpacity"), QStringLiteral("1"));
            } else if (baseName.compare(QStringLiteral("ToneColoring"),
                                        Qt::CaseInsensitive) == 0) {
                setProbeParameter(QStringLiteral("globalHue"), QStringLiteral("180"));
                setProbeParameter(QStringLiteral("globalAdjustment"),
                                  QStringLiteral("100"));
            } else if (baseName.compare(QStringLiteral("Vignette"),
                                        Qt::CaseInsensitive) == 0) {
                setProbeParameter(QStringLiteral("centerPoint"), QStringLiteral("0,0"));
                setProbeParameter(QStringLiteral("boundingBoxWidth"), QStringLiteral("16"));
                setProbeParameter(QStringLiteral("boundingBoxHeight"), QStringLiteral("16"));
                setProbeParameter(QStringLiteral("opacity"), QStringLiteral("1"));
                setProbeParameter(QStringLiteral("vignetteColor"), QStringLiteral("#000000"));
            }
            if (qEnvironmentVariableIsSet("OPENVEGAS_HFPL_RENDER_DEFAULT_PARAMETERS")) {
                parameterValues.clear();
                for (const auto& parameter : parameterSpecs) {
                    parameterValues.append(parameter.defaultValue);
                }
            }
            if (qEnvironmentVariableIsSet("OPENVEGAS_HFPL_RENDER_EMPTY_PARAMETERS")) {
                parameterSpecs.clear();
                parameterValues.clear();
            }
            openvegas::plugin::clearNativeEffectModules();
            if (transitionApi) {
                openvegas::plugin::registerNativeVideoTransitionModule(
                    id, file, dependencyDir, true, parameterSpecs);
            } else {
                openvegas::plugin::registerNativeEffectModule(
                    id, file, dependencyDir, true, parameterSpecs);
            }
            const QStringList geometricProbeNames {
                QStringLiteral("360Blur"),
                QStringLiteral("360ChannelBlur"),
                QStringLiteral("360FisheyeConverter"),
                QStringLiteral("360Glow"),
                QStringLiteral("360GlowDarks"),
                QStringLiteral("360Bulge"),
                QStringLiteral("360Magnify"),
                QStringLiteral("360Twirl"),
                QStringLiteral("360Unsharpen"),
                QStringLiteral("3DExtrusion"),
                QStringLiteral("ActionCamCrop"),
                QStringLiteral("AnamorphicLensFlare"),
                QStringLiteral("AngleBlur"),
                QStringLiteral("AutoLightFlares"),
                QStringLiteral("BezierWarp"),
                QStringLiteral("BilateralBlur"),
                QStringLiteral("BlockDisplacement"),
                QStringLiteral("BoxBlur"),
                QStringLiteral("Bulge"),
                QStringLiteral("ChannelBlur"),
                QStringLiteral("ChromaBlur"),
                QStringLiteral("ChromaticAberration"),
                QStringLiteral("ColorAdjustment"),
                QStringLiteral("Colorama"),
                QStringLiteral("ColorMap"),
                QStringLiteral("CenterWipe"),
                QStringLiteral("Clone"),
                QStringLiteral("CloneStamp"),
                QStringLiteral("Crop"),
                QStringLiteral("Dehaze"),
                QStringLiteral("Deinterlace"),
                QStringLiteral("Denoise"),
                QStringLiteral("DifferenceKey"),
                QStringLiteral("DepthMask"),
                QStringLiteral("DepthMatte"),
                QStringLiteral("Diffuse"),
                QStringLiteral("DisplacementMap"),
                QStringLiteral("EdgeDistortion"),
                QStringLiteral("ErodeWhite"),
                QStringLiteral("FisheyeWarp"),
                QStringLiteral("FisheyeWarp2"),
                QStringLiteral("FlyEye"),
                QStringLiteral("FluidDistortion"),
                QStringLiteral("FrameBlendedRetiming"),
                QStringLiteral("FreezeFrame"),
                QStringLiteral("Glow"),
                QStringLiteral("GoProLensReframe"),
                QStringLiteral("GradingTransfer"),
                QStringLiteral("GrainRemoval"),
                QStringLiteral("HalfToneColor"),
                QStringLiteral("Grid"),
                QStringLiteral("HeatDistortion"),
                QStringLiteral("Histogram"),
                QStringLiteral("Hotspots"),
                QStringLiteral("InnerGlow"),
                QStringLiteral("HighpassSharpen"),
                QStringLiteral("LensBlur"),
                QStringLiteral("LensDistort"),
                QStringLiteral("LensDirt"),
                QStringLiteral("LeaveColor"),
                QStringLiteral("LuminanceKey"),
                QStringLiteral("LUT"),
                QStringLiteral("LinearWipe"),
                QStringLiteral("LightFlares"),
                QStringLiteral("LightLeak"),
                QStringLiteral("LightRays"),
                QStringLiteral("LightWrap"),
                QStringLiteral("Magnify"),
                QStringLiteral("Mosaic"),
                QStringLiteral("MotionBlur"),
                QStringLiteral("MotionLock"),
                QStringLiteral("MatteCleaner"),
                QStringLiteral("OuterGlow"),
                QStringLiteral("OilPainting"),
                QStringLiteral("ProSkinRetouch"),
                QStringLiteral("Photorama"),
                QStringLiteral("Parallax"),
                QStringLiteral("PageCurl"),
                QStringLiteral("Pinwheel"),
                QStringLiteral("PixelSort"),
                QStringLiteral("PerspectiveWarp"),
                QStringLiteral("PiP"),
                QStringLiteral("PondRipple"),
                QStringLiteral("PolarWarp"),
                QStringLiteral("Projector"),
                QStringLiteral("QuadWarp"),
                QStringLiteral("RadialBlur"),
                QStringLiteral("RemoveFringe"),
                QStringLiteral("Reflection"),
                QStringLiteral("RadialReveal"),
                QStringLiteral("RainOnGlass"),
                QStringLiteral("RollingShutter"),
                QStringLiteral("RoughEdges"),
                QStringLiteral("Reverse"),
                QStringLiteral("SetMatte"),
                QStringLiteral("Shake"),
                QStringLiteral("Sharpen"),
                QStringLiteral("ScanLines"),
                QStringLiteral("Solarize"),
                QStringLiteral("SmokeDistortion"),
                QStringLiteral("SplitScreenMasking"),
                QStringLiteral("Sphere"),
                QStringLiteral("Tiles"),
                QStringLiteral("ToneColoring"),
                QStringLiteral("Stroke"),
                QStringLiteral("SurfaceRayTrace"),
                QStringLiteral("Twirl"),
                QStringLiteral("Unsharpen"),
                QStringLiteral("VectorStroke"),
                QStringLiteral("Vignette"),
                QStringLiteral("VignetteExposure"),
                QStringLiteral("VerticalVideo"),
                QStringLiteral("WarpVortex"),
                QStringLiteral("Waves"),
                QStringLiteral("Wireframe"),
                QStringLiteral("WireRemoval"),
                QStringLiteral("WitnessProtection"),
                QStringLiteral("ZoomBlur")
            };
            const bool patternedProbe = geometricProbeNames.contains(
                baseName, Qt::CaseInsensitive);
            const int testDimension =
                baseName.compare(QStringLiteral("ChromaKey"), Qt::CaseInsensitive) == 0
                    ? 64 : patternedProbe ? 32 : 2;
            const auto fillProbeImage = [patternedProbe](QImage& target) {
                if (!patternedProbe) {
                    target.fill(QColor(10, 20, 30, 255));
                    return;
                }
                for (int y = 0; y < target.height(); ++y) {
                    for (int x = 0; x < target.width(); ++x) {
                        const bool checker = ((x / 4) + (y / 4)) % 2 != 0;
                        target.setPixelColor(
                            x, y,
                            QColor((x * 23 + (checker ? 97 : 11)) % 256,
                                   (y * 31 + (checker ? 17 : 149)) % 256,
                                   ((x + y) * 13 + (checker ? 211 : 43)) % 256,
                                   255));
                    }
                }
            };
            const auto changedPixelCount = [](const QImage& before,
                                               const QImage& after) {
                if (before.size() != after.size()) return -1;
                int changed = 0;
                for (int y = 0; y < before.height(); ++y) {
                    for (int x = 0; x < before.width(); ++x) {
                        if (before.pixel(x, y) != after.pixel(x, y)) ++changed;
                    }
                }
                return changed;
            };
            QImage image(testDimension, testDimension, QImage::Format_RGBA8888);
            fillProbeImage(image);
            const QImage sourceImage = image;
            QImage transitionTo(testDimension, testDimension, QImage::Format_RGBA8888);
            transitionTo.fill(QColor(210, 80, 40, 255));
            err << "[hfpl] renderer API " << QFileInfo(file).fileName() << "...\n";
            err.flush();
            bool rendered = transitionApi
                                ? openvegas::plugin::applyNativeVideoTransition(
                                      image, sourceImage, transitionTo, 0.5f, id,
                                      parameterValues)
                                : openvegas::plugin::applyNativeEffectToImage(
                                      image, id, parameterValues);
            err << "[hfpl] main render result=" << (rendered ? 1 : 0) << "\n";
            err.flush();
            const QColor pixel = image.pixelColor(0, 0);
            QString pixelResult = QStringLiteral("%1,%2,%3,%4")
                                      .arg(pixel.red()).arg(pixel.green())
                                      .arg(pixel.blue()).arg(pixel.alpha());
            pixelResult += QStringLiteral(";changed=%1")
                               .arg(changedPixelCount(sourceImage, image));
            // A second frame on the same persistent runtime proves that the
            // key-based values are read per frame rather than captured during
            // Notify(8). With Lightness, the source pixel crosses 5% but not
            // 95%, so both RGB output callbacks are covered.
            if (baseName.compare(QStringLiteral("Threshold"),
                                 Qt::CaseInsensitive) == 0) {
                QImage second(2, 2, QImage::Format_RGBA8888);
                second.fill(QColor(10, 20, 30, 255));
                parameterValues[0] = QStringLiteral("95");
                const bool secondRendered =
                    openvegas::plugin::applyNativeEffectToImage(second, id,
                                                                 parameterValues);
                rendered = rendered && secondRendered;
                const QColor secondPixel = second.pixelColor(0, 0);
                pixelResult += QStringLiteral(";%1,%2,%3,%4")
                                   .arg(secondPixel.red()).arg(secondPixel.green())
                                   .arg(secondPixel.blue()).arg(secondPixel.alpha());
            } else if (baseName.compare(QStringLiteral("BrightnessContrast"),
                                        Qt::CaseInsensitive) == 0) {
                QImage second(2, 2, QImage::Format_RGBA8888);
                second.fill(QColor(10, 20, 30, 255));
                parameterValues = {QStringLiteral("0"), QStringLiteral("100")};
                const bool secondRendered =
                    openvegas::plugin::applyNativeEffectToImage(second, id,
                                                                 parameterValues);
                rendered = rendered && secondRendered;
                const QColor secondPixel = second.pixelColor(0, 0);
                pixelResult += QStringLiteral(";%1,%2,%3,%4")
                                   .arg(secondPixel.red()).arg(secondPixel.green())
                                   .arg(secondPixel.blue()).arg(secondPixel.alpha());
            } else if (baseName.compare(QStringLiteral("Gamma"),
                                        Qt::CaseInsensitive) == 0) {
                QImage second(2, 2, QImage::Format_RGBA8888);
                second.fill(QColor(10, 20, 30, 255));
                parameterValues = {QStringLiteral("1"), QStringLiteral("1"),
                                   QStringLiteral("1")};
                const bool secondRendered =
                    openvegas::plugin::applyNativeEffectToImage(second, id,
                                                                 parameterValues);
                rendered = rendered && secondRendered;
                const QColor secondPixel = second.pixelColor(0, 0);
                pixelResult += QStringLiteral(";%1,%2,%3,%4")
                                   .arg(secondPixel.red()).arg(secondPixel.green())
                                   .arg(secondPixel.blue()).arg(secondPixel.alpha());
            } else if (baseName.compare(QStringLiteral("Fill"),
                                        Qt::CaseInsensitive) == 0) {
                QImage second(2, 2, QImage::Format_RGBA8888);
                second.fill(QColor(10, 20, 30, 255));
                parameterValues = {QStringLiteral("#0000ff"), QStringLiteral("50")};
                const bool secondRendered =
                    openvegas::plugin::applyNativeEffectToImage(second, id,
                                                                 parameterValues);
                rendered = rendered && secondRendered;
                const QColor secondPixel = second.pixelColor(0, 0);
                pixelResult += QStringLiteral(";%1,%2,%3,%4")
                                   .arg(secondPixel.red()).arg(secondPixel.green())
                                   .arg(secondPixel.blue()).arg(secondPixel.alpha());
            } else if (baseName.compare(QStringLiteral("ColorTemperature"),
                                        Qt::CaseInsensitive) == 0) {
                QImage second(2, 2, QImage::Format_RGBA8888);
                second.fill(QColor(10, 20, 30, 255));
                parameterValues = {QStringLiteral("13500")};
                const bool secondRendered =
                    openvegas::plugin::applyNativeEffectToImage(second, id,
                                                                 parameterValues);
                rendered = rendered && secondRendered;
                const QColor secondPixel = second.pixelColor(0, 0);
                pixelResult += QStringLiteral(";%1,%2,%3,%4")
                                   .arg(secondPixel.red()).arg(secondPixel.green())
                                   .arg(secondPixel.blue()).arg(secondPixel.alpha());
            } else if (baseName.compare(QStringLiteral("CrushBlacksWhites"),
                                        Qt::CaseInsensitive) == 0) {
                QImage second(2, 2, QImage::Format_RGBA8888);
                second.fill(QColor(10, 20, 30, 255));
                parameterValues = {QStringLiteral("0"), QStringLiteral("1")};
                const bool secondRendered =
                    openvegas::plugin::applyNativeEffectToImage(second, id,
                                                                 parameterValues);
                rendered = rendered && secondRendered;
                const QColor secondPixel = second.pixelColor(0, 0);
                pixelResult += QStringLiteral(";%1,%2,%3,%4")
                                   .arg(secondPixel.red()).arg(secondPixel.green())
                                   .arg(secondPixel.blue()).arg(secondPixel.alpha());
            } else if (baseName.compare(QStringLiteral("FindEdges"),
                                        Qt::CaseInsensitive) == 0) {
                QImage second(2, 2, QImage::Format_RGBA8888);
                second.fill(QColor(10, 20, 30, 255));
                parameterValues[0] = QStringLiteral("true");
                const bool secondRendered =
                    openvegas::plugin::applyNativeEffectToImage(second, id,
                                                                 parameterValues);
                rendered = rendered && secondRendered;
                const QColor secondPixel = second.pixelColor(0, 0);
                pixelResult += QStringLiteral(";%1,%2,%3,%4")
                                   .arg(secondPixel.red()).arg(secondPixel.green())
                                   .arg(secondPixel.blue()).arg(secondPixel.alpha());
            }

            // Exercise the same path from a persistent render worker. The
            // main event loop remains live while the worker asks it to create
            // the QOffscreenSurface, matching RenderManager's thread model.
            QImage threaded(testDimension, testDimension, QImage::Format_RGBA8888);
            fillProbeImage(threaded);
            bool threadedRendered = false;
            QEventLoop waitForWorker;
            QThread* renderThread = QThread::create([&]() {
                std::fprintf(stderr, "[hfpl] worker render entry verified=%d\n",
                             (transitionApi
                                  ? openvegas::plugin::nativeVideoTransitionRenderingVerified(id)
                                  : openvegas::plugin::nativeEffectFrameRenderingVerified(id))
                                 ? 1
                                 : 0);
                std::fflush(stderr);
                threadedRendered = transitionApi
                                       ? openvegas::plugin::applyNativeVideoTransition(
                                             threaded, sourceImage, transitionTo, 0.5f,
                                             id, parameterValues)
                                       : openvegas::plugin::applyNativeEffectToImage(
                                             threaded, id, parameterValues);
                std::fprintf(stderr, "[hfpl] worker render result=%d\n",
                             threadedRendered ? 1 : 0);
                std::fflush(stderr);
                openvegas::plugin::releaseNativeEffectThreadRenderer();
            });
            QObject::connect(renderThread, &QThread::finished,
                             &waitForWorker, &QEventLoop::quit);
            renderThread->start();
            waitForWorker.exec();
            renderThread->wait();
            delete renderThread;
            rendered = rendered && threadedRendered;
            const QColor threadedPixel = threaded.pixelColor(0, 0);
            pixelResult += QStringLiteral(";thread=%1,%2,%3,%4")
                               .arg(threadedPixel.red()).arg(threadedPixel.green())
                               .arg(threadedPixel.blue()).arg(threadedPixel.alpha());
            pixelResult += QStringLiteral(";threadChanged=%1")
                               .arg(changedPixelCount(sourceImage, threaded));
            err << "[hfpl] renderer API result=" << (rendered ? 1 : 0) << "\n";
            err.flush();
            out << QDir::toNativeSeparators(file) << '\t' << (rendered ? 1 : 0)
                << '\t' << pixelResult << '\n';
            if (!rendered) {
                ++failures;
            }
        }
        out.flush();
        openvegas::plugin::releaseNativeEffectThreadRenderer();
        return failures == 0 ? 0 : 1;
    }

    QOpenGLContext glContext;
    QOffscreenSurface glSurface;
    QOpenGLDebugLogger glDebugLogger;
    if (!messages.isEmpty()) {
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(4, 1);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setOption(QSurfaceFormat::DebugContext);
        glContext.setFormat(format);
        glContext.create();
        glSurface.setFormat(glContext.format());
        glSurface.create();
        if (!glContext.isValid() || !glSurface.isValid()
            || !glContext.makeCurrent(&glSurface)) {
            err << "Unable to create the OpenGL 4.1 offscreen context required by GPU plugins\n";
            return 3;
        }
        if (glDebugLogger.initialize()) {
            QObject::connect(
                &glDebugLogger, &QOpenGLDebugLogger::messageLogged,
                [](const QOpenGLDebugMessage& message) {
                    const QByteArray text = message.message().toUtf8();
                    std::fprintf(stderr, "[hfpl][gl-debug] id=%u severity=%d %s\n",
                                 message.id(), int(message.severity()), text.constData());
                    std::fflush(stderr);
                });
            glDebugLogger.startLogging(QOpenGLDebugLogger::SynchronousLogging);
            glDebugLogger.enableMessages();
        }
    }

    out << "file\tloaded\tloadResult\tunloadResult\tfault\tinstructionRva"
           "\taccessAddress\tmessages\tpixel\tglError\tdrawFbo\terror\n";
    int failures = 0;
    for (const QString& file : files) {
        const QString dependencyDir = args.size() == 3
                                          ? QFileInfo(args.at(2)).absoluteFilePath()
                                          : inferredDependencyDirectory(file);
        openvegas::plugin::NativePluginRuntime runtime;
        openvegas::plugin::NativePluginLifecycleProbe result;
        if (metadataOnly) {
            const auto metadata = openvegas::plugin::loadNativePluginMetadata(file,
                                                                               dependencyDir);
            result.loaded = metadata.lifecycleCompatible;
            result.loadResult = metadata.notifyLoadResult;
            result.unloadResult = metadata.notifyUnloadResult;
            result.faultCode = metadata.notifyFaultCode;
            result.error = metadata.lifecycleError.isEmpty()
                               ? metadata.metadataError : metadata.lifecycleError;
        } else {
            result.loaded = runtime.load(file, dependencyDir);
            result.loadResult = runtime.loadResult();
            result.faultCode = runtime.lastFaultCode();
            result.faultInstructionRva = runtime.lastFaultInstructionRva();
            result.faultAccessAddress = runtime.lastFaultAccessAddress();
            result.error = runtime.errorString();
        }
        QStringList messageResults;
        QString renderedPixel;
        QString glError;
        QString drawFramebuffer;
        if (result.loaded && !messages.isEmpty()) {
            // The common 2D message-10 path reads its per-frame/value block
            // through tagBiffAPI+0x20. A zeroed block represents default
            // parameter values and is sufficient to validate that ABI path.
            QByteArray frameBlock(0x400, '\0');
            QByteArray inputTextureBlock(0x80, '\0');
            QByteArray outputTextureBlock(0x80, '\0');
            void* inputTexturePointer = inputTextureBlock.data();
            std::memcpy(frameBlock.data() + 0x10, &inputTexturePointer,
                        sizeof(inputTexturePointer));
            void* outputTexturePointer = outputTextureBlock.data();
            std::memcpy(frameBlock.data() + 0x18, &outputTexturePointer,
                        sizeof(outputTexturePointer));
            // tagBiffTexture +0x04 is the texture target and +0x08 is the
            // internal/pixel format (confirmed by Tannen::GetSourceTexture).
            const quint32 textureTarget = GL_TEXTURE_2D;
            const quint32 texturePixelFormat = GL_RGBA;
            for (QByteArray* textureBlock : {&inputTextureBlock, &outputTextureBlock}) {
                std::memcpy(textureBlock->data() + 0x04, &textureTarget,
                            sizeof(textureTarget));
                std::memcpy(textureBlock->data() + 0x08, &texturePixelFormat,
                            sizeof(texturePixelFormat));
            }
            const float uvBounds[] = {0.0f, 0.0f, 1.0f, 1.0f};
            std::memcpy(inputTextureBlock.data() + 0x1c, uvBounds, sizeof(uvBounds));
            bool dimensionOk = false;
            const int configuredDimension =
                qEnvironmentVariableIntValue("HFPL_PROBE_DIMENSION", &dimensionOk);
            const qint32 dimension = dimensionOk ? qMax(1, configuredDimension) : 1;
            g_probeFrameDimension = dimension;
            for (const int offset : {0x0c, 0x10, 0x14, 0x18}) {
                std::memcpy(inputTextureBlock.data() + offset, &dimension,
                            sizeof(dimension));
                std::memcpy(outputTextureBlock.data() + offset, &dimension,
                            sizeof(dimension));
            }
            std::memcpy(outputTextureBlock.data() + 0x1c, uvBounds, sizeof(uvBounds));
            const float textureScale = 1.0f;
            for (QByteArray* textureBlock : {&inputTextureBlock, &outputTextureBlock}) {
                std::memcpy(textureBlock->data() + 0x2c, &textureScale,
                            sizeof(textureScale));
                std::memcpy(textureBlock->data() + 0x30, &textureScale,
                            sizeof(textureScale));
            }
            std::memcpy(frameBlock.data() + 0x24, &textureScale,
                        sizeof(textureScale));
            std::memcpy(frameBlock.data() + 0x2c, &textureScale,
                        sizeof(textureScale));
            std::memcpy(frameBlock.data() + 0x30, &textureScale,
                        sizeof(textureScale));
            std::memcpy(frameBlock.data() + 0x60, &dimension, sizeof(dimension));
            std::memcpy(frameBlock.data() + 0x64, &dimension, sizeof(dimension));
            // The two qwords are pointers consumed by glUniformMatrix4fv, not
            // inline flags. The reference's pixel-space projection maps the
            // full frame rectangle over the whole clip rectangle.
            const std::array<float, 16> projection {
                2.0f / dimension, 0.0f,                  0.0f, 0.0f,
                0.0f,                  2.0f / dimension, 0.0f, 0.0f,
                0.0f, 0.0f, -1.0f, 0.0f,
               -1.0f,-1.0f,  0.0f, 1.0f
            };
            const std::array<float, 16> modelView {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
            const float* projectionPointer = projection.data();
            const float* modelViewPointer = modelView.data();
            // Tannen.dll Plugin2DEffect::Render RVA 0x333890 copies five
            // float* arguments into RenderContext +0x38..+0x58.
            const std::array<const float*, 5> transforms {
                modelViewPointer, projectionPointer, modelViewPointer,
                projectionPointer, modelViewPointer
            };
            std::memcpy(frameBlock.data() + 0x38, transforms.data(),
                        sizeof(transforms));
            void* framePointer = frameBlock.data();
            std::memcpy(runtime.apiBlock().data() + 0x20, &framePointer,
                        sizeof(framePointer));
            // CreateAPI normally stores AbstractPluginInstance::vfunc(+0x60)
            // here. Levels reads the render/sample index at +0x2c directly;
            // zero represents the ordinary current-frame render.
            QByteArray instanceRenderState(0x100, '\0');
            void* instanceRenderStatePointer = instanceRenderState.data();
            std::memcpy(runtime.apiBlock().data() + 0x10,
                        &instanceRenderStatePointer,
                        sizeof(instanceRenderStatePointer));
            // Tannen::CreateAPI copies the active renderer capability flag to
            // this byte; Notify(8) selects the matching GL shader path.
            runtime.apiBlock()[0x28] = 1;
            auto* gl = QOpenGLContext::currentContext()->extraFunctions();
            QOpenGLFramebufferObjectFormat outputFormat;
            outputFormat.setAttachment(QOpenGLFramebufferObject::NoAttachment);
            outputFormat.setTextureTarget(GL_TEXTURE_2D);
            outputFormat.setInternalTextureFormat(GL_RGBA32F);
            QOpenGLFramebufferObject output(QSize(dimension, dimension), outputFormat);
            output.bind();
            gl->glViewport(0, 0, dimension, dimension);
            gl->glDisable(GL_SCISSOR_TEST);
            gl->glDisable(GL_DEPTH_TEST);
            gl->glDisable(GL_CULL_FACE);
            gl->glDisable(GL_BLEND);
            gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            gl->glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
            gl->glClear(GL_COLOR_BUFFER_BIT);
            GLuint inputTexture = 0;
            gl->glGenTextures(1, &inputTexture);
            gl->glBindTexture(GL_TEXTURE_2D, inputTexture);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            QVector<float> sourcePixels(dimension * dimension * 4);
            for (int i = 0; i < dimension * dimension; ++i) {
                sourcePixels[i * 4] = 10.0f / 255.0f;
                sourcePixels[i * 4 + 1] = 20.0f / 255.0f;
                sourcePixels[i * 4 + 2] = 30.0f / 255.0f;
                sourcePixels[i * 4 + 3] = 1.0f;
            }
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, dimension, dimension,
                             0, GL_RGBA, GL_FLOAT, sourcePixels.constData());
            std::memcpy(inputTextureBlock.data(), &inputTexture, sizeof(inputTexture));
            const GLuint outputTexture = output.texture();
            std::memcpy(outputTextureBlock.data(), &outputTexture, sizeof(outputTexture));

            bool firstOffsetOk = false;
            bool lastOffsetOk = false;
            const int diagnosticFirstOffset = qEnvironmentVariableIntValue(
                "OPENVEGAS_HFPL_DIAGNOSTIC_FIRST", &firstOffsetOk);
            const int diagnosticLastOffset = qEnvironmentVariableIntValue(
                "OPENVEGAS_HFPL_DIAGNOSTIC_LAST", &lastOffsetOk);
            const bool diagnosticLegacy =
                qEnvironmentVariable("OPENVEGAS_HFPL_DIAGNOSTIC_LEGACY",
                                     QStringLiteral("1")) != QLatin1String("0");
            openvegas::plugin::installNativePluginDiagnosticStubs(
                runtime.apiBlock(), firstOffsetOk ? diagnosticFirstOffset : 0x30,
                lastOffsetOk ? diagnosticLastOffset : -1, diagnosticLegacy);
            // Install concrete services after the catch-all diagnostics: these
            // slots have non-integer return ABIs or real OpenGL side effects.
            putService(runtime.apiBlock(), 0xb0,
                       reinterpret_cast<void*>(&defaultIntegerParameter));
            putService(runtime.apiBlock(), 0xb8,
                       reinterpret_cast<void*>(&defaultScalarParameter));
            putService(runtime.apiBlock(), 0xc0,
                       reinterpret_cast<void*>(&defaultBooleanParameter));
            putService(runtime.apiBlock(), 0xc8,
                       reinterpret_cast<void*>(&defaultIntegerParameter));
            putService(runtime.apiBlock(), 0xd0,
                       reinterpret_cast<void*>(&defaultPoint2dParameter));
            putService(runtime.apiBlock(), 0xd8,
                       reinterpret_cast<void*>(&defaultScalarParameter));
            putService(runtime.apiBlock(), 0xe0,
                       reinterpret_cast<void*>(&defaultColorParameter));
            putService(runtime.apiBlock(), 0xe8,
                       reinterpret_cast<void*>(&defaultLayerId));
            putService(runtime.apiBlock(), 0xf0,
                       reinterpret_cast<void*>(&defaultLayerInfo));
            putService(runtime.apiBlock(), 0x108,
                       reinterpret_cast<void*>(&defaultTimelineInfo));
            putService(runtime.apiBlock(), 0xf8,
                       reinterpret_cast<void*>(&probeScratchTexture));
            putService(runtime.apiBlock(), 0x120,
                       reinterpret_cast<void*>(&probeClearScratchTexture));
            putService(runtime.apiBlock(), 0x178,
                       reinterpret_cast<void*>(&defaultNotifyProgress));
            putService(runtime.apiBlock(), 0x1a0,
                       reinterpret_cast<void*>(&defaultSetBooleanParameter));
            putService(runtime.apiBlock(), 0x268,
                       reinterpret_cast<void*>(&defaultStringParameterLength));
            putService(runtime.apiBlock(), 0x270,
                       reinterpret_cast<void*>(&defaultStringParameter));
            putService(runtime.apiBlock(), 0x340,
                       reinterpret_cast<void*>(&defaultPoint3dParameter));
            putService(runtime.apiBlock(), 0x378,
                       reinterpret_cast<void*>(&defaultPoint3dParameter));
            putService(runtime.apiBlock(), 0x500,
                       reinterpret_cast<void*>(&createRenderbuffer));
            putService(runtime.apiBlock(), 0x508,
                       reinterpret_cast<void*>(&clearRenderbuffer));
            putService(runtime.apiBlock(), 0x4a8,
                       reinterpret_cast<void*>(&createVertexArray));
            putService(runtime.apiBlock(), 0x4b0,
                       reinterpret_cast<void*>(&createBuffer));
            putService(runtime.apiBlock(), 0x4b8,
                       reinterpret_cast<void*>(&deleteBuffer));
            g_bufferGlState.clear();
            g_scratchTrace.clear();
            g_renderbufferTrace.clear();
            QString liveNames;
            if (QFileInfo(file).completeBaseName().compare(QStringLiteral("Invert"),
                                                           Qt::CaseInsensitive) == 0) {
                liveNames = QStringLiteral("globals=%1|%2|%3")
                                .arg(msvcStringAtRva(runtime.moduleBase(), 0x5a060),
                                     msvcStringAtRva(runtime.moduleBase(), 0x5a080),
                                     msvcStringAtRva(runtime.moduleBase(), 0x5a0a0));
            }
            for (const int message : messages) {
                if (message == 8) {
                    runtime.enableRenderingCompatibility();
                }
                err << "[hfpl] " << QFileInfo(file).fileName()
                    << " Notify(" << message << ")...\n";
                err.flush();
                openvegas::plugin::resetNativePluginDiagnosticServiceOffset();
                const int messageResult = runtime.notify(message);
                err << "[hfpl] Notify(" << message << ") = " << messageResult
                    << "\n";
                const QString immediateTrace =
                    openvegas::plugin::nativePluginDiagnosticTrace();
                if (!immediateTrace.isEmpty()) {
                    err << "[hfpl] host trace: " << immediateTrace << "\n";
                }
                err.flush();
                messageResults.append(QStringLiteral("%1:%2:@0x%3:fault=0x%4")
                                          .arg(message)
                                          .arg(messageResult)
                                          .arg(openvegas::plugin::lastNativePluginDiagnosticServiceOffset(),
                                               0, 16)
                                          .arg(quint32(runtime.lastFaultCode()), 8, 16,
                                               QLatin1Char('0')));
                if (message == 8 && runtime.renderingCompatibilityRva() != 0) {
                    messageResults.last().append(
                        QStringLiteral(":compat=0x%1")
                            .arg(runtime.renderingCompatibilityRva(), 0, 16));
                }
                const QString trace = immediateTrace;
                if (!trace.isEmpty()) {
                    messageResults.last().append(QStringLiteral(":trace=") + trace);
                }
                if (runtime.lastFaultCode() != 0) {
                    result.faultCode = runtime.lastFaultCode();
                    result.faultInstructionRva = runtime.lastFaultInstructionRva();
                    result.faultAccessAddress = runtime.lastFaultAccessAddress();
                    result.error = runtime.errorString();
                    break;
                }
                if (message == 10) {
                    err << "[hfpl] scratch=" << g_scratchTrace.join(QLatin1Char('|'))
                        << " renderbuffers="
                        << g_renderbufferTrace.join(QLatin1Char('|'))
                        << " vbo=" << g_bufferGlState
                        << " preReadError=0x" << QString::number(gl->glGetError(), 16)
                        << "\n";
                    err.flush();
                    if (qEnvironmentVariableIsSet("HFPL_PROBE_SKIP_READBACK")) {
                        continue;
                    }
                    std::fputs("[hfpl] cleanup stage: restore context\n", stderr);
                    std::fflush(stderr);
                    glContext.makeCurrent(&glSurface);
                    std::fputs("[hfpl] cleanup stage: bind output\n", stderr);
                    std::fflush(stderr);
                    output.bind();
                    std::array<float, 4> pixel {};
                    GLint fbo = 0;
                    GLint program = 0;
                    GLint vao = 0;
                    GLint arrayBuffer = 0;
                    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                    gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
                    gl->glPixelStorei(GL_PACK_SKIP_ROWS, 0);
                    gl->glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
                    gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
                    gl->glGetIntegerv(GL_CURRENT_PROGRAM, &program);
                    gl->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
                    gl->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
                    gl->glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT,
                                     pixel.data());
                    std::fputs("[hfpl] cleanup stage: readback complete\n", stderr);
                    std::fflush(stderr);
                    const GLenum error = gl->glGetError();
                    const auto channel = [](float value) {
                        return qBound(0, qRound(value * 255.0f), 255);
                    };
                    renderedPixel = QStringLiteral("%1,%2,%3,%4")
                                        .arg(channel(pixel[0])).arg(channel(pixel[1]))
                                        .arg(channel(pixel[2])).arg(channel(pixel[3]));
                    glError = QStringLiteral("0x%1").arg(error, 0, 16);
                    drawFramebuffer = QStringLiteral("%1/program=%2/vao=%3/vbo=%4")
                                          .arg(fbo).arg(program).arg(vao)
                                          .arg(arrayBuffer);
                    if (!g_bufferGlState.isEmpty()) {
                        drawFramebuffer.append(QLatin1Char('/') + g_bufferGlState);
                    }
                    if (!liveNames.isEmpty()) {
                        drawFramebuffer.append(QLatin1Char('/') + liveNames);
                    }
                }
            }
            std::fputs("[hfpl] cleanup stage: finish queued GL work\n", stderr);
            std::fflush(stderr);
            gl->glFinish();
            std::fputs("[hfpl] cleanup stage: delete input texture\n", stderr);
            std::fflush(stderr);
            gl->glDeleteTextures(1, &inputTexture);
            std::fputs("[hfpl] cleanup stage: release output FBO\n", stderr);
            std::fflush(stderr);
            output.release();
            std::fputs("[hfpl] cleanup stage: GL resources released\n", stderr);
            std::fflush(stderr);
        }
        if (result.loaded && !metadataOnly) {
            std::fputs("[hfpl] cleanup stage: unload module\n", stderr);
            std::fflush(stderr);
            runtime.unload();
            std::fputs("[hfpl] cleanup stage: module unloaded\n", stderr);
            std::fflush(stderr);
            result.unloadResult = runtime.unloadResult();
        }
        out << QDir::toNativeSeparators(file) << '\t'
            << (result.loaded ? 1 : 0) << '\t'
            << result.loadResult << '\t'
            << result.unloadResult << '\t'
            << QStringLiteral("0x%1").arg(quint32(result.faultCode), 8, 16,
                                            QLatin1Char('0'))
            << '\t' << QStringLiteral("0x%1").arg(result.faultInstructionRva, 0, 16)
            << '\t' << QStringLiteral("0x%1").arg(result.faultAccessAddress, 0, 16)
            << '\t' << messageResults.join(QLatin1Char(','))
            << '\t' << renderedPixel
            << '\t' << glError
            << '\t' << drawFramebuffer
            << '\t' << result.error << '\n';
        if (!result.loaded || result.loadResult != 1 || result.unloadResult != 1
            || result.faultCode != 0) {
            ++failures;
        }
    }
    out.flush();
    if (!messages.isEmpty()) {
        glContext.doneCurrent();
    }
    return failures == 0 ? 0 : 1;
}
