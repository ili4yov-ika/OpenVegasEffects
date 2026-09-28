#include <QDir>
#include <QDirIterator>
#include <QColor>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
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
               "[message[,message...]|metadata|parameters|render|transition|audio|audio-transition|behavior|behavior-stack|behavior-subobject]\n";
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
    const bool rendererApi = args.size() == 4
                             && (args.at(3).compare(QStringLiteral("render"),
                                                   Qt::CaseInsensitive) == 0
                                 || transitionApi);
    const bool parametersOnly = args.size() == 4
                                && args.at(3).compare(QStringLiteral("parameters"),
                                                     Qt::CaseInsensitive) == 0;
    if (args.size() == 4 && !metadataOnly && !parametersOnly && !rendererApi
        && !audioApi && !audioTransitionApi && !behaviorApi && !behaviorGraphApi
        && !behaviorStackApi && !behaviorSubObjectApi) {
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
            constexpr int frameCount = 2048;
            QVector<qint16> from(frameCount * channels);
            QVector<qint16> to(frameCount * channels);
            for (int frame = 0; frame < frameCount; ++frame) {
                const qint16 left = qint16(((frame * 97) % 24000) - 12000);
                const qint16 right = qint16(9000 - ((frame * 53) % 18000));
                from[frame * channels] = left;
                from[frame * channels + 1] = right;
                to[frame * channels] = qint16(-left / 2);
                to[frame * channels + 1] = qint16(-right / 2);
            }
            QVector<qint16> rendered = from;
            bool ok = audioTransitionApi
                          ? openvegas::plugin::applyNativeAudioTransition(
                                rendered, from, to, channels, 0, frameCount, id,
                                parameterValues)
                          : openvegas::plugin::applyNativeAudioEffect(
                                rendered, channels, 48000, 0, id, parameterValues);
            if (ok && !audioTransitionApi) {
                rendered = from;
                ok = openvegas::plugin::applyNativeAudioEffect(
                    rendered, channels, 48000, frameCount, id, parameterValues);
            }
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
                                 : openvegas::plugin::applyNativeAudioEffect(
                                       threaded, channels, 48000, 0, id,
                                       parameterValues);
                if (threadedOk && !audioTransitionApi) {
                    threaded = from;
                    threadedOk = openvegas::plugin::applyNativeAudioEffect(
                        threaded, channels, 48000, frameCount, id,
                        parameterValues);
                }
                openvegas::plugin::releaseNativeEffectThreadRenderer();
            });
            renderThread->start();
            renderThread->wait();
            delete renderThread;
            ok = ok && threadedOk && mainChecksum == checksum(threaded);
            out << QDir::toNativeSeparators(file) << '\t' << (ok ? 1 : 0)
                << '\t' << QStringLiteral("0x%1;thread=0x%2;first=%3,%4;last=%5,%6")
                                  .arg(mainChecksum, 16, 16, QLatin1Char('0'))
                                  .arg(checksum(threaded), 16, 16, QLatin1Char('0'))
                                  .arg(rendered.value(0)).arg(rendered.value(1))
                                  .arg(rendered.value(rendered.size() - 2))
                                  .arg(rendered.value(rendered.size() - 1))
                << '\n';
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
