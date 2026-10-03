#pragma once

#include "composition/EditorSequence.h"
#include "composition/Layer.h"

#include <QByteArray>
#include <QSize>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

#include <cmath>
#include <memory>

namespace openvegas {
namespace composition {

// CompositionAsset/<RenderSettings Version="2">: the shot's fog and motion
// blur. Motion blur applies to layers whose MotionBlurOn is set: each is
// rendered at sub-frame times across the shutter and averaged. Fog fades 3D
// layers towards fogColor by their distance from the camera (model3d::Fog;
// FogFalloff 0/1/2 = Linear, Exponential, Exponential²). The defaults are the
// reference's own for a new shot.
struct CompositionRenderSettings
{
    bool fogEnabled = false;
    double fogNearDistance = 900.0;
    double fogFarDistance = 2000.0;
    double fogDensity = 1.0;
    QColor fogColor = Qt::black;
    int fogFalloff = 0;
    bool motionBlurEnabled = true;
    double shutterAngle = 180.0;   // degrees of a frame the shutter stays open
    double shutterPhase = -90.0;   // degrees from the frame time it opens at
    int maxNumOfSamples = 20;
    bool useAdaptiveSamples = true;

    bool operator==(const CompositionRenderSettings& o) const
    {
        return fogEnabled == o.fogEnabled && fogNearDistance == o.fogNearDistance
               && fogFarDistance == o.fogFarDistance && fogDensity == o.fogDensity
               && fogColor == o.fogColor && fogFalloff == o.fogFalloff
               && motionBlurEnabled == o.motionBlurEnabled && shutterAngle == o.shutterAngle
               && shutterPhase == o.shutterPhase && maxNumOfSamples == o.maxNumOfSamples
               && useAdaptiveSamples == o.useAdaptiveSamples;
    }
    bool operator!=(const CompositionRenderSettings& o) const { return !(*this == o); }
};

// Project/<ProjectSettings Version="9">, the Rendering tab of the reference's
// Project Settings dialog. Values are the reference's own: BPC 1000/1001/1002
// for 8-bit integer / 16-bit float / 32-bit float (FUN_14022e2a0), and an
// antialiasing mode 1..9 for 4x/8x/16x/32x MSAA, 8x/8xQ/16x/16xQ/32x CSAA
// (FUN_140268720). The defaults are the ones its Options fall back to.
struct ProjectRenderSettings
{
    int bitDepth = 1000;
    int antialiasingMode = 1;
    int reflectionMapSize = 512;
    int modelTextureMaxSize = 4096;
    int shadowMapSize = 2048;
    bool limitVideoDecodingTo8bit = false;
    bool useLinearColor = false;

    bool operator==(const ProjectRenderSettings& o) const
    {
        return bitDepth == o.bitDepth && antialiasingMode == o.antialiasingMode
               && reflectionMapSize == o.reflectionMapSize
               && modelTextureMaxSize == o.modelTextureMaxSize
               && shadowMapSize == o.shadowMapSize
               && limitVideoDecodingTo8bit == o.limitVideoDecodingTo8bit
               && useLinearColor == o.useLinearColor;
    }
    bool operator!=(const ProjectRenderSettings& o) const { return !(*this == o); }
};

// The .vegfx a project was opened from. Saving lays the model over it
// (project/VegfxMerge) instead of writing only what the model holds, which
// used to drop every material, shadow, text format and editor object the
// reference had stored. `assetIdByPath` keeps each media file's <ID>, so the
// assets and the layers pointing at them keep their identity too.
struct NativeProjectSource
{
    QByteArray document;
    QHash<QString, QString> assetIdByPath;   // cleaned media path -> <ID>
};

class Composition
{
public:
    Composition() = default;

    const core::Identifier& id() const { return m_id; }
    void setId(const core::Identifier& id) { if (id.isValid()) m_id = id; }

    const QString& name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    int width() const { return m_width; }
    int height() const { return m_height; }
    void setSize(int width, int height)
    {
        m_width = width;
        m_height = height;
    }

    // A frame rate written as a decimal (29.97, "59.940") as the fraction it
    // stands for: the NTSC family comes back as n * 1000 / 1001, a whole
    // rate as n / 1, anything else to the millisecond.
    static void frameRateFraction(double fps, int* numerator, int* denominator);

    int fpsNumerator() const { return m_fpsNumerator; }
    int fpsDenominator() const { return m_fpsDenominator; }
    void setFrameRate(int numerator, int denominator)
    {
        m_fpsNumerator = numerator;
        m_fpsDenominator = denominator;
    }

    double durationSeconds() const { return m_durationSeconds; }
    void setDurationSeconds(double seconds) { m_durationSeconds = seconds; }

    // AudioVideoSettings <PAR>: the reference's biff::project::AudioVideoSettings::PAR
    // (0 Square, 1 DV NTSC, 2 DV NTSC Wide, 3 DV PAL, 4 DV PAL Wide,
    // 5 HD Anamorphic 1080, 6 DVCPro HD, 7 Anamorphic 2:1, 8 Custom) and
    // <PARCustom>, the value a Custom one has.
    enum PixelAspect { SquarePixels = 0, DvNtsc, DvNtscWide, DvPal, DvPalWide, HdAnamorphic1080,
                       DvcProHd, Anamorphic2To1, CustomAspect, PixelAspectCount };
    int pixelAspect() const { return m_pixelAspect; }
    double customPixelAspect() const { return m_customPixelAspect; }
    void setPixelAspect(int aspect, double custom = 1.0)
    {
        m_pixelAspect = aspect >= 0 && aspect < PixelAspectCount ? aspect : SquarePixels;
        if (custom > 0.0) m_customPixelAspect = custom;
    }
    // Width of a pixel over its height (PARValue; the table of Project.dll).
    double pixelAspectValue() const { return pixelAspectValue(m_pixelAspect, m_customPixelAspect); }
    static double pixelAspectValue(int aspect, double custom)
    {
        switch (aspect) {
        case DvNtsc: return 10.0 / 11.0;
        case DvNtscWide: return 40.0 / 33.0;
        case DvPal: return 12.0 / 11.0;
        case DvPalWide: return 16.0 / 11.0;
        case HdAnamorphic1080: return 4.0 / 3.0;
        case DvcProHd: return 1.5;
        case Anamorphic2To1: return 2.0;
        case CustomAspect: return custom > 0.0 ? custom : 1.0;
        default: return 1.0;
        }
    }
    // The frame in square units - width x PAR by height - which is the space
    // layers live in: positions, plane and grade sizes, masks, the camera
    // (the reference creates a New Grade round(Width * PAR) wide). Rendered
    // frames are this space resampled to width x height pixels; the viewer
    // shows them at this shape.
    QSize displaySize() const
    {
        return QSize(qMax(1, int(std::lround(m_width * pixelAspectValue()))), qMax(1, m_height));
    }

    // AudioVideoSettings <AudioSampleRate>.
    int audioSampleRate() const { return m_audioSampleRate; }
    void setAudioSampleRate(int rate) { if (rate > 0) m_audioSampleRate = rate; }

    const QVector<Layer>& layers() const { return m_layers; }
    Layer& addLayer(const QString& name);
    // Puts a whole layer back at a given position. Undo needs this: removing a
    // layer and re-appending it would move it to the bottom of the stack.
    bool insertLayer(int index, const Layer& layer);
    bool removeLayer(int index);
    // Copy of the layer at index, for commands that have to restore it.
    Layer layerCopy(int index) const;
    void setLayers(const QVector<Layer>& layers);
    bool swapLayers(int a, int b);
    Layer& layerRef(int index);

    Clip* addClip(const QString& layerName, const core::Identifier& mediaId, double startSeconds,
                  double durationSeconds);
    Layer* layer(const QString& name);
    Clip* clipAt(int layerIndex, int clipIndex);

    EditorSequence& editorSequence() { return m_editorSequence; }
    const EditorSequence& editorSequence() const { return m_editorSequence; }

    CompositionRenderSettings& renderSettings() { return m_renderSettings; }
    const CompositionRenderSettings& renderSettings() const { return m_renderSettings; }

    // CompositionAsset's <CTI> (the shot's playhead) and its <In>/<Out> work
    // area, in frames. Out < 0 means the whole shot.
    long long currentFrame() const { return m_currentFrame; }
    void setCurrentFrame(long long frame) { m_currentFrame = qMax(0LL, frame); }
    long long workIn() const { return m_workIn; }
    long long workOut() const { return m_workOut; }
    void setWorkArea(long long in, long long out) { m_workIn = qMax(0LL, in); m_workOut = out; }

    // Project-level data, held by the root composition the project file is
    // read into: Project/<ID>, ProjectSettings and the document it came from.
    const core::Identifier& projectId() const { return m_projectId; }
    void setProjectId(const core::Identifier& id) { if (id.isValid()) m_projectId = id; }
    ProjectRenderSettings& projectSettings() { return m_projectSettings; }
    const ProjectRenderSettings& projectSettings() const { return m_projectSettings; }
    std::shared_ptr<const NativeProjectSource> nativeSource() const { return m_nativeSource; }
    void setNativeSource(std::shared_ptr<const NativeProjectSource> source)
    {
        m_nativeSource = std::move(source);
    }

    // CompositionAsset/<IsPrimary>: the shot VEGAS Pro gets back when it
    // hosts the project ("Set Primary Composite Shot"). At most one shot of a
    // project has it, and a project may have none.
    bool isPrimary() const { return m_primary; }
    void setPrimary(bool primary) { m_primary = primary; }

    // The project's other composite shots - its further CompositionAssets - in
    // the order of its asset list. A layer that nests one of them points at the
    // same object (Clip::nestedComposition), as the reference's AssetLayer
    // points at the CompositionAsset its <AssetID> names.
    const QVector<std::shared_ptr<Composition>>& compositeShots() const { return m_compositeShots; }
    void setCompositeShots(const QVector<std::shared_ptr<Composition>>& shots) { m_compositeShots = shots; }
    void addCompositeShot(const std::shared_ptr<Composition>& shot, int index = -1);
    // Index the shot had, or -1.
    int removeCompositeShot(const core::Identifier& id);
    std::shared_ptr<Composition> compositeShot(const core::Identifier& id) const;

    // <OpenCompositeShots>: the shots open as Editor tabs, in tab order, and
    // the one that was in front.
    const QStringList& openShotIds() const { return m_openShotIds; }
    const QString& activeShotId() const { return m_activeShotId; }
    void setOpenShots(const QStringList& ids, const QString& active)
    {
        m_openShotIds = ids;
        m_activeShotId = active;
    }

    void clear();
    bool isEmpty() const { return m_layers.isEmpty(); }

private:
    core::Identifier m_id = core::Identifier(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    QString m_name = QStringLiteral("Untitled");
    int m_width = 1920;
    int m_height = 1080;
    int m_fpsNumerator = 30;
    int m_fpsDenominator = 1;
    double m_durationSeconds = 10.0;
    int m_pixelAspect = SquarePixels;
    double m_customPixelAspect = 1.0;
    int m_audioSampleRate = 48000;
    QVector<Layer> m_layers;
    EditorSequence m_editorSequence;
    CompositionRenderSettings m_renderSettings;
    long long m_currentFrame = 0;
    long long m_workIn = 0;
    long long m_workOut = -1;
    core::Identifier m_projectId = core::Identifier(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    ProjectRenderSettings m_projectSettings;
    std::shared_ptr<const NativeProjectSource> m_nativeSource;
    bool m_primary = false;
    QVector<std::shared_ptr<Composition>> m_compositeShots;
    QStringList m_openShotIds;
    QString m_activeShotId;
};

} // namespace composition
} // namespace openvegas
