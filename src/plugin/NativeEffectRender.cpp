#include "plugin/NativeEffectRender.h"

#include <QGuiApplication>
#include <QColor>
#include <QHash>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSet>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QSurfaceFormat>
#include <QThread>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <limits>
#include <utility>

#include "plugin/NativePlugin.h"
#include "composition/Composition.h"

namespace openvegas {
namespace plugin {

namespace {

struct ModuleRecord
{
    QString filePath;
    QString dependencyDirectory;
    bool frameRenderingVerified = false;
    bool transitionRenderingVerified = false;
    bool audioRenderingVerified = false;
    bool audioTransitionRenderingVerified = false;
    bool behaviorRenderingVerified = false;
    QVector<EffectParameterSpec> parameters;
};

struct BehaviorStackEntry
{
    QString id;
    ModuleRecord module;
    QStringList values;
};

bool isVerifiedSimulationBehavior(const ModuleRecord& record)
{
    static const QSet<QString> names {
        QStringLiteral("acceleration"),
        QStringLiteral("drag"),
        QStringLiteral("gravity")
    };
    return record.behaviorRenderingVerified
           && names.contains(QFileInfo(record.filePath).baseName().toLower());
}

QMutex g_registryMutex;
QHash<QString, ModuleRecord> g_registry;
thread_local GLuint g_hostVertexArray = 0;

struct ScratchTextureRecord
{
    GLuint id = 0;
    qint32 width = 0;
    qint32 height = 0;
    quint32 internalFormat = 0;
    quint32 pixelFormat = 0;
    quint32 allocationQualifier = 0;
    bool reserved = false;
};

struct ScratchTexturePool
{
    QVector<ScratchTextureRecord> textures;

    void releaseAll()
    {
        for (ScratchTextureRecord& texture : textures) {
            texture.reserved = false;
        }
    }

    bool release(GLuint id)
    {
        for (ScratchTextureRecord& texture : textures) {
            if (texture.id == id) {
                texture.reserved = false;
                return true;
            }
        }
        return false;
    }

    void destroy(QOpenGLExtraFunctions* gl)
    {
        QVector<GLuint> ids;
        ids.reserve(textures.size());
        for (const ScratchTextureRecord& texture : std::as_const(textures)) {
            if (texture.id != 0) {
                ids.append(texture.id);
            }
        }
        if (!ids.isEmpty()) {
            gl->glDeleteTextures(ids.size(), ids.constData());
        }
        textures.clear();
    }
};

struct ScratchVboRecord
{
    GLuint id = 0;
    quint32 target = 0;
    qint32 byteCount = 0;
    quint32 usage = 0;
    bool reserved = false;
};

struct ScratchVboPool
{
    QVector<ScratchVboRecord> buffers;

    void releaseAll()
    {
        for (ScratchVboRecord& buffer : buffers) {
            buffer.reserved = false;
        }
    }

    bool release(GLuint id)
    {
        for (ScratchVboRecord& buffer : buffers) {
            if (buffer.id == id) {
                buffer.reserved = false;
                return true;
            }
        }
        return false;
    }

    void destroy(QOpenGLExtraFunctions* gl)
    {
        QVector<GLuint> ids;
        ids.reserve(buffers.size());
        for (const ScratchVboRecord& buffer : std::as_const(buffers)) {
            if (buffer.id != 0) {
                ids.append(buffer.id);
            }
        }
        if (!ids.isEmpty()) {
            gl->glDeleteBuffers(ids.size(), ids.constData());
        }
        buffers.clear();
    }
};

struct ScratchRenderbufferRecord
{
    GLuint id = 0;
    qint32 width = 0;
    qint32 height = 0;
    qint32 internalFormat = 0;
    qint32 precision = 0;
    bool reserved = false;
};

struct ScratchRenderbufferPool
{
    QVector<ScratchRenderbufferRecord> renderbuffers;

    void releaseAll()
    {
        for (ScratchRenderbufferRecord& buffer : renderbuffers) {
            buffer.reserved = false;
        }
    }

    bool release(GLuint id)
    {
        for (ScratchRenderbufferRecord& buffer : renderbuffers) {
            if (buffer.id == id) {
                buffer.reserved = false;
                return true;
            }
        }
        return false;
    }

    void destroy(QOpenGLExtraFunctions* gl)
    {
        QVector<GLuint> ids;
        ids.reserve(renderbuffers.size());
        for (const ScratchRenderbufferRecord& buffer : std::as_const(renderbuffers)) {
            if (buffer.id != 0) {
                ids.append(buffer.id);
            }
        }
        if (!ids.isEmpty()) {
            gl->glDeleteRenderbuffers(ids.size(), ids.constData());
        }
        renderbuffers.clear();
    }
};

struct RenderInvocation
{
    const ModuleRecord* module = nullptr;
    const QStringList* values = nullptr;
    ScratchTexturePool* scratchPool = nullptr;
    ScratchVboPool* scratchVboPool = nullptr;
    ScratchRenderbufferPool* scratchRenderbufferPool = nullptr;
    qint32 width = 0;
    qint32 height = 0;
    qint32 sampleRate = 48000;
    qint32 channels = 2;
    const qint16* audioSamples = nullptr;
    qint32 audioFrameCount = 0;
    qint64 audioStartSample = 0;
    qint32 timelineFrame = 0;
    qint32 layerDurationFrames = 1;
    double frameRate = 30.0;
    const composition::Composition* composition = nullptr;
    core::Identifier sourceLayerId;
    std::array<float, 16> preBehaviorTransformation {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    QHash<int, QString> valueOverrides;
};

thread_local RenderInvocation* g_renderInvocation = nullptr;

int parameterIndex(const char* key)
{
    if (!g_renderInvocation || !g_renderInvocation->module || !key) {
        return -1;
    }
    const auto& specs = g_renderInvocation->module->parameters;
    const QString name = QString::fromLatin1(key);
    for (int i = 0; i < specs.size(); ++i) {
        if (specs.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

QString parameterValue(const char* key)
{
    const int index = parameterIndex(key);
    if (index < 0) {
        return QString();
    }
    const auto overridden = g_renderInvocation->valueOverrides.constFind(index);
    if (overridden != g_renderInvocation->valueOverrides.constEnd()) {
        return overridden.value();
    }
    const QStringList& values = *g_renderInvocation->values;
    if (index < values.size() && !values.at(index).isEmpty()) {
        return values.at(index);
    }
    return g_renderInvocation->module->parameters.at(index).defaultValue;
}

float scalarParameter(quint64, const char* key, quint32)
{
    bool ok = false;
    const float value = parameterValue(key).toFloat(&ok);
    if (qEnvironmentVariableIsSet("OPENVEGAS_HFPL_RENDER_TRACE")) {
        std::fprintf(stderr, "[hfpl-render] scalar %s=%g ok=%d\n",
                     key ? key : "<null>", double(value), ok ? 1 : 0);
        std::fflush(stderr);
    }
    return ok ? value : 0.0f;
}

void colorParameter(quint64, const char* key, quint32,
                    float* red, float* green, float* blue)
{
    const QColor color(parameterValue(key));
    if (red) {
        *red = color.isValid() ? color.redF() : 0.0f;
    }
    if (green) {
        *green = color.isValid() ? color.greenF() : 0.0f;
    }
    if (blue) {
        *blue = color.isValid() ? color.blueF() : 0.0f;
    }
}

QVector<float> vectorParameter(const char* key, int componentCount)
{
    const QStringList parts = parameterValue(key).split(QLatin1Char(','));
    QVector<float> result(componentCount, 0.0f);
    for (int i = 0; i < componentCount && i < parts.size(); ++i) {
        bool ok = false;
        const float value = parts.at(i).trimmed().toFloat(&ok);
        if (ok) {
            result[i] = value;
        }
    }
    return result;
}

void point2dParameter(quint64, const char* key, quint32, float* x, float* y)
{
    const QVector<float> value = vectorParameter(key, 2);
    if (x) *x = value.at(0);
    if (y) *y = value.at(1);
}

void point3dParameter(quint64, const char* key, quint32,
                      float* x, float* y, float* z)
{
    const QVector<float> value = vectorParameter(key, 3);
    if (x) *x = value.at(0);
    if (y) *y = value.at(1);
    if (z) *z = value.at(2);
}

void layerIdParameter(quint64, const char* key, quint32, char* layerId)
{
    if (!layerId) {
        return;
    }
    std::memset(layerId, 0, 0x28);
    const QString selected = parameterValue(key).trimmed();
    const QByteArray value = selected.isEmpty()
        ? QByteArrayLiteral("00000000-0000-0000-0000-000000000000")
        : selected.toLatin1();
    std::memcpy(layerId, value.constData(), size_t(qMin(value.size(), 0x27)));
}

int layerInfo(quint64, const char*, quint32, void* info)
{
    // tagLayerInfo is 0x54 bytes. Tannen clears it before resolving the layer
    // and returns -5 when no project/layer is available. Native image renders
    // currently have no secondary layer graph, so expose that exact safe state.
    if (info) {
        std::memset(info, 0, 0x54);
    }
    return -5;
}

int audioLayerInfoV2(quint64, const char*, qint32, void* info)
{
    if (!info) return -10;
    // tagBiffLayerInfo V2 extends the original structure through +0xb4.
    // Audio processors only use the type/timing portion when resolving their
    // own source layer. A plain media layer with unity scale is sufficient.
    std::memset(info, 0, 0xf0);
    const qint32 mediaType = 0;
    const qint32 one = 1;
    const float unity = 1.0f;
    const qint32 duration = g_renderInvocation
                                ? qMax(1, g_renderInvocation->layerDurationFrames)
                                : 1;
    std::memcpy(static_cast<char*>(info) + 0x28, &mediaType, sizeof(mediaType));
    std::memcpy(static_cast<char*>(info) + 0x34, &duration, sizeof(duration));
    std::memcpy(static_cast<char*>(info) + 0x38, &one, sizeof(char));
    std::memcpy(static_cast<char*>(info) + 0x6c, &one, sizeof(one));
    std::memcpy(static_cast<char*>(info) + 0x70, &one, sizeof(one));
    std::memcpy(static_cast<char*>(info) + 0x88, &unity, sizeof(unity));
    std::memcpy(static_cast<char*>(info) + 0x9c, &unity, sizeof(unity));
    std::memcpy(static_cast<char*>(info) + 0xb0, &unity, sizeof(unity));
    return 0;
}

int preBehaviorTransformation(quint64, const char*, qint32, float* matrix)
{
    if (!matrix || !g_renderInvocation) return -5;
    std::memcpy(matrix, g_renderInvocation->preBehaviorTransformation.data(),
                sizeof(g_renderInvocation->preBehaviorTransformation));
    return 0;
}

int createBehaviorFloatSlider(quint64, const wchar_t*, const wchar_t*,
                              const char*, qint32, float, float, float, qint32)
{
    // Runtime instances repeat a small part of their UI registration during
    // Notify(3). The controls and their values have already been captured by
    // loadNativePluginMetadata(), so acknowledging the registration is enough.
    return 0;
}

int createBehaviorLayerPicker(quint64, const wchar_t*, const char*, qint32)
{
    return 0;
}

void clearBehaviorPersistentMessage(quint64)
{
}

int originTransformation(quint64, const char*, qint32, float* matrix)
{
    if (!matrix || !g_renderInvocation) return -5;
    // Tannen exposes a column-major 4x4 matrix here. The renderer already
    // normalises the source layer around its centre before applying behavior
    // matrices, so the matching host-space origin is the identity matrix.
    std::memcpy(matrix, g_renderInvocation->preBehaviorTransformation.data(),
                sizeof(g_renderInvocation->preBehaviorTransformation));
    return 0;
}

int layerEulerAngles(quint64, const char*, qint32, float* angles)
{
    if (!angles || !g_renderInvocation) return -5;
    angles[0] = 0.0f;
    angles[1] = 0.0f;
    angles[2] = 0.0f;
    return 0;
}

int layerOrientation(quint64 host, const char* layerId, qint32 frame,
                     float* orientation)
{
    return layerEulerAngles(host, layerId, frame, orientation);
}

int layerScale(quint64, const char*, qint32, double* scale)
{
    if (!scale || !g_renderInvocation) return -5;
    scale[0] = 1.0;
    scale[1] = 1.0;
    scale[2] = 1.0;
    return 0;
}

const composition::Layer* behaviorLayer(const char* layerId)
{
    if (!g_renderInvocation || !g_renderInvocation->composition || !layerId) {
        return nullptr;
    }
    const QString id = QString::fromLatin1(layerId);
    for (const auto& layer : g_renderInvocation->composition->layers()) {
        if (layer.id.value().compare(id, Qt::CaseInsensitive) == 0) {
            return &layer;
        }
    }
    return nullptr;
}

int numberOfBehaviorLayers(quint64)
{
    return g_renderInvocation && g_renderInvocation->composition
               ? g_renderInvocation->composition->layers().size() : 0;
}

void behaviorLayerId(quint64, qint32 index, char* id, char* name,
                     qint32 nameCapacity, qint32* kind)
{
    static constexpr char nullId[] = "00000000-0000-0000-0000-000000000000";
    if (id) {
        std::memset(id, 0, 0x28);
        std::memcpy(id, nullId, sizeof(nullId));
    }
    if (name && nameCapacity > 0) name[0] = '\0';
    if (kind) *kind = 0;
    if (!g_renderInvocation || !g_renderInvocation->composition
        || index < 0 || index >= g_renderInvocation->composition->layers().size()) {
        return;
    }
    const auto& layer = g_renderInvocation->composition->layers().at(index);
    if (id) {
        const QByteArray encoded = layer.id.value().toLatin1();
        std::memset(id, 0, 0x28);
        std::memcpy(id, encoded.constData(), size_t(qMin(encoded.size(), 0x27)));
    }
    if (name && nameCapacity > 0) {
        const QByteArray encoded = layer.name.toUtf8();
        const int length = qMin(encoded.size(), nameCapacity - 1);
        std::memcpy(name, encoded.constData(), size_t(length));
        name[length] = '\0';
    }
    // Native BiffLayerType values still need a verified crosswalk for every
    // layer kind; 0 is the ordinary media/asset layer accepted by behaviors.
}

int layerPosition(quint64, const char* layerId, qint32 frame, float* position)
{
    if (!position || !g_renderInvocation) return -5;
    if (g_renderInvocation->composition) {
        const composition::Layer* layer = behaviorLayer(layerId);
        if (!layer) {
            position[0] = position[1] = position[2] = 0.0f;
            return -4;
        }
        const QPointF sampled = layer->transform.positionAt(frame);
        position[0] = float(sampled.x());
        position[1] = float(sampled.y());
        position[2] = float(layer->transform.positionZAt(frame));
        return 0;
    }
    position[0] = float(g_renderInvocation->width) * 0.5f;
    position[1] = float(g_renderInvocation->height) * 0.5f;
    position[2] = 0.0f;
    return 0;
}

int layerAnchorPoint(quint64, const char*, qint32, float* anchor)
{
    if (!anchor || !g_renderInvocation) return -5;
    anchor[0] = float(g_renderInvocation->width) * 0.5f;
    anchor[1] = float(g_renderInvocation->height) * 0.5f;
    anchor[2] = 0.0f;
    return 0;
}

void currentAudioLayerId(quint64, const char*, qint32, char* layerId)
{
    if (!layerId) return;
    std::memset(layerId, 0, 0x28);
    static constexpr char nullFxId[] = "00000000-0000-0000-0000-000000000000";
    std::memcpy(layerId, nullFxId, sizeof(nullFxId));
}

int audioSamples(quint64, const char*, qint32 firstSample, qint32 endSample,
                 void* destination, qint32 destinationBytes, qint32* actualFrames)
{
    if (actualFrames) *actualFrames = 0;
    if (!g_renderInvocation || !destination || destinationBytes < 0) return -5;
    std::memset(destination, 0, size_t(destinationBytes));
    if (endSample <= firstSample || !g_renderInvocation->audioSamples) {
        return 0;
    }
    // Tannen::GetAudioSamples returns a mono float array. The final argument
    // before actualFrames is the destination size in bytes, not a channel
    // count; treating it as channels corrupts the caller's scratch buffer.
    const qint32 frameCount = qMin(endSample - firstSample,
                                   destinationBytes / qint32(sizeof(float)));
    auto* output = static_cast<float*>(destination);
    const int channels = qMax(1, g_renderInvocation->channels);
    for (qint32 frame = 0; frame < frameCount; ++frame) {
        const qint64 sourceFrame = qint64(firstSample) + frame
                                   - g_renderInvocation->audioStartSample;
        if (sourceFrame < 0 || sourceFrame >= g_renderInvocation->audioFrameCount) {
            continue;
        }
        qint32 mixed = 0;
        for (int channel = 0; channel < channels; ++channel) {
            mixed += g_renderInvocation->audioSamples[sourceFrame * channels + channel];
        }
        output[frame] = float(mixed) / float(channels * 32768.0);
    }
    if (actualFrames) *actualFrames = frameCount;
    return 0;
}

int copySourceTexture(void* renderContext, void* texture)
{
    if (!texture) {
        return -10;
    }
    std::memset(texture, 0, 0x34);
    if (!renderContext) {
        return -4;
    }
    void* source = nullptr;
    std::memcpy(&source, static_cast<char*>(renderContext) + 0x10,
                sizeof(source));
    if (!source) {
        return -4;
    }
    // tagBiffTexture is 0x34 bytes through scaleY. The source object belongs
    // to this render invocation, so the plugin receives a value copy exactly
    // as Tannen's GetSourceTexture does rather than ownership of the block.
    std::memcpy(texture, source, 0x34);
    return 0;
}

int layerTexture(quint64, void* renderContext, const char*, void* texture)
{
    // The isolated renderer has one source layer. Layer pickers that resolve
    // to it use the same texture as GetSourceTexture.
    return copySourceTexture(renderContext, texture);
}

int layerTextureV2(quint64, void* renderContext, const char*, qint32,
                   void* texture)
{
    return copySourceTexture(renderContext, texture);
}

int sourceTexture(quint64, void* renderContext, qint32, void* texture)
{
    return copySourceTexture(renderContext, texture);
}

int layerDepthTexture(quint64, void*, const char*, qint32, void* texture)
{
    // A plain 2D source has no depth plane. Tannen clears the result and uses
    // -4 for an unavailable layer texture.
    if (!texture) {
        return -10;
    }
    std::memset(texture, 0, 0x34);
    return -4;
}

int numberOfTextOutlineVertices(quint64, const char*, qint32)
{
    // The isolated image renderer has no TextLayer/TextBox graph. The native
    // API represents that valid state as an empty outline.
    return 0;
}

int numberOfMasks(quint64, const char*)
{
    // A standalone image has no composition mask collection.
    return 0;
}

void maskInfo(quint64, const char*, qint32, void*)
{
    // Never called after numberOfMasks() reports an empty collection.
}

void maskPoints(quint64, const char*, qint32, qint32, qint32, void*)
{
    // Never called after numberOfMasks() reports an empty collection.
}

void textOutlinePoints(quint64, const char*, qint32, qint32, void*)
{
    // No points follow an empty outline count.
}

int activeCamera(quint64, qint32, char* layerId)
{
    // A 2D image render has no project camera. Tannen's GetActiveCamera uses
    // an empty FXID as the valid "no active camera" result.
    if (layerId) {
        std::memset(layerId, 0, 0x28);
    }
    return 0;
}

int activeLights(quint64, qint32, void*, qint32, qint32* count)
{
    // The count-query form passes no destination and expects zero when the
    // current composition has no light layers.
    if (count) {
        *count = 0;
    }
    return 0;
}

int cameraInfo(quint64, const char*, qint32, void* info)
{
    if (!info) {
        return -10;
    }
    // tagBiffCameraInfo is 0x100 bytes. The reference synthesizes this same
    // default camera when a composition has no explicit camera layer.
    std::memset(info, 0, 0x100);
    const float one = 1.0f;
    const float farClip = 100000.0f;
    std::memcpy(static_cast<char*>(info) + 0x28, &one, sizeof(one));
    std::memcpy(static_cast<char*>(info) + 0x2c, &one, sizeof(one));
    std::memcpy(static_cast<char*>(info) + 0x38, &one, sizeof(one));
    std::memcpy(static_cast<char*>(info) + 0x3c, &farClip, sizeof(farClip));
    const std::array<float, 16> identity {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    std::memcpy(static_cast<char*>(info) + 0x40, identity.data(),
                sizeof(identity));
    std::memcpy(static_cast<char*>(info) + 0x80, identity.data(),
                sizeof(identity));
    return 0;
}

int motionBlurInfo(quint64, quint64, void*, qint32, qint32* count)
{
    // The isolated still-frame renderer has no neighbouring samples. The
    // count-query form of Tannen's service succeeds with an empty collection.
    if (count) {
        *count = 0;
    }
    return 0;
}

int timelineInfo(quint64, void* info)
{
    if (!info || !g_renderInvocation) {
        return -5;
    }
    std::memset(info, 0, 0x30);
    const qint32 width = g_renderInvocation->width;
    const qint32 height = g_renderInvocation->height;
    const double pixelAspect = 1.0;
    const qint32 frameCount = 1;
    const double frameRate = qMax(0.001, g_renderInvocation->frameRate);
    const qint32 roundedFrameRate = qMax(1, qRound(frameRate));
    const qint32 sampleRate = g_renderInvocation->sampleRate;
    const qint32 channelLayout = g_renderInvocation->channels;
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

int integerParameter(quint64, const char* key, quint32)
{
    const int index = parameterIndex(key);
    if (index < 0) {
        return 0;
    }
    const QString value = parameterValue(key);
    bool ok = false;
    const int numeric = value.toInt(&ok);
    if (ok) {
        return numeric;
    }
    const QStringList& choices = g_renderInvocation->module->parameters.at(index).choices;
    for (int i = 0; i < choices.size(); ++i) {
        if (choices.at(i).compare(value, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return 0;
}

int booleanParameter(quint64, const char* key, quint32)
{
    const QString value = parameterValue(key).trimmed();
    return value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
           || value == QLatin1String("1") ? 1 : 0;
}

int setBooleanParameter(quint64, const char* key, qint32, bool value, qint32)
{
    const int index = parameterIndex(key);
    if (qEnvironmentVariableIsSet("OPENVEGAS_HFPL_RENDER_TRACE")) {
        std::fprintf(stderr, "[hfpl-render] set-bool %s=%d index=%d\n",
                     key ? key : "<null>", value ? 1 : 0, index);
        std::fflush(stderr);
    }
    if (index < 0) {
        return -5;
    }
    // PluginUIButton is a transient bool property. A plugin can clear or
    // update it and immediately observe that value later in the same call.
    g_renderInvocation->valueOverrides.insert(
        index, value ? QStringLiteral("true") : QStringLiteral("false"));
    return 0;
}

int setStringParameter(quint64, const char* key, qint32,
                       const wchar_t* value, qint32)
{
    const int index = parameterIndex(key);
    if (index < 0) {
        return -5;
    }
    g_renderInvocation->valueOverrides.insert(
        index, value ? QString::fromWCharArray(value) : QString());
    return 0;
}

int numberOfKeyframes(quint64, const char*)
{
    // RenderManager already evaluates parameter animation at the requested
    // frame and passes the resolved value to this one-frame renderer.
    return 0;
}

int hostOptions(quint64)
{
    // Tannen's HostOptions implementation returns this capability mask.
    return 1;
}

int redrawCustomUi(quint64, const char*)
{
    // The native host invalidates either the named custom control or all
    // controls when the name is null. Rendering happens off the GUI thread in
    // this port, so there is nothing to repaint synchronously; acknowledging
    // the request matches the host's successful return value and keeps scope
    // effects independent from QWidget ownership.
    return 0;
}

int stringParameterLength(quint64, const char* key, quint32, int* length)
{
    if (!length) {
        return -10;
    }
    const QString value = parameterValue(key);
    *length = int(value.toStdWString().size());
    if (qEnvironmentVariableIsSet("OPENVEGAS_HFPL_RENDER_TRACE")) {
        std::fprintf(stderr, "[hfpl-render] string-length %s=%d\n",
                     key ? key : "<null>", *length);
        std::fflush(stderr);
    }
    return 0;
}

int stringParameter(quint64, const char* key, quint32, wchar_t* destination)
{
    if (!destination) {
        return -10;
    }
    const std::wstring value = parameterValue(key).toStdWString();
    if (qEnvironmentVariableIsSet("OPENVEGAS_HFPL_RENDER_TRACE")) {
        std::fprintf(stderr, "[hfpl-render] string %s chars=%zu\n",
                     key ? key : "<null>", value.size());
        std::fflush(stderr);
    }
    std::memcpy(destination, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
    return 0;
}

int notifyProgress(quint64, qint32, qint32)
{
    // Tannen forwards this to the render profiler and returns whether the
    // caller requested cancellation.  NativeEffectRender runs one frame
    // synchronously, so there is no host-side cancellation object here.
    return 0;
}

quint32 scratchTexture(quint64, quint64, qint32 width, qint32 height,
                       quint32 internalFormat, quint32 pixelFormat,
                       quint32 allocationQualifier, qint32, quint64,
                       qint32* actualWidth, qint32* actualHeight, quint32)
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context || !g_renderInvocation || !g_renderInvocation->scratchPool) {
        return 0;
    }
    width = qMax(1, width);
    height = qMax(1, height);
    auto* gl = context->extraFunctions();
    ScratchTexturePool& pool = *g_renderInvocation->scratchPool;
    for (ScratchTextureRecord& candidate : pool.textures) {
        if (!candidate.reserved && candidate.width == width
            && candidate.height == height
            && candidate.internalFormat == internalFormat
            && candidate.pixelFormat == pixelFormat
            && candidate.allocationQualifier == allocationQualifier) {
            candidate.reserved = true;
            if (actualWidth) {
                *actualWidth = candidate.width;
            }
            if (actualHeight) {
                *actualHeight = candidate.height;
            }
            return candidate.id;
        }
    }
    GLuint texture = 0;
    gl->glGenTextures(1, &texture);
    // A preceding native pass may leave an unpack PBO bound. With a PBO,
    // nullptr below is interpreted as a byte offset into that buffer and the
    // allocation can fail (Chroma Key does this between scratch passes).
    // Tannen's renderer pool isolates this transfer state for every texture.
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
    // The first format is the sized internal format in the renderer-backed
    // Tannen path; the next two describe upload/readback representation.
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
    // Tannen's standalone no-data allocation path ignores the seventh SDK
    // argument and uses unsigned bytes. Plugin call sites pass both GL_TEXTURE_2D
    // and GL_UNSIGNED_BYTE here, so forwarding it blindly as an upload type is
    // invalid for one of the two ABI variants.
    const GLenum uploadType = GL_UNSIGNED_BYTE;
    gl->glTexImage2D(GL_TEXTURE_2D, 0, storageFormat, width, height, 0,
                     uploadFormat, uploadType, nullptr);
    if (actualWidth) {
        *actualWidth = width;
    }
    if (actualHeight) {
        *actualHeight = height;
    }
    pool.textures.append({texture, width, height, internalFormat, pixelFormat,
                          allocationQualifier, true});
    return texture;
}

int clearScratchTexture(quint64, quint64, qint32 texture)
{
    if (!g_renderInvocation || !g_renderInvocation->scratchPool || texture <= 0) {
        return -8;
    }
    return g_renderInvocation->scratchPool->release(GLuint(texture)) ? 0 : -8;
}

void putService(QByteArray& api, int offset, void* callback)
{
    std::memcpy(api.data() + offset, &callback, sizeof(callback));
}

quint64 createVertexArray(quint64, quint64, quint64, quint64,
                          quint64, quint64, quint64, quint64)
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        return 0;
    }
    if (g_hostVertexArray == 0) {
        context->extraFunctions()->glGenVertexArrays(1, &g_hostVertexArray);
    }
    return g_hostVertexArray;
}

quint32 getScratchVbo(quint64, quint32 target, qint32 byteCount, const void* data,
                      quint32 usage, qint32* actualByteCount, quint32)
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context || !g_renderInvocation || !g_renderInvocation->scratchVboPool
        || byteCount < 0) {
        return 0;
    }
    auto* gl = context->extraFunctions();
    ScratchVboPool& pool = *g_renderInvocation->scratchVboPool;
    for (ScratchVboRecord& candidate : pool.buffers) {
        if (!candidate.reserved && candidate.target == target
            && candidate.byteCount == byteCount && candidate.usage == usage) {
            candidate.reserved = true;
            gl->glBindBuffer(GLenum(target), candidate.id);
            if (data && byteCount > 0) {
                gl->glBufferSubData(GLenum(target), 0, GLsizeiptr(byteCount), data);
            }
            if (actualByteCount) {
                *actualByteCount = candidate.byteCount;
            }
            return candidate.id;
        }
    }
    GLuint buffer = 0;
    gl->glGenBuffers(1, &buffer);
    gl->glBindBuffer(GLenum(target), buffer);
    gl->glBufferData(GLenum(target), GLsizeiptr(byteCount), data, GLenum(usage));
    if (actualByteCount) {
        *actualByteCount = byteCount;
    }
    pool.buffers.append({buffer, target, byteCount, usage, true});
    return buffer;
}

int clearReservedVbo(quint64, qint32 buffer)
{
    if (!g_renderInvocation || !g_renderInvocation->scratchVboPool || buffer <= 0) {
        return -15;
    }
    return g_renderInvocation->scratchVboPool->release(GLuint(buffer)) ? 0 : -15;
}

quint32 getScratchRenderbuffer(quint64, quint64, qint32 width, qint32 height,
                               qint32 internalFormat, qint32 precision, quint32)
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context || !g_renderInvocation
        || !g_renderInvocation->scratchRenderbufferPool) {
        return 0;
    }
    width = qMax(1, width);
    height = qMax(1, height);
    ScratchRenderbufferPool& pool = *g_renderInvocation->scratchRenderbufferPool;
    for (ScratchRenderbufferRecord& candidate : pool.renderbuffers) {
        if (!candidate.reserved && candidate.width == width
            && candidate.height == height && candidate.internalFormat == internalFormat
            && candidate.precision == precision) {
            candidate.reserved = true;
            return candidate.id;
        }
    }
    const GLenum format = internalFormat > 0 ? GLenum(internalFormat)
                                              : GLenum(GL_DEPTH24_STENCIL8);
    auto* gl = context->extraFunctions();
    GLuint renderbuffer = 0;
    gl->glGenRenderbuffers(1, &renderbuffer);
    gl->glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    gl->glRenderbufferStorage(GL_RENDERBUFFER, format, width, height);
    if (gl->glGetError() != GL_NO_ERROR) {
        gl->glDeleteRenderbuffers(1, &renderbuffer);
        return 0;
    }
    pool.renderbuffers.append(
        {renderbuffer, width, height, internalFormat, precision, true});
    return renderbuffer;
}

int clearReservedRenderbuffer(quint64, quint64, qint32 renderbuffer)
{
    if (!g_renderInvocation || !g_renderInvocation->scratchRenderbufferPool
        || renderbuffer <= 0) {
        return -16;
    }
    return g_renderInvocation->scratchRenderbufferPool->release(GLuint(renderbuffer))
               ? 0 : -16;
}

void installCpuServices(NativePluginRuntime& runtime)
{
    if (qEnvironmentVariableIsSet("OPENVEGAS_HFPL_RENDER_DIAGNOSTIC_STUBS")) {
        installNativePluginDiagnosticStubs(runtime.apiBlock());
    }
    putService(runtime.apiBlock(), 0xb0, reinterpret_cast<void*>(&integerParameter));
    putService(runtime.apiBlock(), 0xb8, reinterpret_cast<void*>(&scalarParameter));
    putService(runtime.apiBlock(), 0xc0, reinterpret_cast<void*>(&booleanParameter));
    putService(runtime.apiBlock(), 0xc8, reinterpret_cast<void*>(&integerParameter));
    putService(runtime.apiBlock(), 0xd0, reinterpret_cast<void*>(&point2dParameter));
    putService(runtime.apiBlock(), 0xd8, reinterpret_cast<void*>(&scalarParameter));
    putService(runtime.apiBlock(), 0xe0, reinterpret_cast<void*>(&colorParameter));
    putService(runtime.apiBlock(), 0xe8, reinterpret_cast<void*>(&currentAudioLayerId));
    putService(runtime.apiBlock(), 0x108, reinterpret_cast<void*>(&timelineInfo));
    putService(runtime.apiBlock(), 0x128, reinterpret_cast<void*>(&audioLayerInfoV2));
    putService(runtime.apiBlock(), 0x160, reinterpret_cast<void*>(&audioSamples));
    putService(runtime.apiBlock(), 0x178, reinterpret_cast<void*>(&notifyProgress));
    putService(runtime.apiBlock(), 0x1a0, reinterpret_cast<void*>(&setBooleanParameter));
    putService(runtime.apiBlock(), 0x1e8, reinterpret_cast<void*>(&numberOfKeyframes));
    putService(runtime.apiBlock(), 0x268, reinterpret_cast<void*>(&stringParameterLength));
    putService(runtime.apiBlock(), 0x270, reinterpret_cast<void*>(&stringParameter));
    putService(runtime.apiBlock(), 0x278, reinterpret_cast<void*>(&setStringParameter));
    putService(runtime.apiBlock(), 0x340, reinterpret_cast<void*>(&point3dParameter));
    putService(runtime.apiBlock(), 0x350, reinterpret_cast<void*>(&hostOptions));
}

void installBehaviorServices(NativePluginRuntime& runtime)
{
    installCpuServices(runtime);
    putService(runtime.apiBlock(), 0xe8,
               reinterpret_cast<void*>(&layerIdParameter));
    putService(runtime.apiBlock(), 0x60,
               reinterpret_cast<void*>(&createBehaviorFloatSlider));
    putService(runtime.apiBlock(), 0x98,
               reinterpret_cast<void*>(&createBehaviorLayerPicker));
    putService(runtime.apiBlock(), 0x2a0,
               reinterpret_cast<void*>(&clearBehaviorPersistentMessage));
    // PluginHostAPI::GetPreBehaviorEffectTransformation. Transformation
    // behaviors use this as their base matrix before writing the matrix passed
    // through tagBiffBehaviorMC+0x18.
    putService(runtime.apiBlock(), 0x300,
               reinterpret_cast<void*>(&preBehaviorTransformation));
    putService(runtime.apiBlock(), 0x308,
               reinterpret_cast<void*>(&originTransformation));
    putService(runtime.apiBlock(), 0x310,
               reinterpret_cast<void*>(&layerEulerAngles));
    putService(runtime.apiBlock(), 0x318,
               reinterpret_cast<void*>(&layerOrientation));
    putService(runtime.apiBlock(), 0x320,
               reinterpret_cast<void*>(&layerScale));
    putService(runtime.apiBlock(), 0x328,
               reinterpret_cast<void*>(&layerPosition));
    putService(runtime.apiBlock(), 0x330,
               reinterpret_cast<void*>(&layerAnchorPoint));
    putService(runtime.apiBlock(), 0x360,
               reinterpret_cast<void*>(&numberOfBehaviorLayers));
    putService(runtime.apiBlock(), 0x368,
               reinterpret_cast<void*>(&behaviorLayerId));
}

class AudioThreadRenderer
{
public:
    ~AudioThreadRenderer()
    {
        for (const auto& runtime : std::as_const(m_runtimes)) {
            if (runtime && m_pluginInstances.contains(runtime.get())) {
                runtime->notify(4);
            }
        }
    }

    bool processEffect(QVector<qint16>& samples, int channels, int sampleRate,
                       qint64 startSample, const QString& id,
                       const ModuleRecord& record,
                       const QStringList& parameterValues)
    {
        if (channels <= 0 || sampleRate <= 0 || samples.isEmpty()
            || samples.size() % channels != 0) {
            return false;
        }
        NativePluginRuntime* runtime = runtimeFor(id, record);
        if (!runtime) return false;

        const qint32 frameCount = samples.size() / channels;
        const qint32 channelCount = channels;
        const qint32 parameterStart = qint32(qBound<qint64>(
            qint64(0), startSample,
            qint64(std::numeric_limits<qint32>::max())));
        const qint32 parameterEnd = qint32(qMin<qint64>(
            qint64(std::numeric_limits<qint32>::max()),
            qint64(parameterStart) + qMax(0, frameCount - 1)));
        void* samplePointer = samples.data();
        QByteArray context(0x80, '\0');
        std::memcpy(context.data() + 0x10, &channelCount, sizeof(channelCount));
        std::memcpy(context.data() + 0x14, &sampleRate, sizeof(sampleRate));
        std::memcpy(context.data() + 0x20, &samplePointer, sizeof(samplePointer));
        std::memcpy(context.data() + 0x2c, &frameCount, sizeof(frameCount));
        std::memcpy(context.data() + 0x30, &sampleRate, sizeof(sampleRate));
        std::memcpy(context.data() + 0x34, &parameterStart, sizeof(parameterStart));
        std::memcpy(context.data() + 0x38, &parameterEnd, sizeof(parameterEnd));
        std::memcpy(context.data() + 0x40, &startSample, sizeof(startSample));
        // These positions describe discontinuity between consecutive blocks,
        // not the block range. Equal values select ordinary contiguous input;
        // a difference asks effects such as Balance to shift their history.
        std::memcpy(context.data() + 0x48, &startSample, sizeof(startSample));
        return notifyRender(*runtime, context, record, parameterValues,
                            sampleRate, channels);
    }

    bool processTransition(QVector<qint16>& output,
                           const QVector<qint16>& from,
                           const QVector<qint16>& to, int channels,
                           qint32 sampleOffset, qint32 totalTransitionSamples,
                           const QString& id, const ModuleRecord& record,
                           const QStringList& parameterValues)
    {
        if (channels <= 0 || from.isEmpty() || to.isEmpty()
            || from.size() % channels != 0 || to.size() % channels != 0
            || totalTransitionSamples <= 0) {
            return false;
        }
        NativePluginRuntime* runtime = runtimeFor(id, record);
        if (!runtime) return false;

        const qint32 fromFrames = from.size() / channels;
        const qint32 toFrames = to.size() / channels;
        const qint32 outputFrames = qMin(fromFrames, toFrames);
        output.resize(outputFrames * channels);
        const qint32 channelCount = channels;
        const qint32 transitionCurrent = qMax(0, sampleOffset);
        const qint32 transitionDuration = qMax(1, totalTransitionSamples);
        const void* fromPointer = from.constData();
        const void* toPointer = to.constData();
        void* outputPointer = output.data();
        QByteArray context(0x70, '\0');
        std::memcpy(context.data() + 0x0c, &channelCount, sizeof(channelCount));
        std::memcpy(context.data() + 0x18, &fromPointer, sizeof(fromPointer));
        std::memcpy(context.data() + 0x20, &fromFrames, sizeof(fromFrames));
        std::memcpy(context.data() + 0x28, &toPointer, sizeof(toPointer));
        std::memcpy(context.data() + 0x30, &toFrames, sizeof(toFrames));
        std::memcpy(context.data() + 0x38, &outputPointer, sizeof(outputPointer));
        std::memcpy(context.data() + 0x40, &outputFrames, sizeof(outputFrames));
        std::memcpy(context.data() + 0x44, &transitionCurrent,
                    sizeof(transitionCurrent));
        std::memcpy(context.data() + 0x48, &transitionDuration,
                    sizeof(transitionDuration));
        std::memcpy(context.data() + 0x4c, &transitionDuration,
                    sizeof(transitionDuration));
        return notifyRender(*runtime, context, record, parameterValues,
                            48000, channels);
    }

private:
    bool notifyRender(NativePluginRuntime& runtime, QByteArray& context,
                      const ModuleRecord& record,
                      const QStringList& parameterValues, int sampleRate,
                      int channels)
    {
        void* contextPointer = context.data();
        std::memcpy(runtime.apiBlock().data() + 0x20, &contextPointer,
                    sizeof(contextPointer));
        RenderInvocation invocation;
        invocation.module = &record;
        invocation.values = &parameterValues;
        invocation.sampleRate = sampleRate;
        invocation.channels = channels;
        invocation.audioSamples = static_cast<const qint16*>(
            *reinterpret_cast<void* const*>(context.constData() + 0x20));
        std::memcpy(&invocation.audioFrameCount, context.constData() + 0x2c,
                    sizeof(invocation.audioFrameCount));
        invocation.audioStartSample = 0;
        if (context.size() >= 0x48) {
            std::memcpy(&invocation.audioStartSample, context.constData() + 0x40,
                        sizeof(invocation.audioStartSample));
        }
        RenderInvocation* previousInvocation = g_renderInvocation;
        g_renderInvocation = &invocation;
        if (qEnvironmentVariableIsSet("OPENVEGAS_HFPL_RENDER_DIAGNOSTIC_STUBS")) {
            resetNativePluginDiagnosticServiceOffset();
        }
        const int result = runtime.notify(10);
        if (qEnvironmentVariableIsSet("OPENVEGAS_HFPL_RENDER_TRACE")) {
            std::fprintf(stderr,
                         "[hfpl-audio] notify=%d fault=0x%08lx rva=0x%llx access=0x%llx service=0x%llx trace=%s\n",
                         result, runtime.lastFaultCode(),
                         static_cast<unsigned long long>(runtime.lastFaultInstructionRva()),
                         static_cast<unsigned long long>(runtime.lastFaultAccessAddress()),
                         static_cast<unsigned long long>(
                             lastNativePluginDiagnosticServiceOffset()),
                         nativePluginDiagnosticTrace().toUtf8().constData());
            std::fflush(stderr);
        }
        g_renderInvocation = previousInvocation;
        if (result == 1 && runtime.lastFaultCode() == 0) return true;
        qWarning().noquote()
            << QStringLiteral("Native HFPL audio Notify(10) failed for %1: result=%2, "
                              "fault=0x%3, instructionRva=0x%4, service=0x%5, %6")
                   .arg(record.filePath).arg(result)
                   .arg(quint32(runtime.lastFaultCode()), 8, 16, QLatin1Char('0'))
                   .arg(runtime.lastFaultInstructionRva(), 0, 16)
                   .arg(lastNativePluginDiagnosticServiceOffset(), 0, 16)
                   .arg(runtime.errorString());
        return false;
    }

    NativePluginRuntime* runtimeFor(const QString& id,
                                    const ModuleRecord& record)
    {
        const QFileInfo file(record.filePath);
        const QString cacheKey = QStringLiteral("%1\n%2\n%3/%4")
                                     .arg(id, file.absoluteFilePath())
                                     .arg(file.size())
                                     .arg(file.lastModified().toMSecsSinceEpoch());
        const auto existing = m_runtimes.find(cacheKey);
        if (existing != m_runtimes.end()) return existing.value().get();
        auto runtime = std::make_shared<NativePluginRuntime>();
        if (!runtime->load(record.filePath, record.dependencyDirectory)) {
            qWarning().noquote() << "Unable to load native HFPL audio module:"
                                 << runtime->errorString();
            return nullptr;
        }
        installCpuServices(*runtime);
        const int initializationResult = runtime->notify(8);
        if (initializationResult != 1 || runtime->lastFaultCode() != 0) {
            qWarning().noquote() << "Native HFPL audio Notify(8) failed:"
                                 << runtime->errorString();
            return nullptr;
        }
        const int instanceResult = runtime->notify(3);
        void* instancePointer = nullptr;
        std::memcpy(&instancePointer, runtime->apiBlock().constData() + 0x10,
                    sizeof(instancePointer));
        if (runtime->lastFaultCode() != 0) {
            qWarning().noquote() << "Native HFPL audio Notify(3) failed:"
                                 << runtime->errorString();
            return nullptr;
        }
        if (instanceResult == 1 && instancePointer) {
            m_pluginInstances.insert(runtime.get());
        } else {
            auto state = std::make_shared<QByteArray>(0x100, '\0');
            instancePointer = state->data();
            std::memcpy(runtime->apiBlock().data() + 0x10, &instancePointer,
                        sizeof(instancePointer));
            m_fallbackInstanceStates.insert(runtime.get(), std::move(state));
        }
        NativePluginRuntime* result = runtime.get();
        m_runtimes.insert(cacheKey, std::move(runtime));
        return result;
    }

    QHash<QString, std::shared_ptr<NativePluginRuntime>> m_runtimes;
    QSet<NativePluginRuntime*> m_pluginInstances;
    QHash<NativePluginRuntime*, std::shared_ptr<QByteArray>> m_fallbackInstanceStates;
};

class BehaviorThreadRenderer
{
public:
    ~BehaviorThreadRenderer()
    {
        for (const auto& runtime : std::as_const(m_runtimes)) {
            if (runtime && m_pluginInstances.contains(runtime.get())) {
                runtime->notify(4);
            }
        }
    }

    bool evaluate(NativeBehaviorResult& result, int timelineFrame, int localFrame,
                  int layerDurationFrames, int canvasWidth, int canvasHeight,
                  double frameRate, bool includeSimulation,
                  const QString& id, const ModuleRecord& record,
                  const QStringList& parameterValues,
                  const composition::Composition* composition,
                  const core::Identifier& sourceLayerId)
    {
#ifndef Q_OS_WIN
        Q_UNUSED(result);
        Q_UNUSED(timelineFrame);
        Q_UNUSED(localFrame);
        Q_UNUSED(layerDurationFrames);
        Q_UNUSED(canvasWidth);
        Q_UNUSED(canvasHeight);
        Q_UNUSED(frameRate);
        Q_UNUSED(includeSimulation);
        Q_UNUSED(id);
        Q_UNUSED(record);
        Q_UNUSED(parameterValues);
        Q_UNUSED(composition);
        Q_UNUSED(sourceLayerId);
        return false;
#else
        RenderInvocation invocation;
        invocation.module = &record;
        invocation.values = &parameterValues;
        invocation.width = qMax(1, canvasWidth);
        invocation.height = qMax(1, canvasHeight);
        invocation.timelineFrame = timelineFrame;
        invocation.layerDurationFrames = qMax(1, layerDurationFrames);
        invocation.frameRate = qMax(0.001, frameRate);
        invocation.composition = composition;
        invocation.sourceLayerId = sourceLayerId;
        RenderInvocation* previousInvocation = g_renderInvocation;
        g_renderInvocation = &invocation;
        NativePluginRuntime* runtime = runtimeFor(id, record);
        if (!runtime) {
            g_renderInvocation = previousInvocation;
            return false;
        }

        static constexpr char nullLayerId[] =
            "00000000-0000-0000-0000-000000000000";
        const QByteArray sourceId = sourceLayerId.isValid()
                                        ? sourceLayerId.value().toLatin1()
                                        : QByteArray(nullLayerId);
        const char* layerId = sourceId.constData();

        std::array<float, 16> matrix = invocation.preBehaviorTransformation;
        float* matrixPointer = matrix.data();
        QByteArray transformationContext(0x80, '\0');
        std::memcpy(transformationContext.data() + 0x00, &layerId,
                    sizeof(layerId));
        std::memcpy(transformationContext.data() + 0x08, &timelineFrame,
                    sizeof(timelineFrame));
        std::memcpy(transformationContext.data() + 0x0c, &localFrame,
                    sizeof(localFrame));
        std::memcpy(transformationContext.data() + 0x18, &matrixPointer,
                    sizeof(matrixPointer));
        void* contextPointer = transformationContext.data();
        std::memcpy(runtime->apiBlock().data() + 0x20, &contextPointer,
                    sizeof(contextPointer));
        const qint32 transformationCapability = 6;
        std::memcpy(runtime->apiBlock().data() + 0x498,
                    &transformationCapability, sizeof(transformationCapability));
        const int transformationResult = runtime->notify(102);
        const bool transformationOk = transformationResult == 1
                                      && runtime->lastFaultCode() == 0;

        float opacity = 1.0f;
        QByteArray opacityContext(0x80, '\0');
        std::memcpy(opacityContext.data() + 0x00, &layerId, sizeof(layerId));
        std::memcpy(opacityContext.data() + 0x08, &timelineFrame,
                    sizeof(timelineFrame));
        std::memcpy(opacityContext.data() + 0x0c, &localFrame,
                    sizeof(localFrame));
        std::memcpy(opacityContext.data() + 0x18, &opacity, sizeof(opacity));
        contextPointer = opacityContext.data();
        std::memcpy(runtime->apiBlock().data() + 0x20, &contextPointer,
                    sizeof(contextPointer));
        const qint32 opacityCapability = 7;
        std::memcpy(runtime->apiBlock().data() + 0x498, &opacityCapability,
                    sizeof(opacityCapability));
        const int opacityResult = runtime->notify(104);
        const bool opacityOk = opacityResult == 1 && runtime->lastFaultCode() == 0;

        struct SimulationAccumulator {
            float acceleration[3] {0.0f, 0.0f, 0.0f};
            float force[3] {0.0f, 0.0f, 0.0f};
            float forceWeight = 0.0f;
            float damping = 1.0f;
        } simulation;
        const auto simulationCallback = +[](void* opaque, int, float x, float y,
                                             float z, int mode) {
            auto* value = static_cast<SimulationAccumulator*>(opaque);
            if (!value) return;
            float* destination = mode == 1 ? value->force : value->acceleration;
            destination[0] += x;
            destination[1] += y;
            destination[2] += z;
            if (mode == 1) value->forceWeight += 1.0f;
        };
        const auto dampingCallback = +[](void* opaque, int, float damping) {
            auto* value = static_cast<SimulationAccumulator*>(opaque);
            if (value) value->damping *= damping;
        };
        std::array<float, 3> velocity {0.0f, 0.0f, 0.0f};
        std::array<float, 3> position {0.0f, 0.0f, 0.0f};
        bool simulationOk = false;
        const double timeStep = 1.0 / invocation.frameRate;
        const int simulationFrames = includeSimulation ? qMax(0, localFrame) : 0;
        for (int simulationFrame = 0; simulationFrame < simulationFrames;
             ++simulationFrame) {
            simulation = {};
            QByteArray simulationContext(0x80, '\0');
            const int objectIndex = 0;
            const double currentTime = double(simulationFrame + 1) * timeStep;
            std::memcpy(simulationContext.data() + 0x00, &objectIndex,
                        sizeof(objectIndex));
            std::memcpy(simulationContext.data() + 0x18, &currentTime,
                        sizeof(currentTime));
            std::memcpy(simulationContext.data() + 0x20, &timeStep,
                        sizeof(timeStep));
            void* simulationState = &simulation;
            std::memcpy(simulationContext.data() + 0x28, &simulationState,
                        sizeof(simulationState));
            // SimulateLayers exposes one 0x80-byte record per participating
            // layer through SC+0x30. Drag reads the current velocity at
            // record+0x28 before submitting its damping multiplier.
            QByteArray layerSimulationState(0x80, '\0');
            std::memcpy(layerSimulationState.data() + 0x28, velocity.data(),
                        sizeof(float) * velocity.size());
            void* layerSimulationStatePointer = layerSimulationState.data();
            std::memcpy(simulationContext.data() + 0x30,
                        &layerSimulationStatePointer,
                        sizeof(layerSimulationStatePointer));
            const auto callback = simulationCallback;
            std::memcpy(simulationContext.data() + 0x40, &callback,
                        sizeof(callback));
            const auto dragCallback = dampingCallback;
            std::memcpy(simulationContext.data() + 0x48, &dragCallback,
                        sizeof(dragCallback));
            contextPointer = simulationContext.data();
            std::memcpy(runtime->apiBlock().data() + 0x20, &contextPointer,
                        sizeof(contextPointer));
            const qint32 simulationCapability = 9;
            std::memcpy(runtime->apiBlock().data() + 0x498,
                        &simulationCapability, sizeof(simulationCapability));
            const int simulationResult = runtime->notify(103);
            if (simulationResult != 1 || runtime->lastFaultCode() != 0) {
                break;
            }
            simulationOk = true;
            const float dampingFactor = float(std::pow(
                double(simulation.damping), timeStep));
            for (int component = 0; component < 3; ++component) {
                float acceleration = simulation.acceleration[component];
                if (simulation.forceWeight > 0.0f) {
                    acceleration += simulation.force[component]
                                    / simulation.forceWeight;
                }
                velocity[component] = velocity[component] * dampingFactor
                                      + acceleration * float(timeStep);
                position[component] += velocity[component] * float(timeStep);
            }
        }

        if (qEnvironmentVariableIsSet("OPENVEGAS_HFPL_RENDER_TRACE")) {
            std::fprintf(stderr,
                         "[hfpl-behavior] transform=%d opacity=%d simulation=%d "
                         "position=%g,%g,%g service=0x%x trace=%s\n",
                         transformationResult, opacityResult, simulationOk ? 1 : 0,
                         double(position[0]), double(position[1]), double(position[2]),
                         lastNativePluginDiagnosticServiceOffset(),
                         nativePluginDiagnosticTrace().toUtf8().constData());
            std::fflush(stderr);
        }

        g_renderInvocation = previousInvocation;
        if (!transformationOk && !opacityOk && !simulationOk) {
            if (runtime->lastFaultCode() != 0) {
                qWarning().noquote()
                    << QStringLiteral("Native HFPL behavior failed for %1: fault=0x%2, "
                                      "instructionRva=0x%3, service=0x%4, %5")
                           .arg(record.filePath)
                           .arg(quint32(runtime->lastFaultCode()), 8, 16,
                                QLatin1Char('0'))
                           .arg(runtime->lastFaultInstructionRva(), 0, 16)
                           .arg(lastNativePluginDiagnosticServiceOffset(), 0, 16)
                           .arg(runtime->errorString());
            }
            return false;
        }
        if (!transformationOk) {
            matrix = invocation.preBehaviorTransformation;
        }
        if (simulationOk) {
            matrix[12] += position[0];
            matrix[13] += position[1];
            matrix[14] += position[2];
        }
        // BIFF and QMatrix4x4 both store column-major data, but QMatrix4x4's
        // float-pointer constructor interprets its input as row-major and
        // transposes it. Copy into the writable storage so translation remains
        // at indices 12..14 exactly as supplied by the native module.
        result.transformation.setToIdentity();
        std::memcpy(result.transformation.data(), matrix.data(),
                    sizeof(float) * matrix.size());
        result.opacity = opacityOk && qIsFinite(opacity)
                             ? qBound(0.0f, opacity, 1.0f) : 1.0f;
        return true;
#endif
    }

    bool simulateStack(NativeBehaviorResult& result, int timelineFrame,
                       int localFrame, int layerDurationFrames,
                       int canvasWidth, int canvasHeight, double frameRate,
                       const QVector<BehaviorStackEntry>& entries,
                       const composition::Composition* composition,
                       const core::Identifier& sourceLayerId)
    {
#ifndef Q_OS_WIN
        Q_UNUSED(result);
        Q_UNUSED(timelineFrame);
        Q_UNUSED(localFrame);
        Q_UNUSED(layerDurationFrames);
        Q_UNUSED(canvasWidth);
        Q_UNUSED(canvasHeight);
        Q_UNUSED(frameRate);
        Q_UNUSED(entries);
        Q_UNUSED(composition);
        Q_UNUSED(sourceLayerId);
        return false;
#else
        struct SimulationAccumulator {
            float acceleration[3] {0.0f, 0.0f, 0.0f};
            float force[3] {0.0f, 0.0f, 0.0f};
            float forceWeight = 0.0f;
            float damping = 1.0f;
        } simulation;
        const auto forceCallback = +[](void* opaque, int, float x, float y,
                                       float z, int mode) {
            auto* value = static_cast<SimulationAccumulator*>(opaque);
            if (!value) return;
            float* destination = mode == 1 ? value->force : value->acceleration;
            destination[0] += x;
            destination[1] += y;
            destination[2] += z;
            if (mode == 1) value->forceWeight += 1.0f;
        };
        const auto dampingCallback = +[](void* opaque, int, float damping) {
            auto* value = static_cast<SimulationAccumulator*>(opaque);
            if (value) value->damping *= damping;
        };

        if (entries.isEmpty() || localFrame <= 0) return false;
        const double safeFrameRate = qMax(0.001, frameRate);
        const double timeStep = 1.0 / safeFrameRate;
        std::array<float, 3> velocity {0.0f, 0.0f, 0.0f};
        std::array<float, 3> position {0.0f, 0.0f, 0.0f};
        QSet<QString> rejected;
        bool anySimulation = false;
        RenderInvocation* previousInvocation = g_renderInvocation;

        for (int simulationFrame = 0; simulationFrame < localFrame;
             ++simulationFrame) {
            simulation = {};
            QByteArray layerSimulationState(0x80, '\0');
            std::memcpy(layerSimulationState.data() + 0x28, velocity.data(),
                        sizeof(float) * velocity.size());
            void* layerSimulationStatePointer = layerSimulationState.data();

            for (const BehaviorStackEntry& entry : entries) {
                if (rejected.contains(entry.id)) continue;
                RenderInvocation invocation;
                invocation.module = &entry.module;
                invocation.values = &entry.values;
                invocation.width = qMax(1, canvasWidth);
                invocation.height = qMax(1, canvasHeight);
                invocation.timelineFrame = timelineFrame;
                invocation.layerDurationFrames = qMax(1, layerDurationFrames);
                invocation.frameRate = safeFrameRate;
                invocation.composition = composition;
                invocation.sourceLayerId = sourceLayerId;
                g_renderInvocation = &invocation;
                NativePluginRuntime* runtime = runtimeFor(entry.id, entry.module);
                if (!runtime) {
                    rejected.insert(entry.id);
                    continue;
                }

                QByteArray context(0x80, '\0');
                const int objectIndex = 0;
                const double currentTime = double(simulationFrame + 1) * timeStep;
                std::memcpy(context.data() + 0x00, &objectIndex,
                            sizeof(objectIndex));
                std::memcpy(context.data() + 0x18, &currentTime,
                            sizeof(currentTime));
                std::memcpy(context.data() + 0x20, &timeStep,
                            sizeof(timeStep));
                void* simulationState = &simulation;
                std::memcpy(context.data() + 0x28, &simulationState,
                            sizeof(simulationState));
                std::memcpy(context.data() + 0x30, &layerSimulationStatePointer,
                            sizeof(layerSimulationStatePointer));
                const auto force = forceCallback;
                const auto drag = dampingCallback;
                std::memcpy(context.data() + 0x40, &force, sizeof(force));
                std::memcpy(context.data() + 0x48, &drag, sizeof(drag));
                void* contextPointer = context.data();
                std::memcpy(runtime->apiBlock().data() + 0x20, &contextPointer,
                            sizeof(contextPointer));
                const qint32 simulationCapability = 9;
                std::memcpy(runtime->apiBlock().data() + 0x498,
                            &simulationCapability, sizeof(simulationCapability));
                const int simulationResult = runtime->notify(103);
                if (simulationResult == 1 && runtime->lastFaultCode() == 0) {
                    anySimulation = true;
                } else {
                    rejected.insert(entry.id);
                }
            }

            const float dampingFactor = float(std::pow(
                double(simulation.damping), timeStep));
            for (int component = 0; component < 3; ++component) {
                float acceleration = simulation.acceleration[component];
                if (simulation.forceWeight > 0.0f) {
                    acceleration += simulation.force[component]
                                    / simulation.forceWeight;
                }
                velocity[component] = velocity[component] * dampingFactor
                                      + acceleration * float(timeStep);
                position[component] += velocity[component] * float(timeStep);
            }
        }
        g_renderInvocation = previousInvocation;
        if (!anySimulation) return false;

        result.transformation.setToIdentity();
        float* matrix = result.transformation.data();
        matrix[12] = position[0];
        matrix[13] = position[1];
        matrix[14] = position[2];
        result.opacity = 1.0f;
        return true;
#endif
    }

private:
    NativePluginRuntime* runtimeFor(const QString& id,
                                    const ModuleRecord& record)
    {
        const QFileInfo file(record.filePath);
        const QString cacheKey = QStringLiteral("%1\n%2\n%3/%4")
                                     .arg(id, file.absoluteFilePath())
                                     .arg(file.size())
                                     .arg(file.lastModified().toMSecsSinceEpoch());
        const auto existing = m_runtimes.find(cacheKey);
        if (existing != m_runtimes.end()) return existing.value().get();
        auto runtime = std::make_shared<NativePluginRuntime>();
        if (!runtime->load(record.filePath, record.dependencyDirectory)) {
            qWarning().noquote() << "Unable to load native HFPL behavior module:"
                                 << runtime->errorString();
            return nullptr;
        }
        installBehaviorServices(*runtime);
        const int instanceResult = runtime->notify(3);
        void* instancePointer = nullptr;
        std::memcpy(&instancePointer, runtime->apiBlock().constData() + 0x10,
                    sizeof(instancePointer));
        if (runtime->lastFaultCode() != 0) return nullptr;
        if (instanceResult == 1 && instancePointer) {
            m_pluginInstances.insert(runtime.get());
        } else {
            auto state = std::make_shared<QByteArray>(0x100, '\0');
            instancePointer = state->data();
            std::memcpy(runtime->apiBlock().data() + 0x10, &instancePointer,
                        sizeof(instancePointer));
            m_fallbackInstanceStates.insert(runtime.get(), std::move(state));
        }
        NativePluginRuntime* result = runtime.get();
        m_runtimes.insert(cacheKey, std::move(runtime));
        return result;
    }

    QHash<QString, std::shared_ptr<NativePluginRuntime>> m_runtimes;
    QSet<NativePluginRuntime*> m_pluginInstances;
    QHash<NativePluginRuntime*, std::shared_ptr<QByteArray>> m_fallbackInstanceStates;
};

class ThreadRenderer
{
public:
    ~ThreadRenderer()
    {
        if (m_ready && m_surface) {
            m_context.makeCurrent(m_surface.data());
            // Notify(3) creates the module-owned per-effect instance stored at
            // API+0x10. Release it while the module and GL context are still
            // alive; Notify(1) in NativePluginRuntime then unloads globals.
            for (const auto& runtime : std::as_const(m_runtimes)) {
                if (runtime && m_pluginInstances.contains(runtime.get())) {
                    runtime->notify(4);
                }
            }
            m_runtimes.clear();
            m_pluginInstances.clear();
            m_fallbackInstanceStates.clear();
            m_scratchPool.destroy(m_context.extraFunctions());
            m_scratchVboPool.destroy(m_context.extraFunctions());
            m_scratchRenderbufferPool.destroy(m_context.extraFunctions());
            if (g_hostVertexArray != 0) {
                m_context.extraFunctions()->glDeleteVertexArrays(1, &g_hostVertexArray);
                g_hostVertexArray = 0;
            }
            m_context.doneCurrent();
        }
        if (m_surface) {
            QOffscreenSurface* surface = m_surface.data();
            m_surface = nullptr;
            if (QGuiApplication::instance()) {
                if (QThread::currentThread() == surface->thread()) {
                    delete surface;
                } else {
                    // RenderManager waits for its worker from the GUI thread;
                    // a blocking deletion here would deadlock that shutdown.
                    surface->deleteLater();
                }
            }
            // At CRT/TLS teardown QGuiApplication may already be gone. The OS
            // is reclaiming the process in that case, so touching the Qt
            // platform surface is less safe than leaving this tiny object.
        }
    }

    bool render(QImage& image, const QString& id, const ModuleRecord& record,
                const QStringList& parameterValues, const QImage* secondInput = nullptr,
                float transitionProgress = 0.0f)
    {
#ifndef Q_OS_WIN
        Q_UNUSED(image);
        Q_UNUSED(id);
        Q_UNUSED(record);
        Q_UNUSED(parameterValues);
        Q_UNUSED(secondInput);
        Q_UNUSED(transitionProgress);
        return false;
#else
        const bool transition = secondInput != nullptr;
        const bool trace = qEnvironmentVariableIsSet("OPENVEGAS_HFPL_RENDER_TRACE");
        if (trace) {
            std::fprintf(stderr, "[hfpl-render] begin %s %dx%d\n",
                         record.filePath.toUtf8().constData(), image.width(), image.height());
            std::fflush(stderr);
        }
        if (!ensureContext()) {
            qWarning().noquote() << "Native HFPL OpenGL initialization failed for"
                                 << record.filePath;
            return false;
        }
        if (!m_context.makeCurrent(m_surface.data())) {
            qWarning().noquote() << "Native HFPL context activation failed for"
                                 << record.filePath;
            return false;
        }
        NativePluginRuntime* runtime = runtimeFor(id, record);
        if (trace) {
            std::fprintf(stderr, "[hfpl-render] runtime=%p\n", static_cast<void*>(runtime));
            std::fflush(stderr);
        }
        if (!runtime) {
            qWarning().noquote() << "Native HFPL runtime initialization failed for"
                                 << record.filePath;
            return false;
        }

        auto* gl = m_context.extraFunctions();
        const int width = image.width();
        const int height = image.height();
        QOpenGLFramebufferObjectFormat outputFormat;
        // Tannen hands effects a colour target without an implicit depth/stencil
        // attachment. 3D-style modules (for example Wireframe) attach their own
        // scratch depth texture. Keeping Qt's combined renderbuffer here leaves
        // its stencil half attached and makes that framebuffer unsupported.
        outputFormat.setAttachment(QOpenGLFramebufferObject::NoAttachment);
        outputFormat.setTextureTarget(GL_TEXTURE_2D);
        outputFormat.setInternalTextureFormat(GL_RGBA32F);
        QOpenGLFramebufferObject output(QSize(width, height), outputFormat);
        if (!output.isValid() || !output.bind()) {
            qWarning().noquote() << "Native HFPL framebuffer creation failed for"
                                 << record.filePath;
            return false;
        }
        gl->glViewport(0, 0, width, height);
        gl->glDisable(GL_SCISSOR_TEST);
        gl->glDisable(GL_DEPTH_TEST);
        gl->glDisable(GL_CULL_FACE);
        gl->glDisable(GL_BLEND);
        gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT);

        GLuint inputTexture = 0;
        gl->glGenTextures(1, &inputTexture);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, inputTexture);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const QImage rgbaInput = image.convertToFormat(QImage::Format_RGBA8888);
        QVector<float> inputPixels(width * height * 4);
        for (int y = 0; y < height; ++y) {
            const uchar* source = rgbaInput.constScanLine(y);
            float* destination = inputPixels.data() + y * width * 4;
            for (int x = 0; x < width * 4; ++x) {
                destination[x] = float(source[x]) / 255.0f;
            }
        }
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA,
                         GL_FLOAT, inputPixels.constData());

        GLuint secondTexture = 0;
        if (transition) {
            gl->glGenTextures(1, &secondTexture);
            gl->glActiveTexture(GL_TEXTURE1);
            gl->glBindTexture(GL_TEXTURE_2D, secondTexture);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            const QImage rgbaSecond = secondInput->scaled(
                width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                          .convertToFormat(QImage::Format_RGBA8888);
            QVector<float> secondPixels(width * height * 4);
            for (int y = 0; y < height; ++y) {
                const uchar* source = rgbaSecond.constScanLine(y);
                float* destination = secondPixels.data() + y * width * 4;
                for (int x = 0; x < width * 4; ++x) {
                    destination[x] = float(source[x]) / 255.0f;
                }
            }
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0,
                             GL_RGBA, GL_FLOAT, secondPixels.constData());
            gl->glActiveTexture(GL_TEXTURE0);
        }

        QByteArray inputTextureBlock(0x80, '\0');
        std::memcpy(inputTextureBlock.data(), &inputTexture, sizeof(inputTexture));
        // tagBiffTexture is {name, target, format, allocated size, visible
        // size, UV rectangle}. Tannen::GetSourceTexture writes GL_TEXTURE_2D
        // at +0x04; treating this slot as a component type creates an invalid
        // texture binding in multi-pass effects such as Chroma Key.
        const quint32 textureTarget = GL_TEXTURE_2D;
        const quint32 texturePixelFormat = GL_RGBA;
        std::memcpy(inputTextureBlock.data() + 0x04, &textureTarget,
                    sizeof(textureTarget));
        std::memcpy(inputTextureBlock.data() + 0x08, &texturePixelFormat,
                    sizeof(texturePixelFormat));
        // tagBiffTexture: allocated size at +0x0c/+0x10, visible size at
        // +0x14/+0x18, followed by the UV rectangle at +0x1c. Effects that
        // build intermediate textures (Chroma Key, Derez, colour wheels) use
        // all four dimensions rather than only the GL name.
        const qint32 textureWidth = width;
        const qint32 textureHeight = height;
        std::memcpy(inputTextureBlock.data() + 0x0c, &textureWidth,
                    sizeof(textureWidth));
        std::memcpy(inputTextureBlock.data() + 0x10, &textureHeight,
                    sizeof(textureHeight));
        std::memcpy(inputTextureBlock.data() + 0x14, &textureWidth,
                    sizeof(textureWidth));
        std::memcpy(inputTextureBlock.data() + 0x18, &textureHeight,
                    sizeof(textureHeight));
        const float uvBounds[] = {0.0f, 0.0f, 1.0f, 1.0f};
        std::memcpy(inputTextureBlock.data() + 0x1c, uvBounds, sizeof(uvBounds));
        const float textureScale = 1.0f;
        std::memcpy(inputTextureBlock.data() + 0x2c, &textureScale,
                    sizeof(textureScale));
        std::memcpy(inputTextureBlock.data() + 0x30, &textureScale,
                    sizeof(textureScale));

        QByteArray secondTextureBlock(0x80, '\0');
        if (transition) {
            std::memcpy(secondTextureBlock.data(), &secondTexture, sizeof(secondTexture));
            std::memcpy(secondTextureBlock.data() + 0x04, &textureTarget,
                        sizeof(textureTarget));
            std::memcpy(secondTextureBlock.data() + 0x08, &texturePixelFormat,
                        sizeof(texturePixelFormat));
            std::memcpy(secondTextureBlock.data() + 0x0c, &textureWidth,
                        sizeof(textureWidth));
            std::memcpy(secondTextureBlock.data() + 0x10, &textureHeight,
                        sizeof(textureHeight));
            std::memcpy(secondTextureBlock.data() + 0x14, &textureWidth,
                        sizeof(textureWidth));
            std::memcpy(secondTextureBlock.data() + 0x18, &textureHeight,
                        sizeof(textureHeight));
            std::memcpy(secondTextureBlock.data() + 0x1c, uvBounds, sizeof(uvBounds));
            std::memcpy(secondTextureBlock.data() + 0x2c, &textureScale,
                        sizeof(textureScale));
            std::memcpy(secondTextureBlock.data() + 0x30, &textureScale,
                        sizeof(textureScale));
        }

        QByteArray outputTextureBlock(0x80, '\0');
        const GLuint outputTexture = output.texture();
        std::memcpy(outputTextureBlock.data(), &outputTexture, sizeof(outputTexture));
        std::memcpy(outputTextureBlock.data() + 0x04, &textureTarget,
                    sizeof(textureTarget));
        std::memcpy(outputTextureBlock.data() + 0x08, &texturePixelFormat,
                    sizeof(texturePixelFormat));
        std::memcpy(outputTextureBlock.data() + 0x0c, &textureWidth,
                    sizeof(textureWidth));
        std::memcpy(outputTextureBlock.data() + 0x10, &textureHeight,
                    sizeof(textureHeight));
        std::memcpy(outputTextureBlock.data() + 0x14, &textureWidth,
                    sizeof(textureWidth));
        std::memcpy(outputTextureBlock.data() + 0x18, &textureHeight,
                    sizeof(textureHeight));
        std::memcpy(outputTextureBlock.data() + 0x1c, uvBounds, sizeof(uvBounds));
        std::memcpy(outputTextureBlock.data() + 0x2c, &textureScale,
                    sizeof(textureScale));
        std::memcpy(outputTextureBlock.data() + 0x30, &textureScale,
                    sizeof(textureScale));

        const std::array<float, 16> projection {
            2.0f / width, 0.0f,          0.0f, 0.0f,
            0.0f,         2.0f / height, 0.0f, 0.0f,
            0.0f,         0.0f,         -1.0f, 0.0f,
           -1.0f,        -1.0f,          0.0f, 1.0f
        };
        const std::array<float, 16> modelView {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        const float* projectionPointer = projection.data();
        const float* modelViewPointer = modelView.data();

        QByteArray frameBlock(0x400, '\0');
        // RenderContext +0x08 is the current source UUID string. Scope
        // effects compare all 36 characters with the null UUID before they
        // decide whether cached histogram data can be reused. A null pointer
        // reaches strncmp() inside Histogram/Waveform and faults before any
        // host callback is made.
        static constexpr char kNullSourceUuid[] =
            "00000000-0000-0000-0000-000000000000";
        const char* sourceUuid = kNullSourceUuid;
        std::memcpy(frameBlock.data() + 0x08, &sourceUuid, sizeof(sourceUuid));
        void* inputPointer = inputTextureBlock.data();
        std::memcpy(frameBlock.data() + 0x10, &inputPointer, sizeof(inputPointer));
        void* outputPointer = outputTextureBlock.data();
        if (transition) {
            void* secondPointer = secondTextureBlock.data();
            std::memcpy(frameBlock.data() + 0x18, &secondPointer,
                        sizeof(secondPointer));
            std::memcpy(frameBlock.data() + 0x20, &outputPointer,
                        sizeof(outputPointer));
        } else {
            std::memcpy(frameBlock.data() + 0x18, &outputPointer,
                        sizeof(outputPointer));
        }
        // RenderContext carries the horizontal/vertical render scale at
        // +0x2c/+0x30. Zero makes the SDK blur helper divide by zero even
        // when the requested radius is zero.
        if (!transition) {
            std::memcpy(frameBlock.data() + 0x24, &textureScale,
                        sizeof(textureScale));
        }
        std::memcpy(frameBlock.data() + 0x2c, &textureScale,
                    sizeof(textureScale));
        std::memcpy(frameBlock.data() + 0x30, &textureScale,
                    sizeof(textureScale));
        // Plugin2DEffect::Render (Tannen.dll RVA 0x333890) copies five float*
        // arguments into RenderContext +0x38..+0x58.  They are all required:
        // the shared blur helper reads more than the projection/model-view
        // pair and produces NaN/Inf uniforms when either edge slot is null.
        const qint32 frameWidth = width;
        const qint32 frameHeight = height;
        if (transition) {
            // PluginVideoTransition::Render builds a different context: three
            // textures at +0x10/+0x18/+0x20, two matrices at +0x38/+0x40,
            // output dimensions at +0x48/+0x4c and sample/start/end at
            // +0x54/+0x58/+0x60. Native modules derive their normalized
            // progress from those integer positions.
            std::memcpy(frameBlock.data() + 0x38, &modelViewPointer,
                        sizeof(modelViewPointer));
            std::memcpy(frameBlock.data() + 0x40, &projectionPointer,
                        sizeof(projectionPointer));
            std::memcpy(frameBlock.data() + 0x48, &frameWidth, sizeof(frameWidth));
            std::memcpy(frameBlock.data() + 0x4c, &frameHeight, sizeof(frameHeight));
            constexpr qint32 transitionStart = 0;
            constexpr qint32 transitionEnd = 1000000;
            const qint32 transitionFrame = qBound(
                transitionStart,
                qRound(qBound(0.0f, transitionProgress, 1.0f) * transitionEnd),
                transitionEnd);
            std::memcpy(frameBlock.data() + 0x54, &transitionFrame,
                        sizeof(transitionFrame));
            std::memcpy(frameBlock.data() + 0x58, &transitionStart,
                        sizeof(transitionStart));
            std::memcpy(frameBlock.data() + 0x5c, &transitionEnd,
                        sizeof(transitionEnd));
            std::memcpy(frameBlock.data() + 0x60, &transitionEnd,
                        sizeof(transitionEnd));
        } else {
            const std::array<const float*, 5> transforms {
                modelViewPointer, projectionPointer, modelViewPointer,
                projectionPointer, modelViewPointer
            };
            std::memcpy(frameBlock.data() + 0x38, transforms.data(),
                        sizeof(transforms));
            std::memcpy(frameBlock.data() + 0x60, &frameWidth, sizeof(frameWidth));
            std::memcpy(frameBlock.data() + 0x64, &frameHeight, sizeof(frameHeight));
        }
        void* framePointer = frameBlock.data();
        std::memcpy(runtime->apiBlock().data() + 0x20, &framePointer,
                    sizeof(framePointer));

        m_scratchPool.releaseAll();
        m_scratchVboPool.releaseAll();
        m_scratchRenderbufferPool.releaseAll();
        RenderInvocation invocation {&record, &parameterValues, &m_scratchPool,
                                     &m_scratchVboPool, &m_scratchRenderbufferPool,
                                     width, height};
        RenderInvocation* previousInvocation = g_renderInvocation;
        g_renderInvocation = &invocation;
        if (qEnvironmentVariableIsSet("OPENVEGAS_HFPL_RENDER_DIAGNOSTIC_STUBS")) {
            resetNativePluginDiagnosticServiceOffset();
        }
        const int notifyResult = runtime->notify(10);
        if (trace) {
            std::fprintf(stderr,
                         "[hfpl-render] notify=%d fault=0x%08lx module=%s rva=0x%llx access=0x%llx\n",
                         notifyResult, runtime->lastFaultCode(),
                         runtime->lastFaultModulePath().toUtf8().constData(),
                         static_cast<unsigned long long>(runtime->lastFaultInstructionRva()),
                         static_cast<unsigned long long>(runtime->lastFaultAccessAddress()));
            std::fflush(stderr);
            const QString serviceTrace = nativePluginDiagnosticTrace();
            if (!serviceTrace.isEmpty()) {
                std::fprintf(stderr, "[hfpl-render] services %s\n",
                             serviceTrace.toUtf8().constData());
                std::fflush(stderr);
            }
        }
        g_renderInvocation = previousInvocation;
        m_scratchPool.releaseAll();
        m_scratchVboPool.releaseAll();
        m_scratchRenderbufferPool.releaseAll();
        bool ok = notifyResult == 1 && runtime->lastFaultCode() == 0;
        if (!ok) {
            qWarning().noquote()
                << QStringLiteral("Native HFPL Notify(10) failed for %1: result=%2, "
                                  "fault=0x%3, instructionRva=0x%4, %5")
                       .arg(record.filePath)
                       .arg(notifyResult)
                       .arg(quint32(runtime->lastFaultCode()), 8, 16, QLatin1Char('0'))
                       .arg(runtime->lastFaultInstructionRva(), 0, 16)
                       .arg(runtime->errorString());
        }
        if (ok) {
            // Multi-pass plugins leave their last scratch framebuffer bound.
            // They may also switch the current WGL context. Restore both host
            // objects before reading the effect result.
            ok = m_context.makeCurrent(m_surface.data()) && output.bind();
            if (!ok) {
                qWarning().noquote() << "Native HFPL could not restore the host framebuffer for"
                                     << record.filePath;
            }
        }
        if (ok) {
            QImage rendered(image.size(), QImage::Format_RGBA8888);
            gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
            gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
            gl->glPixelStorei(GL_PACK_SKIP_ROWS, 0);
            gl->glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
            gl->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                             rendered.bits());
            ok = gl->glGetError() == GL_NO_ERROR;
            if (!ok) {
                qWarning().noquote() << "Native HFPL readback reported an OpenGL error for"
                                     << record.filePath;
            }
            if (ok) {
                image = rendered;
            }
        }
        // Multi-pass plugins can leave work for scratch targets queued after
        // the final output draw. The render worker may destroy this context as
        // soon as render() returns, so complete the frame before any texture or
        // context lifetime ends (required by the NVIDIA path used by ChromaKey).
        gl->glFinish();
        gl->glDeleteTextures(1, &inputTexture);
        if (secondTexture != 0) {
            gl->glDeleteTextures(1, &secondTexture);
        }
        output.release();
        if (trace) {
            std::fprintf(stderr, "[hfpl-render] end ok=%d gl=0x%x\n", ok ? 1 : 0,
                         unsigned(gl->glGetError()));
            std::fflush(stderr);
        }
        return ok;
#endif
    }

private:
    bool ensureContext()
    {
        if (m_ready) {
            return true;
        }
        if (!QGuiApplication::instance()) {
            return false;
        }
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(4, 1);
        format.setProfile(QSurfaceFormat::CoreProfile);
        m_context.setFormat(format);
        if (!m_context.create()) {
            qWarning() << "Unable to create an OpenGL 4.1 context for native HFPL rendering";
            return false;
        }
        QGuiApplication* application = qobject_cast<QGuiApplication*>(
            QGuiApplication::instance());
        if (!application) {
            return false;
        }
        const QSurfaceFormat actualFormat = m_context.format();
        const auto createSurface = [this, actualFormat]() {
            auto* surface = new QOffscreenSurface;
            surface->setFormat(actualFormat);
            surface->create();
            m_surface = surface;
        };
        if (QThread::currentThread() == application->thread()) {
            createSurface();
        } else if (!QMetaObject::invokeMethod(application, createSurface,
                                               Qt::BlockingQueuedConnection)) {
            return false;
        }
        m_ready = m_surface && m_surface->isValid()
                  && m_context.makeCurrent(m_surface.data());
        if (!m_ready) {
            qWarning() << "Unable to create or activate the GUI-owned HFPL offscreen surface";
        }
        return m_ready;
    }

    NativePluginRuntime* runtimeFor(const QString& id, const ModuleRecord& record)
    {
        const QFileInfo file(record.filePath);
        const QString cacheKey = QStringLiteral("%1\n%2\n%3/%4")
                                     .arg(id, file.absoluteFilePath())
                                     .arg(file.size())
                                     .arg(file.lastModified().toMSecsSinceEpoch());
        const auto existing = m_runtimes.find(cacheKey);
        if (existing != m_runtimes.end()) {
            return existing.value().get();
        }
        auto runtime = std::make_shared<NativePluginRuntime>();
        if (!runtime->load(record.filePath, record.dependencyDirectory)) {
            qWarning().noquote() << "Unable to load native HFPL module:"
                                 << runtime->errorString();
            return nullptr;
        }
        if (!runtime->enableRenderingCompatibility()) {
            qWarning().noquote() << "Unable to load native HFPL module:"
                                 << runtime->errorString();
            return nullptr;
        }
        // Tannen::PluginFile::CreateAPI copies the host renderer capability
        // byte to tagBiffAPI+0x28. Notify(8) forwards it to the effect's GL
        // resource initializer. This renderer always creates a 4.1 core
        // context, so advertise that path; leaving the byte zero makes newer
        // shader wrappers keep a null render entry and fault in Notify(10).
        runtime->apiBlock()[0x28] = 1;
        if (qEnvironmentVariableIsSet("OPENVEGAS_HFPL_RENDER_DIAGNOSTIC_STUBS")) {
            installNativePluginDiagnosticStubs(runtime->apiBlock());
        }
        putService(runtime->apiBlock(), 0x4a8,
                   reinterpret_cast<void*>(&createVertexArray));
        putService(runtime->apiBlock(), 0x4b0,
                   reinterpret_cast<void*>(&getScratchVbo));
        putService(runtime->apiBlock(), 0x4b8,
                   reinterpret_cast<void*>(&clearReservedVbo));
        // CreateAPI.c in the recovered Tannen code fixes these byte offsets.
        // FloatValue and AngleValue have the same ABI, as do IntValue and
        // ComboBoxValue. The third value argument is frame+0x70; animation has
        // already been resolved by RenderManager before this call.
        putService(runtime->apiBlock(), 0xb0,
                   reinterpret_cast<void*>(&integerParameter));
        putService(runtime->apiBlock(), 0xb8,
                   reinterpret_cast<void*>(&scalarParameter));
        putService(runtime->apiBlock(), 0xc0,
                   reinterpret_cast<void*>(&booleanParameter));
        putService(runtime->apiBlock(), 0xc8,
                   reinterpret_cast<void*>(&integerParameter));
        putService(runtime->apiBlock(), 0xd0,
                   reinterpret_cast<void*>(&point2dParameter));
        putService(runtime->apiBlock(), 0xd8,
                   reinterpret_cast<void*>(&scalarParameter));
        putService(runtime->apiBlock(), 0xe0,
                   reinterpret_cast<void*>(&colorParameter));
        putService(runtime->apiBlock(), 0xe8,
                   reinterpret_cast<void*>(&layerIdParameter));
        putService(runtime->apiBlock(), 0x4e8,
                   reinterpret_cast<void*>(&integerParameter));
        putService(runtime->apiBlock(), 0xf0,
                   reinterpret_cast<void*>(&layerInfo));
        putService(runtime->apiBlock(), 0x100,
                   reinterpret_cast<void*>(&layerTexture));
        putService(runtime->apiBlock(), 0x108,
                   reinterpret_cast<void*>(&timelineInfo));
        putService(runtime->apiBlock(), 0x118,
                   reinterpret_cast<void*>(&sourceTexture));
        putService(runtime->apiBlock(), 0x128,
                   reinterpret_cast<void*>(&layerInfo));
        putService(runtime->apiBlock(), 0x130,
                   reinterpret_cast<void*>(&layerTextureV2));
        putService(runtime->apiBlock(), 0x138,
                   reinterpret_cast<void*>(&activeCamera));
        putService(runtime->apiBlock(), 0x140,
                   reinterpret_cast<void*>(&cameraInfo));
        putService(runtime->apiBlock(), 0x148,
                   reinterpret_cast<void*>(&motionBlurInfo));
        putService(runtime->apiBlock(), 0x158,
                   reinterpret_cast<void*>(&activeLights));
        putService(runtime->apiBlock(), 0xf8,
                   reinterpret_cast<void*>(&scratchTexture));
        putService(runtime->apiBlock(), 0x120,
                   reinterpret_cast<void*>(&clearScratchTexture));
        putService(runtime->apiBlock(), 0x178,
                   reinterpret_cast<void*>(&notifyProgress));
        putService(runtime->apiBlock(), 0x1a0,
                   reinterpret_cast<void*>(&setBooleanParameter));
        putService(runtime->apiBlock(), 0x278,
                   reinterpret_cast<void*>(&setStringParameter));
        putService(runtime->apiBlock(), 0x1e8,
                   reinterpret_cast<void*>(&numberOfKeyframes));
        putService(runtime->apiBlock(), 0x350,
                   reinterpret_cast<void*>(&hostOptions));
        putService(runtime->apiBlock(), 0x2c8,
                   reinterpret_cast<void*>(&redrawCustomUi));
        putService(runtime->apiBlock(), 0x268,
                   reinterpret_cast<void*>(&stringParameterLength));
        putService(runtime->apiBlock(), 0x270,
                   reinterpret_cast<void*>(&stringParameter));
        putService(runtime->apiBlock(), 0x340,
                   reinterpret_cast<void*>(&point3dParameter));
        putService(runtime->apiBlock(), 0x378,
                   reinterpret_cast<void*>(&point3dParameter));
        putService(runtime->apiBlock(), 0x2f8,
                   reinterpret_cast<void*>(&layerDepthTexture));
        putService(runtime->apiBlock(), 0x2e8,
                   reinterpret_cast<void*>(&numberOfTextOutlineVertices));
        putService(runtime->apiBlock(), 0x2f0,
                   reinterpret_cast<void*>(&textOutlinePoints));
        putService(runtime->apiBlock(), 0x2d0,
                   reinterpret_cast<void*>(&numberOfMasks));
        putService(runtime->apiBlock(), 0x2d8,
                   reinterpret_cast<void*>(&maskInfo));
        putService(runtime->apiBlock(), 0x2e0,
                   reinterpret_cast<void*>(&maskPoints));
        putService(runtime->apiBlock(), 0x500,
                   reinterpret_cast<void*>(&getScratchRenderbuffer));
        putService(runtime->apiBlock(), 0x508,
                   reinterpret_cast<void*>(&clearReservedRenderbuffer));
        const int initializationResult = runtime->notify(8);
        if (initializationResult != 1 || runtime->lastFaultCode() != 0) {
            qWarning().noquote() << "Native HFPL Notify(8) failed:"
                                 << runtime->errorString();
            return nullptr;
        }
        // Notify(3) allocates the plugin's real per-effect instance and stores
        // it at API+0x10. Its layout is module-specific: simple effects use a
        // small frame counter, while scopes keep mutex/task/cache state there.
        // A generic zero block is therefore insufficient and can crash the
        // MSVC concurrency runtime during Notify(10).
        const int instanceResult = runtime->notify(3);
        void* instancePointer = nullptr;
        std::memcpy(&instancePointer, runtime->apiBlock().constData() + 0x10,
                    sizeof(instancePointer));
        if (runtime->lastFaultCode() != 0) {
            qWarning().noquote() << "Native HFPL Notify(3) failed:"
                                 << runtime->errorString();
            return nullptr;
        }
        if (instanceResult == 1 && instancePointer) {
            m_pluginInstances.insert(runtime.get());
        } else {
            // Older effects report message 3 as unsupported. They only read
            // the common sample/frame field, so retain the small legacy block
            // for the complete runtime lifetime instead of a stack temporary.
            auto state = std::make_shared<QByteArray>(0x100, '\0');
            instancePointer = state->data();
            std::memcpy(runtime->apiBlock().data() + 0x10, &instancePointer,
                        sizeof(instancePointer));
            m_fallbackInstanceStates.insert(runtime.get(), std::move(state));
        }
        NativePluginRuntime* result = runtime.get();
        m_runtimes.insert(cacheKey, std::move(runtime));
        return result;
    }

    QOpenGLContext m_context;
    QPointer<QOffscreenSurface> m_surface;
    QHash<QString, std::shared_ptr<NativePluginRuntime>> m_runtimes;
    QSet<NativePluginRuntime*> m_pluginInstances;
    QHash<NativePluginRuntime*, std::shared_ptr<QByteArray>> m_fallbackInstanceStates;
    ScratchTexturePool m_scratchPool;
    ScratchVboPool m_scratchVboPool;
    ScratchRenderbufferPool m_scratchRenderbufferPool;
    bool m_ready = false;
};

// QOpenGLContext and QOffscreenSurface are Qt objects and must not be
// constructed by the CRT's TLS initializer before QGuiApplication exists.
// Keep only a trivial pointer in TLS and create the renderer on first use.
thread_local std::unique_ptr<ThreadRenderer> g_threadRenderer;
thread_local std::unique_ptr<AudioThreadRenderer> g_audioThreadRenderer;
thread_local std::unique_ptr<BehaviorThreadRenderer> g_behaviorThreadRenderer;

} // namespace

void clearNativeEffectModules()
{
    QMutexLocker lock(&g_registryMutex);
    g_registry.clear();
}

void registerNativeEffectModule(const core::Identifier& id, const QString& filePath,
                                const QString& dependencyDirectory,
                                bool frameRenderingVerified,
                                const QVector<EffectParameterSpec>& parameters)
{
    QMutexLocker lock(&g_registryMutex);
    g_registry.insert(id.value(), {filePath, dependencyDirectory,
                                   frameRenderingVerified, false, false, false, false,
                                   parameters});
}

void registerNativeVideoTransitionModule(
    const core::Identifier& id, const QString& filePath,
    const QString& dependencyDirectory, bool renderingVerified,
    const QVector<EffectParameterSpec>& parameters)
{
    QMutexLocker lock(&g_registryMutex);
    g_registry.insert(id.value(), {filePath, dependencyDirectory,
                                   false, renderingVerified, false, false, false,
                                   parameters});
}

void registerNativeAudioEffectModule(
    const core::Identifier& id, const QString& filePath,
    const QString& dependencyDirectory, bool renderingVerified,
    const QVector<EffectParameterSpec>& parameters)
{
    QMutexLocker lock(&g_registryMutex);
    g_registry.insert(id.value(), {filePath, dependencyDirectory,
                                   false, false, renderingVerified, false, false,
                                   parameters});
}

void registerNativeAudioTransitionModule(
    const core::Identifier& id, const QString& filePath,
    const QString& dependencyDirectory, bool renderingVerified,
    const QVector<EffectParameterSpec>& parameters)
{
    QMutexLocker lock(&g_registryMutex);
    g_registry.insert(id.value(), {filePath, dependencyDirectory,
                                   false, false, false, renderingVerified, false,
                                   parameters});
}

void registerNativeBehaviorModule(
    const core::Identifier& id, const QString& filePath,
    const QString& dependencyDirectory, bool renderingVerified,
    const QVector<EffectParameterSpec>& parameters)
{
    QMutexLocker lock(&g_registryMutex);
    g_registry.insert(id.value(), {filePath, dependencyDirectory,
                                   false, false, false, false,
                                   renderingVerified, parameters});
}

bool nativeEffectFrameRenderingVerified(const core::Identifier& id)
{
    QMutexLocker lock(&g_registryMutex);
    return g_registry.value(id.value()).frameRenderingVerified;
}

bool nativeVideoTransitionRenderingVerified(const core::Identifier& id)
{
    QMutexLocker lock(&g_registryMutex);
    return g_registry.value(id.value()).transitionRenderingVerified;
}

bool nativeAudioEffectRenderingVerified(const core::Identifier& id)
{
    QMutexLocker lock(&g_registryMutex);
    return g_registry.value(id.value()).audioRenderingVerified;
}

bool nativeAudioTransitionRenderingVerified(const core::Identifier& id)
{
    QMutexLocker lock(&g_registryMutex);
    return g_registry.value(id.value()).audioTransitionRenderingVerified;
}

bool nativeBehaviorRenderingVerified(const core::Identifier& id)
{
    QMutexLocker lock(&g_registryMutex);
    return g_registry.value(id.value()).behaviorRenderingVerified;
}

bool nativeBehaviorSimulationRenderingVerified(const core::Identifier& id)
{
    QMutexLocker lock(&g_registryMutex);
    const auto it = g_registry.constFind(id.value());
    return it != g_registry.constEnd() && isVerifiedSimulationBehavior(it.value());
}

bool evaluateNativeBehavior(NativeBehaviorResult& result, int timelineFrame,
                            int localFrame, int layerDurationFrames,
                            int canvasWidth, int canvasHeight, double frameRate,
                            const core::Identifier& id,
                            const QStringList& parameterValues,
                            const composition::Composition* composition,
                            const core::Identifier& sourceLayerId)
{
    ModuleRecord record;
    {
        QMutexLocker lock(&g_registryMutex);
        const auto it = g_registry.constFind(id.value());
        if (it == g_registry.constEnd() || !it->behaviorRenderingVerified) {
            return false;
        }
        record = it.value();
    }
    if (!g_behaviorThreadRenderer) {
        g_behaviorThreadRenderer = std::make_unique<BehaviorThreadRenderer>();
    }
    return g_behaviorThreadRenderer->evaluate(
        result, timelineFrame, localFrame, layerDurationFrames,
        canvasWidth, canvasHeight, frameRate, true, id.value(), record,
        parameterValues, composition, sourceLayerId);
}

bool evaluateNativeBehaviorFrame(NativeBehaviorResult& result, int timelineFrame,
                                 int localFrame, int layerDurationFrames,
                                 int canvasWidth, int canvasHeight, double frameRate,
                                 const core::Identifier& id,
                                 const QStringList& parameterValues,
                                 const composition::Composition* composition,
                                 const core::Identifier& sourceLayerId)
{
    ModuleRecord record;
    {
        QMutexLocker lock(&g_registryMutex);
        const auto it = g_registry.constFind(id.value());
        if (it == g_registry.constEnd() || !it->behaviorRenderingVerified) {
            return false;
        }
        record = it.value();
    }
    if (!g_behaviorThreadRenderer) {
        g_behaviorThreadRenderer = std::make_unique<BehaviorThreadRenderer>();
    }
    return g_behaviorThreadRenderer->evaluate(
        result, timelineFrame, localFrame, layerDurationFrames,
        canvasWidth, canvasHeight, frameRate, false, id.value(), record,
        parameterValues, composition, sourceLayerId);
}

bool simulateNativeBehaviorStack(
    NativeBehaviorResult& result, int timelineFrame, int localFrame,
    int layerDurationFrames, int canvasWidth, int canvasHeight, double frameRate,
    const QVector<NativeBehaviorRequest>& behaviors,
    const composition::Composition* composition,
    const core::Identifier& sourceLayerId)
{
    QVector<BehaviorStackEntry> entries;
    {
        QMutexLocker lock(&g_registryMutex);
        entries.reserve(behaviors.size());
        for (const NativeBehaviorRequest& behavior : behaviors) {
            const auto it = g_registry.constFind(behavior.id.value());
            if (it == g_registry.constEnd()
                || !isVerifiedSimulationBehavior(it.value())) {
                continue;
            }
            entries.push_back({behavior.id.value(), it.value(),
                               behavior.parameterValues});
        }
    }
    if (entries.isEmpty()) return false;
    if (!g_behaviorThreadRenderer) {
        g_behaviorThreadRenderer = std::make_unique<BehaviorThreadRenderer>();
    }
    return g_behaviorThreadRenderer->simulateStack(
        result, timelineFrame, localFrame, layerDurationFrames,
        canvasWidth, canvasHeight, frameRate, entries, composition, sourceLayerId);
}

bool applyNativeEffectToImage(QImage& image, const core::Identifier& id,
                              const QStringList& parameterValues)
{
    ModuleRecord record;
    {
        QMutexLocker lock(&g_registryMutex);
        const auto it = g_registry.constFind(id.value());
        if (it == g_registry.constEnd() || !it->frameRenderingVerified) {
            qWarning().noquote() << "Native HFPL module is not registered for rendering:"
                                 << id.value();
            return false;
        }
        record = it.value();
    }
    if (!g_threadRenderer) {
        g_threadRenderer = std::make_unique<ThreadRenderer>();
    }
    return g_threadRenderer->render(image, id.value(), record, parameterValues);
}

bool applyNativeVideoTransition(QImage& output, const QImage& from, const QImage& to,
                                float progress, const core::Identifier& id,
                                const QStringList& parameterValues)
{
    ModuleRecord record;
    {
        QMutexLocker lock(&g_registryMutex);
        const auto it = g_registry.constFind(id.value());
        if (it == g_registry.constEnd() || !it->transitionRenderingVerified) {
            qWarning().noquote()
                << "Native HFPL video transition is not registered for rendering:"
                << id.value();
            return false;
        }
        record = it.value();
    }
    if (from.isNull() || to.isNull()) {
        return false;
    }
    output = from.convertToFormat(QImage::Format_RGBA8888);
    if (!g_threadRenderer) {
        g_threadRenderer = std::make_unique<ThreadRenderer>();
    }
    return g_threadRenderer->render(output, id.value(), record, parameterValues,
                                    &to, progress);
}

bool applyNativeAudioEffect(QVector<qint16>& interleavedSamples, int channels,
                            int sampleRate, qint64 startSample,
                            const core::Identifier& id,
                            const QStringList& parameterValues)
{
    ModuleRecord record;
    {
        QMutexLocker lock(&g_registryMutex);
        const auto it = g_registry.constFind(id.value());
        if (it == g_registry.constEnd() || !it->audioRenderingVerified) return false;
        record = it.value();
    }
    if (!g_audioThreadRenderer) {
        g_audioThreadRenderer = std::make_unique<AudioThreadRenderer>();
    }
    return g_audioThreadRenderer->processEffect(
        interleavedSamples, channels, sampleRate, startSample, id.value(), record,
        parameterValues);
}

bool applyNativeAudioTransition(QVector<qint16>& output,
                                const QVector<qint16>& from,
                                const QVector<qint16>& to, int channels,
                                qint32 sampleOffset, qint32 totalTransitionSamples,
                                const core::Identifier& id,
                                const QStringList& parameterValues)
{
    ModuleRecord record;
    {
        QMutexLocker lock(&g_registryMutex);
        const auto it = g_registry.constFind(id.value());
        if (it == g_registry.constEnd()
            || !it->audioTransitionRenderingVerified) return false;
        record = it.value();
    }
    if (!g_audioThreadRenderer) {
        g_audioThreadRenderer = std::make_unique<AudioThreadRenderer>();
    }
    return g_audioThreadRenderer->processTransition(
        output, from, to, channels, sampleOffset, totalTransitionSamples,
        id.value(), record, parameterValues);
}

void releaseNativeEffectThreadRenderer()
{
    g_threadRenderer.reset();
    g_audioThreadRenderer.reset();
    g_behaviorThreadRenderer.reset();
}

} // namespace plugin
} // namespace openvegas
