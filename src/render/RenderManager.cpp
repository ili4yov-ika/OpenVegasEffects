#include "render/RenderManager.h"
#include "composition/CompositionState.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>

#include <QMetaType>
#include <QFont>
#include <QPainter>
#include <QSettings>
#include <QPainterPath>
#include <QSet>
#include <QTransform>
#include <QVarLengthArray>

#include "core/Identifier.h"
#include "core/Log.h"
#include "composition/TextStyle.h"
#include "composition/Transition.h"
#include "plugin/EffectRender.h"
#include "plugin/NativeEffectRender.h"
#include "render/TextGeometry.h"
#include "render/TextRender.h"

namespace openvegas {
namespace render {

namespace {

// Layer blend modes. The reference does these in GLSL - Project.dll carries the
// shader source, whose locals name the whole family (screen, overlay, darken,
// lighten, colorburn, dodge, hardlight, soft, diff, excl, divide, subtract,
// plus an HSL group for hue/sat/colour/luminosity). Qt implements most of the
// same set natively, so the port maps onto QPainter rather than reproducing any
// formula.
//
// Modes with no QPainter equivalent - Divide, Subtract and the HSL four - are
// deliberately absent from the picker instead of being listed and ignored;
// anything unrecognised falls back to plain source-over.
QPainter::CompositionMode compositionModeFor(const QString& mode)
{
    static const QHash<QString, QPainter::CompositionMode> kModes = {
        {QStringLiteral("None"),        QPainter::CompositionMode_SourceOver},
        {QStringLiteral("Normal"),      QPainter::CompositionMode_SourceOver},
        {QStringLiteral("Add"),         QPainter::CompositionMode_Plus},
        {QStringLiteral("Screen"),      QPainter::CompositionMode_Screen},
        {QStringLiteral("Multiply"),    QPainter::CompositionMode_Multiply},
        {QStringLiteral("Overlay"),     QPainter::CompositionMode_Overlay},
        {QStringLiteral("Darken"),      QPainter::CompositionMode_Darken},
        {QStringLiteral("Lighten"),     QPainter::CompositionMode_Lighten},
        {QStringLiteral("Color Dodge"), QPainter::CompositionMode_ColorDodge},
        {QStringLiteral("Color Burn"),  QPainter::CompositionMode_ColorBurn},
        {QStringLiteral("Hard Light"),  QPainter::CompositionMode_HardLight},
        {QStringLiteral("Soft Light"),  QPainter::CompositionMode_SoftLight},
        {QStringLiteral("Difference"),  QPainter::CompositionMode_Difference},
        {QStringLiteral("Exclusion"),   QPainter::CompositionMode_Exclusion},
    };
    const auto it = kModes.constFind(mode);
    return it == kModes.constEnd() ? QPainter::CompositionMode_SourceOver : it.value();
}

// Motion blur works on the 2D transform the CPU path draws with.
bool hasMotion(const composition::Layer& layer)
{
    using P = composition::TransformProperty;
    for (P prop : {P::Position, P::Scale, P::Rotation})
        if (layer.transform.isAnimated(prop)) return true;
    return false;
}

// The layer as it stands at a fractional frame: each animated 2D property is
// interpolated between the frames either side and written back as static.
composition::Layer layerAtSubFrame(const composition::Layer& layer, double frame)
{
    using P = composition::TransformProperty;
    composition::Layer sampled = layer;
    const double whole = std::floor(frame);
    const double t = frame - whole;
    for (P prop : {P::Position, P::Scale, P::Rotation}) {
        for (int axis = 0; axis < composition::axisCount(prop); ++axis) {
            const composition::KeyFrameList* curve = layer.transform.curve(prop, axis);
            if (!curve || curve->isEmpty()) continue;
            const double a = layer.transform.valueAt(prop, axis, int(whole));
            const double b = layer.transform.valueAt(prop, axis, int(whole) + 1);
            *sampled.transform.curve(prop, axis) = composition::KeyFrameList();
            composition::setLayerTransformValue(sampled, prop, axis, a + (b - a) * t);
        }
    }
    return sampled;
}

// How far, in canvas pixels, the layer moves over one frame: its centre, plus
// what scaling and turning do at the corners of a canvas-sized layer.
double framePixelTravel(const composition::Layer& layer, int frame, const QSize& size)
{
    const composition::LayerTransform& t = layer.transform;
    const double radius = std::hypot(size.width(), size.height()) / 2.0;
    const QPointF move = t.positionAt(frame + 1) - t.positionAt(frame);
    const QPointF grow = t.scaleAt(frame + 1) - t.scaleAt(frame);
    const double turn = std::abs(t.rotationAt(frame + 1) - t.rotationAt(frame));
    return std::hypot(move.x(), move.y())
           + std::max(std::abs(grow.x()), std::abs(grow.y())) / 100.0 * radius
           + qDegreesToRadians(turn) * radius;
}

// Shutter samples for a layer with MotionBlurOn: the shutter opens
// shutterPhase degrees from the frame and stays open for shutterAngle degrees
// (the reference's 180/-90 spans a quarter frame either side). Adaptive
// sampling keeps successive samples about two pixels apart.
QVector<double> shutterOffsets(const composition::CompositionRenderSettings& settings,
                               double travelPerFrame)
{
    const double open = settings.shutterPhase / 360.0;
    const double span = settings.shutterAngle / 360.0;
    int samples = qMax(1, settings.maxNumOfSamples);
    if (settings.useAdaptiveSamples) {
        samples = qBound(1, int(std::ceil(travelPerFrame * span / 2.0)), samples);
    }
    QVector<double> offsets;
    offsets.reserve(samples);
    for (int s = 0; s < samples; ++s) offsets.append(open + span * (s + 0.5) / samples);
    return offsets;
}

// Linear dissolve in premultiplied space, so fading to or from a transparent
// side changes coverage rather than leaving the other picture fully opaque.
QImage dissolveImages(const QImage& from, const QImage& to, float progress)
{
    QImage a = from.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const QImage b = to.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                         .scaled(a.size());
    const int weight = qBound(0, qRound(progress * 256.0f), 256);
    for (int y = 0; y < a.height(); ++y) {
        uchar* out = a.scanLine(y);
        const uchar* in = b.constScanLine(y);
        for (int x = 0; x < a.width() * 4; ++x) {
            out[x] = uchar((out[x] * (256 - weight) + in[x] * weight) >> 8);
        }
    }
    return a.convertToFormat(QImage::Format_RGBA8888);
}

// What a Behavior module is told about its layer (plugin::NativeBehaviorLayer):
// the layer's own placement at `frame` - from the frame's centre, Y up, a
// positive rotation turning clockwise as the renderer draws it - and its
// extent as PluginBehaviorEffect::TransformationAtTime measures it.
plugin::NativeBehaviorLayer behaviorLayerAt(const composition::Layer& layer,
                                            const composition::Clip& clip,
                                            const composition::Effect* text, int frame,
                                            const QSize& canvas,
                                            const media::MediaManager* media)
{
    plugin::NativeBehaviorLayer info;
    info.composition = canvas;
    const composition::LayerTransform& t = layer.transform;
    const QPointF position = t.positionAt(frame);
    const QPointF scale = t.scaleAt(frame);
    QMatrix4x4 world;
    world.translate(float(position.x()), float(position.y()), float(t.positionZAt(frame)));
    world.translate(float(t.anchorPoint.x()), float(t.anchorPoint.y()));
    world.rotate(float(-t.rotationAt(frame)), 0.0f, 0.0f, 1.0f);
    world.translate(float(-t.anchorPoint.x()), float(-t.anchorPoint.y()));
    world.scale(float(scale.x() / 100.0), float(scale.y() / 100.0), 1.0f);
    std::copy(world.constData(), world.constData() + 16, info.world.begin());

    if (text) {
        // A text layer (type 6): a paragraph its box; point text the frame's
        // width, and its lines from the first one's height above the origin
        // down to the last one (the reference sums the heights of its text
        // tokens; the font size stands in for them here).
        QStringList values = text->parameterValues;
        for (int p = 0; p < values.size(); ++p) values[p] = text->parameterAt(p, frame).toString();
        const composition::TextStyle style = composition::textStyleFromParameters(values);
        if (style.textMode == composition::TextStyle::TextMode::Paragraph) {
            const QPointF centre = style.paragraphOffset;
            const QSizeF size = style.paragraphSize;
            info.bounds = {float(centre.x() - size.width() / 2.0), float(centre.y() - size.height() / 2.0),
                           float(centre.x() + size.width() / 2.0), float(centre.y() + size.height() / 2.0)};
        } else {
            const int lines = qMax(1, int(style.text.count(QLatin1Char('\n'))) + 1);
            info.bounds = {float(-canvas.width() / 2.0), float(-(lines - 1) * style.fontSize),
                           float(canvas.width() / 2.0), float(style.fontSize)};
        }
        return info;
    }
    // A footage layer (type 0) runs from 0 to its asset's size, a grade
    // layer (4) over the frame; a plane is a frame-sized asset.
    QSize size;
    if (layer.kind == composition::LayerKind::Grade || layer.kind == composition::LayerKind::Plane) {
        size = canvas;
    } else if (clip.nestedComposition) {
        size = clip.nestedComposition->displaySize();
    } else if (media && clip.mediaId.isValid()) {
        size = media->assetById(clip.mediaId).frameSize();
    }
    if (size.isValid()) info.bounds = {0.0f, 0.0f, float(size.width()), float(size.height())};
    return info;
}

} // namespace

RenderWorker::RenderWorker(QObject* parent)
    : QObject(parent)
{
}

RenderWorker::~RenderWorker() = default;

void RenderWorker::setComposition(std::shared_ptr<composition::Composition> comp)
{
    m_composition = std::move(comp);
}

void RenderWorker::setMediaManager(std::shared_ptr<media::MediaManager> media)
{
    m_mediaManager = std::move(media);
}

QColor RenderWorker::clipColor(const core::Identifier& mediaId) const
{
    // Deterministic, distinct hue per media asset.
    const size_t h = qHash(mediaId.value());
    const int hue = static_cast<int>(h % 360);
    return QColor::fromHsv(hue, 160, 200);
}

// Frame of a video asset for a time inside the clip. The worker never decodes:
// it reads what the GUI thread has already put in the cache and leaves a
// request behind for anything missing - QMediaPlayer cannot be driven from
// here. Time is quantised to the composition's frame rate so scrubbing lands on
// the same keys instead of asking for a new decode every pixel.
QImage RenderWorker::loadVideoFrame(const core::Identifier& mediaId, double localSeconds) const
{
    if (!m_mediaManager) {
        return QImage();
    }
    const media::MediaAsset asset = m_mediaManager->assetById(mediaId);
    if (asset.isImageSequence()) {
        // Stills are read here, on the render thread; no decoder involved.
        const QString path = asset.sequenceFileAt(localSeconds);
        if (QImage* cached = m_sequenceFrames.object(path)) return *cached;
        QImage frame(path);
        if (frame.isNull()) return QImage();
        frame = frame.convertToFormat(QImage::Format_RGBA8888);
        m_sequenceFrames.insert(path, new QImage(frame),
                                qMax(1, int(frame.sizeInBytes() / 1024)));
        return frame;
    }
    if (asset.kind() != media::MediaKind::Video) {
        return QImage();
    }
    // The position in the source, in milliseconds (MediaManager::videoFrame);
    // an overridden frame rate picks the frame its own count reaches.
    const int sourceMilliseconds = qMax(0, int(std::lround(asset.sourceSecondsAt(localSeconds) * 1000.0)));
    const QImage frame = m_mediaManager->videoFrame(mediaId, sourceMilliseconds);
    if (frame.isNull()) m_frameComplete = false;
    if (!frame.isNull()) {
        return frame;
    }
    // Not decoded yet. Hold the last picture this asset produced rather than
    // returning nothing: the caller's fallback is a coloured placeholder, and
    // playback asks for frames faster than they can be decoded, so that
    // placeholder would show through over the footage for most of a playthrough.
    return m_mediaManager->lastVideoFrame(mediaId);
}

QString RenderWorker::clipLabel(const core::Identifier& mediaId) const
{
    if (m_mediaManager) {
        const media::MediaAsset asset = m_mediaManager->assetById(mediaId);
        if (asset.isValid()) {
            return asset.fileName();
        }
    }
    return QStringLiteral("clip: %1").arg(mediaId.value());
}

QImage RenderWorker::loadImageMedia(const core::Identifier& mediaId) const
{
    const QString key = mediaId.value();
    const auto it = m_imageCache.constFind(key);
    if (it != m_imageCache.constEnd()) {
        return it.value();
    }

    QImage img;
    if (m_mediaManager) {
        const media::MediaAsset asset = m_mediaManager->assetById(mediaId);
        if (asset.kind() == media::MediaKind::Image) {
            img.load(asset.filePath());
        }
    }
    m_imageCache.insert(key, img);
    return img;
}

// An audio clip has no picture. Without this it fell through to the placeholder
// below and filled the whole canvas, and since layers composite in order an
// audio layer at the top hid every image under it.
// Composition frame number for a time on the timeline; keyframes are addressed
// by frame, not by seconds.
int RenderWorker::frameForTime(double timeSeconds) const
{
    if (!m_composition || m_composition->fpsDenominator() <= 0) {
        return 0;
    }
    const double fps = static_cast<double>(m_composition->fpsNumerator())
                       / m_composition->fpsDenominator();
    return static_cast<int>(qRound(timeSeconds * fps));
}

// The text effect carried by a text layer, or nullptr for anything else.
const composition::Effect* RenderWorker::textEffectOf(const composition::Clip& clip)
{
    for (const composition::Effect& fx : clip.effects) {
        if (fx.pluginId.value() == QStringLiteral("text")
            || fx.name.compare(QLatin1String("Text"), Qt::CaseInsensitive) == 0) {
            return fx.parameterValues.isEmpty() ? nullptr : &fx;
        }
    }
    return nullptr;
}

bool RenderWorker::contributesVideo(const core::Identifier& mediaId) const
{
    if (!m_mediaManager) {
        return true;
    }
    const media::MediaAsset asset = m_mediaManager->assetById(mediaId);
    return asset.kind() != media::MediaKind::Audio;
}

QImage RenderWorker::renderClip(const composition::Layer& layer, const composition::Clip& clip,
                                double localTimeSeconds, const QSize& canvasSize, int frame) const
{
    // A 3D layer is seen through the shot's camera. Text that Geometry
    // modules turned into a mesh is already 3D and keeps the mesh path below.
    if (layer.dimension == composition::LayerDimension::ThreeD
        && layer.kind != composition::LayerKind::Model3D) {
        const bool meshText = textEffectOf(clip)
            && std::any_of(clip.effects.cbegin(), clip.effects.cend(), [](const composition::Effect& fx) {
                   return fx.enabled && plugin::nativeGeometryRenderingVerified(fx.pluginId);
               });
        if (!meshText) return renderClipIn3D(layer, clip, localTimeSeconds, canvasSize, frame);
    }

    const composition::LayerTransform& transform = layer.transform;
    QImage canvas(canvasSize, QImage::Format_RGBA8888);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // A Plane is its own content: a solid fill placed by the layer transform,
    // so it needs neither media nor the placeholder below.
    if (layer.kind == composition::LayerKind::Plane) {
        const QPointF scalePct = transform.scaleAt(frame);
        const QPointF pos = transform.positionAt(frame);
        const QSizeF scaled(canvasSize.width() * scalePct.x() / 100.0,
                            canvasSize.height() * scalePct.y() / 100.0);
        painter.translate(canvasSize.width() / 2.0 + pos.x(),
                          canvasSize.height() / 2.0 - pos.y());
        const double angle = transform.rotationAt(frame);
        if (!qFuzzyIsNull(angle)) {
            painter.rotate(angle);
        }
        painter.fillRect(QRectF(QPointF(-scaled.width() / 2.0, -scaled.height() / 2.0), scaled),
                         layer.planeColor);
        return canvas;
    }

    // Video first: a decoded frame is used exactly like a still, so the whole
    // transform path below applies unchanged. A miss returns null and the clip
    // falls through to the placeholder, which now only happens before the very
    // first frame of a source has been decoded.
    QImage media;
    if (clip.nestedComposition) {
        // Render the child in its own square space (width x PAR by height),
        // then let the parent layer transform place it exactly like footage
        // or a still image. A pre-render holds the child's pixels, so it is
        // stretched to that shape.
        const QSize childSize = clip.nestedComposition->displaySize();
        media = loadPreRenderedFrame(*clip.nestedComposition, localTimeSeconds,
                                     QSize(qMax(1, clip.nestedComposition->width()),
                                           qMax(1, clip.nestedComposition->height())));
        if (!media.isNull() && media.size() != childSize) {
            media = media.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                        .scaled(childSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_RGBA8888);
        }
        if (media.isNull()) {
            RenderWorker nested;
            nested.setComposition(clip.nestedComposition);
            nested.setMediaManager(m_mediaManager);
            nested.m_preRenderDirectory = m_preRenderDirectory;
            media = nested.renderFrameImage(localTimeSeconds, childSize, true, true);
            if (!nested.m_frameComplete) m_frameComplete = false;
        }
    } else {
        media = loadVideoFrame(clip.mediaId, localTimeSeconds);
        if (media.isNull()) {
            media = loadImageMedia(clip.mediaId);
        }
        // Media Properties' alpha, levels and colour space overrides.
        if (!media.isNull() && m_mediaManager && clip.mediaId.isValid()) {
            const media::MediaAsset asset = m_mediaManager->assetById(clip.mediaId);
            if (asset.reinterpretsPixels()) media = asset.interpretFrame(media);
        }
    }
    if (!media.isNull()) {
        // Place the media by the layer transform instead of stretching it over
        // the canvas: scale is a percentage of the media's own size, position is
        // an offset from the composition centre with Y pointing up, and rotation
        // turns the layer about its anchor point.
        const QPointF scalePct = transform.scaleAt(frame);
        const QPointF pos = transform.positionAt(frame);
        // Footage with non-square pixels is as wide as its pixels say:
        // width x the asset's PAR, in the shot's square units.
        const double aspect = !clip.nestedComposition && m_mediaManager && clip.mediaId.isValid()
            ? m_mediaManager->assetById(clip.mediaId).pixelAspectValue() : 1.0;
        const QSizeF scaled(media.width() * aspect * scalePct.x() / 100.0,
                            media.height() * scalePct.y() / 100.0);
        const QPointF centre(canvasSize.width() / 2.0 + pos.x(),
                             canvasSize.height() / 2.0 - pos.y());
        painter.save();
        painter.translate(centre);
        const double angle = transform.rotationAt(frame);
        if (!qFuzzyIsNull(angle)) {
            painter.translate(transform.anchorPoint.x(), -transform.anchorPoint.y());
            painter.rotate(angle);
            painter.translate(-transform.anchorPoint.x(), transform.anchorPoint.y());
        }
        painter.drawImage(QRectF(QPointF(-scaled.width() / 2.0, -scaled.height() / 2.0), scaled),
                          media);
        painter.restore();
    } else if (const composition::Effect* text = textEffectOf(clip)) {
        // A text layer draws its string, not a placeholder block: it sits above
        // the footage, so an opaque fill here would hide everything below.
        //
        // Everything about how it looks comes from the layer's own style - the
        // font, the outline, the paragraph indents, the background plate - which
        // is what the reference's Text panel edits and what TextRender draws.
        QStringList values = text->parameterValues;
        for (int p = 0; p < values.size(); ++p) values[p] = text->parameterAt(p, frame).toString();
        const composition::TextStyle style = composition::textStyleFromParameters(values);
        if (style.text.isEmpty()) {
            return canvas; // nothing to draw, and no placeholder over the layers below
        }
        QVector<const composition::Effect*> geometryEffects;
        for (const composition::Effect& fx : clip.effects) {
            if (fx.enabled && plugin::nativeGeometryRenderingVerified(fx.pluginId)) {
                geometryEffects.append(&fx);
            }
        }
        if (!geometryEffects.isEmpty()) {
            painter.end();
            return renderTextGeometry(layer, clip, style, geometryEffects, canvasSize, frame);
        }
        // Text follows the same layer transform as any other content.
        const QPointF scalePct = transform.scaleAt(frame);
        const QPointF pos = transform.positionAt(frame);

        painter.translate(canvasSize.width() / 2.0 + pos.x(),
                          canvasSize.height() / 2.0 - pos.y());
        const double angle = transform.rotationAt(frame);
        if (!qFuzzyIsNull(angle)) {
            painter.rotate(angle);
        }
        painter.scale(scalePct.x() / 100.0, scalePct.y() / 100.0);
        // The text box is the frame, centred on the origin - where the reference
        // puts a text box's own centre.
        const QRectF box(-canvasSize.width() / 2.0, -canvasSize.height() / 2.0,
                         canvasSize.width(), canvasSize.height());
        bool hasGlyphBehavior = false;
        for (const composition::Effect& fx : clip.effects) {
            if (fx.enabled && plugin::nativeBehaviorSubObjectRenderingVerified(fx.pluginId)) {
                hasGlyphBehavior = true;
                break;
            }
        }
        render::GlyphModifier glyphModifier;
        if (hasGlyphBehavior) {
            glyphModifier = [&](QVector<render::GlyphRenderState>& glyphs) {
                plugin::NativeSubObjectResult result;
                result.transformations.resize(glyphs.size());
                result.clipValues.resize(glyphs.size());
                result.opacities.reserve(glyphs.size());
                for (const auto& glyph : glyphs) {
                    result.opacities.append(glyph.opacity);
                }
                const double fps = m_composition
                    ? double(m_composition->fpsNumerator())
                          / qMax(1, m_composition->fpsDenominator())
                    : 30.0;
                const int localFrame = qMax(0, frame - qRound(clip.startSeconds * fps));
                const int durationFrames = qMax(1, qRound(clip.durationSeconds * fps));
                for (const composition::Effect& fx : clip.effects) {
                    if (!fx.enabled
                        || !plugin::nativeBehaviorSubObjectRenderingVerified(fx.pluginId)) {
                        continue;
                    }
                    QStringList resolved;
                    for (int i = 0; i < fx.parameterValues.size(); ++i) {
                        resolved.append(fx.parameterAt(i, frame).toString());
                    }
                    plugin::evaluateNativeSubObjectBehavior(
                        result, frame, localFrame, durationFrames,
                        canvasSize.width(), canvasSize.height(), fps,
                        fx.pluginId, resolved, layer.id);
                }
                for (qsizetype i = 0; i < glyphs.size(); ++i) {
                    const float* matrix = result.transformations.at(i).constData();
                    if (qIsFinite(matrix[0]) && qIsFinite(matrix[1])
                        && qIsFinite(matrix[4]) && qIsFinite(matrix[5])
                        && qIsFinite(matrix[12]) && qIsFinite(matrix[13])) {
                        // Native text matrices use Y-up. Qt's text layout is
                        // Y-down, so reflect the off-diagonal terms as well
                        // as the vertical translation.
                        glyphs[i].transformation = QTransform(
                            double(matrix[0]), -double(matrix[1]),
                            -double(matrix[4]), double(matrix[5]),
                            double(matrix[12]), -double(matrix[13]));
                    }
                    const float nativeOpacity = result.opacities.at(i);
                    glyphs[i].opacity = qIsFinite(nativeOpacity)
                                            ? qBound(0.0f, nativeOpacity, 1.0f)
                                            : 1.0f;
                    const auto& nativeClip = result.clipValues.at(i);
                    glyphs[i].clipEnabled = false;
                    if (nativeClip.enabled) {
                        const float* values = nativeClip.values;
                        if (qIsFinite(values[0]) && qIsFinite(values[1])
                            && qIsFinite(values[2]) && qIsFinite(values[3])) {
                            glyphs[i].clipEnabled = true;
                            glyphs[i].clipRect = QRectF(values[0], values[3],
                                                        values[1] - values[0],
                                                        values[2] - values[3]);
                        }
                    }
                }
            };
        }
        render::drawStyledText(painter, box, style, glyphModifier);
    } else {
        // Placeholder: determined by media, animated by local clip time so playback
        // is visibly moving.
        const QColor color = clipColor(clip.mediaId);
        painter.fillRect(QRect(QPoint(0, 0), canvasSize), color.darker(120));

        const int bars = 16;
        const int inset = 40;
        const double phase = qSin(localTimeSeconds * 3.0) * 0.5 + 0.5;
        for (int i = 0; i < bars; ++i) {
            const int opacity = 30 + static_cast<int>(25.0 * phase * ((i % 3) + 1));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(color.lighter(150).red(), color.lighter(150).green(),
                                    color.lighter(150).blue(), opacity));
            painter.drawRect(inset + i * (canvasSize.width() - 2 * inset) / bars,
                             0, (canvasSize.width() - 2 * inset) / bars, canvasSize.height());
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QColor(255, 255, 255, 220));
        painter.drawText(QRect(0, 0, canvasSize.width(), canvasSize.height()),
                         Qt::AlignCenter, clipLabel(clip.mediaId));
    }

    return canvas;
}

// Runs the clip's enabled effects over the rendered picture. Parameters are
// resolved through Effect::parameterAt so a keyframed parameter uses its curve
// at this frame rather than the static default. Unknown ids (the "text" effect
// among them, which renderClip has already drawn) are simply not handled by
// applyEffectToImage and leave the image untouched.
void RenderWorker::applyClipEffects(QImage& image, const composition::Clip& clip, int frame) const
{
    if (image.isNull()) {
        return;
    }
    // When the frame is, for the effects that move by themselves.
    plugin::NativeFrameTime time;
    time.frameRate = m_composition && m_composition->fpsDenominator() > 0
        ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
    time.frame = frame;
    time.layerFrame = frame - frameForTime(clip.startSeconds);
    time.layerFrames = qMax(1, int(std::lround(clip.durationSeconds * time.frameRate)));
    for (const composition::Effect& fx : clip.effects) {
        // Transitions act on the clip's edges, in renderFrameImage.
        if (!fx.enabled || fx.isTransition()) {
            continue;
        }
        // Behaviors operate on the layer transformation/opacity through their
        // own Notify(102/104) ABI. They are applied once in
        // applyClipBehaviors, after the pixel effects have completed.
        if (plugin::nativeBehaviorRenderingVerified(fx.pluginId)
            || plugin::nativeBehaviorUsesInstance(fx.pluginId)) {
            continue;
        }
        // Geometry modules shape the text mesh in renderTextGeometry.
        if (plugin::nativeGeometryRenderingVerified(fx.pluginId)) {
            continue;
        }
        QStringList resolved;
        resolved.reserve(fx.parameterValues.size());
        for (int i = 0; i < fx.parameterValues.size(); ++i) {
            resolved.push_back(fx.parameterAt(i, frame).toString());
        }
        plugin::applyEffectToImage(image, fx.pluginId, resolved, time);
    }
}

double RenderWorker::applyClipBehaviors(QImage& image,
                                        const composition::Layer& layer,
                                        const composition::Clip& clip,
                                        int frame, int localFrame,
                                        const QSize& canvasSize) const
{
    double opacity = 1.0;
    const double frameRate = m_composition
        ? double(m_composition->fpsNumerator())
              / qMax(1, m_composition->fpsDenominator())
        : 30.0;
    const int durationFrames = m_composition
        ? qMax(1, qRound(clip.durationSeconds
                         * frameRate))
        : 1;
    QVector<plugin::NativeBehaviorRequest> simulationBehaviors;
    // Behavior matrices are in composition space - from the frame's centre,
    // Y up (Drop lifts a layer's lower edge to +height/2, the top) - so the
    // whole matrix is mirrored into the image's rows, rotation and shear
    // included: a module's clockwise turn stays clockwise on screen.
    const auto applyTransformation = [&image](const QMatrix4x4& matrix) {
        const float* m = matrix.constData();
        const QTransform transform(double(m[0]), -double(m[1]),
                                   -double(m[4]), double(m[5]),
                                   double(m[12]), -double(m[13]));
        if (transform.isIdentity()) return;
        QImage transformed(image.size(), QImage::Format_RGBA8888);
        transformed.fill(Qt::transparent);
        QPainter painter(&transformed);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QPointF centre(image.width() / 2.0, image.height() / 2.0);
        painter.translate(centre);
        painter.setTransform(transform, true);
        painter.translate(-centre);
        painter.drawImage(QPoint(0, 0), image);
        image = std::move(transformed);
    };
    const qsizetype clipIndex = &clip - layer.clips.constData();
    for (qsizetype effectIndex = 0; effectIndex < clip.effects.size(); ++effectIndex) {
        const composition::Effect& fx = clip.effects.at(effectIndex);
        if (fx.enabled && plugin::nativeBehaviorUsesInstance(fx.pluginId)
            && clipIndex >= 0 && clipIndex < layer.clips.size()) {
            // Matrices the module's own instance gave on the GUI thread, on
            // the canvas from its centre, Y up (NativeInstanceHost::bake).
            std::array<float, 16> matrix {};
            const QString key = QStringLiteral("%1/%2/%3").arg(layer.id.value()).arg(clipIndex)
                                    .arg(effectIndex);
            if (plugin::nativeInstanceTransformAt(key, frame, &matrix))
                applyTransformation(QMatrix4x4(matrix.data()).transposed());
            continue;
        }
        if (!fx.enabled || !plugin::nativeBehaviorRenderingVerified(fx.pluginId)
            || plugin::nativeBehaviorSubObjectRenderingVerified(fx.pluginId)) {
            continue;
        }
        QStringList values = fx.parameterValues;
        for (int parameter = 0; parameter < values.size(); ++parameter) {
            values[parameter] = fx.parameterAt(parameter, frame).toString();
        }
        if (plugin::nativeBehaviorSimulationRenderingVerified(fx.pluginId)) {
            simulationBehaviors.push_back({fx.pluginId, values});
        }
        plugin::NativeBehaviorResult behavior;
        if (!plugin::evaluateNativeBehaviorFrame(
                behavior, frame, localFrame, durationFrames,
                canvasSize.width(), canvasSize.height(), frameRate,
                fx.pluginId, values, m_composition.get(), layer.id,
                behaviorLayerAt(layer, clip, textEffectOf(clip), frame, canvasSize,
                                m_mediaManager.get()))) {
            continue;
        }
        opacity *= behavior.opacity;
        applyTransformation(behavior.transformation);
    }
    plugin::NativeBehaviorResult simulation;
    if (plugin::simulateNativeBehaviorStack(
            simulation, frame, localFrame, durationFrames,
            canvasSize.width(), canvasSize.height(), frameRate,
            simulationBehaviors, m_composition.get(), layer.id)) {
        applyTransformation(simulation.transformation);
    }
    return qBound(0.0, opacity, 1.0);
}

void RenderWorker::applyLayerMasks(QImage& image, const composition::Layer& layer) const
{
    QVector<composition::LayerMask> masks;
    for (const auto& mask : layer.masks) if (mask.enabled && !mask.bounds.isEmpty()) masks << mask;
    if (image.isNull() || masks.isEmpty()) return;

    // Masks are drawn in the shot's square space, the space the clip image is
    // rendered in.
    const QSize space = m_composition ? m_composition->displaySize() : image.size();
    const double sx = double(image.width()) / qMax(1, space.width());
    const double sy = double(image.height()) / qMax(1, space.height());
    QImage alpha(image.size(), QImage::Format_RGBA8888);
    alpha.fill(Qt::transparent);
    QPainter maskPainter(&alpha);
    maskPainter.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& mask : masks) {
        QRectF bounds(mask.bounds.x() * sx, mask.bounds.y() * sy,
                      mask.bounds.width() * sx, mask.bounds.height() * sy);
        bounds.adjust(-mask.expansion * sx, -mask.expansion * sy,
                      mask.expansion * sx, mask.expansion * sy);
        QPainterPath path;
        if (mask.shape == composition::MaskShape::Freehand && mask.points.size() >= 3) {
            QPolygonF polygon;
            const QPointF center = mask.bounds.center();
            const double ex = mask.bounds.width() > 0 ? (mask.bounds.width() + 2.0 * mask.expansion) / mask.bounds.width() : 1.0;
            const double ey = mask.bounds.height() > 0 ? (mask.bounds.height() + 2.0 * mask.expansion) / mask.bounds.height() : 1.0;
            for (const QPointF& point : mask.points)
                polygon << QPointF((center.x() + (point.x() - center.x()) * ex) * sx,
                                   (center.y() + (point.y() - center.y()) * ey) * sy);
            path.addPolygon(polygon); path.closeSubpath();
        } else if (mask.shape == composition::MaskShape::Ellipse) path.addEllipse(bounds);
        else if (mask.shape == composition::MaskShape::RoundedRectangle)
            path.addRoundedRect(bounds, bounds.width() * .15, bounds.height() * .15);
        else if (mask.shape == composition::MaskShape::Polygon
                 || mask.shape == composition::MaskShape::Star) {
            QPolygonF polygon;
            const int count = mask.shape == composition::MaskShape::Star ? 10 : 5;
            const QPointF center = bounds.center();
            for (int i = 0; i < count; ++i) {
                constexpr double pi = 3.14159265358979323846;
                const double angle = -pi * .5 + i * 2.0 * pi / count;
                const double factor = mask.shape == composition::MaskShape::Star && (i & 1) ? .42 : 1.0;
                polygon << QPointF(center.x() + qCos(angle) * bounds.width() * .5 * factor,
                                   center.y() + qSin(angle) * bounds.height() * .5 * factor);
            }
            path.addPolygon(polygon); path.closeSubpath();
        } else path.addRect(bounds);
        if (mask.inverted) {
            QPainterPath outer; outer.addRect(QRectF(QPointF(0, 0), image.size()));
            path = outer.subtracted(path);
        }
        maskPainter.setPen(Qt::NoPen);
        maskPainter.setBrush(QColor(255, 255, 255, qRound(qBound(0.0, mask.opacity, 1.0) * 255.0)));
        maskPainter.drawPath(path);
    }
    maskPainter.end();
    double feather = 0.0;
    for (const auto& mask : masks) feather = qMax(feather, mask.feather * qMax(sx, sy));
    if (feather > 0.0) {
        plugin::applyEffectToImage(alpha, plugin::PluginId(plugin::kBuiltinBlur),
                                   {QString::number(feather), QStringLiteral("2"),
                                    QStringLiteral("Horizontal & Vertical"), QStringLiteral("1")});
    }
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawImage(QPoint(0, 0), alpha);
}

QMatrix4x4 RenderWorker::layerModelMatrix(const composition::LayerTransform& transform, int frame)
{
    // Order matters and follows the reference: the layer is placed, then aimed
    // by its orientation, then spun about its own three axes, then scaled, and
    // all of it turns about the anchor point rather than the layer's centre.
    const QPointF position = transform.positionAt(frame);
    const QPointF scale = transform.scaleAt(frame);

    QMatrix4x4 matrix;
    matrix.translate(float(position.x()), float(position.y()),
                     float(transform.positionZAt(frame)));

    matrix.rotate(float(transform.orientationAt(frame, 2)), 0.0f, 0.0f, 1.0f);
    matrix.rotate(float(transform.orientationAt(frame, 1)), 0.0f, 1.0f, 0.0f);
    matrix.rotate(float(transform.orientationAt(frame, 0)), 1.0f, 0.0f, 0.0f);

    // Z turns clockwise on screen, as the 2D path draws it; Y points up here,
    // so that is a negative turn. It used to be positive, and a rotated text
    // or model layer switched to 3D turned the other way.
    matrix.rotate(float(-transform.rotationAt(frame)), 0.0f, 0.0f, 1.0f);
    matrix.rotate(float(transform.rotationYAt(frame)), 0.0f, 1.0f, 0.0f);
    matrix.rotate(float(transform.rotationXAt(frame)), 1.0f, 0.0f, 0.0f);

    matrix.scale(float(scale.x() / 100.0), float(scale.y() / 100.0),
                 float(transform.scaleZAt(frame) / 100.0));
    matrix.translate(float(-transform.anchorPoint.x()), float(-transform.anchorPoint.y()),
                     float(-transform.anchorPointZ));
    return matrix;
}

model3d::Camera RenderWorker::sceneCamera(const QSize& canvasSize, int frame) const
{
    model3d::Camera camera = model3d::defaultCameraForCanvas(canvasSize);
    if (!m_composition) {
        return camera;
    }
    for (const composition::Layer& layer : m_composition->layers()) {
        if (layer.kind != composition::LayerKind::Camera || !layer.visible) {
            continue;
        }
        camera.fieldOfViewDegrees = qBound(20.0, layer.cameraFieldOfView, 140.0);
        const composition::LayerTransform& t = layer.transform;
        const QPointF position = t.positionAt(frame);
        camera.position = QVector3D(float(position.x()), float(position.y()),
                                    float(t.positionZAt(frame)));
        // The camera looks down its own -Z, so its rotations aim that axis and
        // the target follows from where it ends up pointing.
        QMatrix4x4 aim;
        aim.rotate(float(t.rotationAt(frame)), 0.0f, 0.0f, 1.0f);
        aim.rotate(float(t.rotationYAt(frame)), 0.0f, 1.0f, 0.0f);
        aim.rotate(float(t.rotationXAt(frame)), 1.0f, 0.0f, 0.0f);
        const QVector3D forward = aim.map(QVector3D(0.0f, 0.0f, -1.0f));
        camera.target = camera.position + forward;
        camera.up = aim.map(QVector3D(0.0f, 1.0f, 0.0f));
        break;
    }
    return camera;
}

model3d::Light RenderWorker::sceneLight(int frame) const
{
    model3d::Light light;
    if (!m_composition) {
        return light;
    }
    for (const composition::Layer& layer : m_composition->layers()) {
        if (layer.kind != composition::LayerKind::Light || !layer.visible) {
            continue;
        }
        // A light layer shines from where it stands towards the scene origin,
        // which is what a point light placed in the composition amounts to at
        // this level of detail.
        const QPointF position = layer.transform.positionAt(frame);
        const QVector3D at(float(position.x()), float(position.y()),
                           float(layer.transform.positionZAt(frame)));
        if (!at.isNull()) {
            light.direction = -at.normalized();
        }
        light.intensity = layer.transform.opacityAt(frame, layer.opacity);
        break;
    }
    return light;
}

QMatrix4x4 RenderWorker::layerPlaneMatrix(const composition::LayerTransform& t, int frame)
{
    // Placement on the layer's plane, as the 2D path places the layer: about
    // the anchor for the turns, about the centre for the scale. Y points up in
    // the scene, so the Z turn that is clockwise on screen in 2D is negative.
    const QPointF position = t.positionAt(frame);
    const QPointF scale = t.scaleAt(frame);
    QMatrix4x4 model;
    model.translate(float(position.x()), float(position.y()), float(t.positionZAt(frame)));
    model.translate(float(t.anchorPoint.x()), float(t.anchorPoint.y()), float(t.anchorPointZ));
    model.rotate(float(t.orientationAt(frame, 2)), 0.0f, 0.0f, 1.0f);
    model.rotate(float(t.orientationAt(frame, 1)), 0.0f, 1.0f, 0.0f);
    model.rotate(float(t.orientationAt(frame, 0)), 1.0f, 0.0f, 0.0f);
    model.rotate(float(-t.rotationAt(frame)), 0.0f, 0.0f, 1.0f);
    model.rotate(float(t.rotationYAt(frame)), 0.0f, 1.0f, 0.0f);
    model.rotate(float(t.rotationXAt(frame)), 1.0f, 0.0f, 0.0f);
    model.translate(float(-t.anchorPoint.x()), float(-t.anchorPoint.y()), float(-t.anchorPointZ));
    model.scale(float(scale.x() / 100.0), float(scale.y() / 100.0), float(t.scaleZAt(frame) / 100.0));
    return model;
}

bool RenderWorker::inScene(const composition::Layer& layer)
{
    if (layer.kind == composition::LayerKind::Model3D) return true;
    return layer.dimension == composition::LayerDimension::ThreeD
           && (layer.kind == composition::LayerKind::Media || layer.kind == composition::LayerKind::Plane
               || layer.kind == composition::LayerKind::Text);
}

QVector<float> RenderWorker::sceneDistances(const composition::Layer& layer, const QImage& image,
                                            int frame) const
{
    const QSize size = image.size();
    QVector<float> distances(size.width() * size.height(), std::numeric_limits<float>::infinity());
    if (size.isEmpty()) return distances;
    const model3d::Camera camera = sceneCamera(size, frame);
    const QMatrix4x4 viewProjection = camera.viewProjection(size);
    bool invertible = false;
    const QMatrix4x4 unproject = viewProjection.inverted(&invertible);
    const QVector3D eye = camera.position;
    // A model or Geometry text is a body, not a sheet: it is placed in depth
    // by where the layer stands. A sheet is met pixel by pixel.
    const bool sheet = layer.kind != composition::LayerKind::Model3D
        && !(layer.kind == composition::LayerKind::Text && !layer.clips.isEmpty()
             && std::any_of(layer.clips.first().effects.cbegin(), layer.clips.first().effects.cend(),
                            [](const composition::Effect& fx) {
                                return fx.enabled && plugin::nativeGeometryRenderingVerified(fx.pluginId);
                            }));
    const QMatrix4x4 model = sheet ? layerPlaneMatrix(layer.transform, frame) : QMatrix4x4();
    const QVector3D origin = sheet ? model.map(QVector3D(0.0f, 0.0f, 0.0f))
        : QVector3D(float(layer.transform.positionAt(frame).x()), float(layer.transform.positionAt(frame).y()),
                    float(layer.transform.positionZAt(frame)));
    const float bodyDistance = (origin - eye).length();
    const QVector3D normal = QVector3D::crossProduct(model.mapVector(QVector3D(1.0f, 0.0f, 0.0f)),
                                                     model.mapVector(QVector3D(0.0f, 1.0f, 0.0f)));
    const float toPlane = QVector3D::dotProduct(origin - eye, normal);
    const double width = size.width(), height = size.height();
    for (int y = 0; y < size.height(); ++y) {
        const uchar* row = image.constScanLine(y);
        const double ndcY = 1.0 - 2.0 * (y + 0.5) / height;
        for (int x = 0; x < size.width(); ++x) {
            if (row[x * 4 + 3] == 0) continue;
            float distance = bodyDistance;
            if (sheet && invertible) {
                const double ndcX = 2.0 * (x + 0.5) / width - 1.0;
                const QVector3D ray = unproject.map(QVector3D(float(ndcX), float(ndcY), 1.0f)) - eye;
                const float along = QVector3D::dotProduct(ray, normal);
                if (!qFuzzyIsNull(along) && toPlane / along > 0.0f) distance = toPlane / along * ray.length();
            }
            distances[y * size.width() + x] = distance;
        }
    }
    return distances;
}

model3d::Fog RenderWorker::sceneFog() const
{
    model3d::Fog fog;
    if (!m_composition) return fog;
    const composition::CompositionRenderSettings& s = m_composition->renderSettings();
    fog.enabled = s.fogEnabled;
    fog.nearDistance = s.fogNearDistance;
    fog.farDistance = s.fogFarDistance;
    fog.density = s.fogDensity;
    fog.falloff = model3d::Fog::Falloff(qBound(0, s.fogFalloff, 2));
    fog.color = s.fogColor;
    return fog;
}

QImage RenderWorker::renderClipIn3D(const composition::Layer& layer, const composition::Clip& clip,
                                    double localTimeSeconds, const QSize& canvasSize, int frame) const
{
    QImage canvas(canvasSize, QImage::Format_RGBA8888);
    canvas.fill(Qt::transparent);
    if (canvasSize.isEmpty()) return canvas;

    // The layer's own plane: its content as a 2D layer left at the frame's
    // centre would show it, on a sheet as large as the frame - or as the
    // footage or nested shot, when that is larger, so nothing is cut off
    // before the layer is scaled.
    QSize plane = canvasSize;
    if (clip.nestedComposition) {
        plane = plane.expandedTo(clip.nestedComposition->displaySize());
    } else if (m_mediaManager && clip.mediaId.isValid()) {
        const media::MediaAsset asset = m_mediaManager->assetById(clip.mediaId);
        const QSize own = asset.frameSize();
        if (own.isValid()) {
            plane = plane.expandedTo(QSize(int(std::ceil(own.width() * asset.pixelAspectValue())),
                                           own.height()));
        }
    }
    composition::Layer flat = layer;
    flat.dimension = composition::LayerDimension::TwoD;
    flat.transform = composition::LayerTransform();
    const QImage content = renderClip(flat, clip, localTimeSeconds,
                                      textEffectOf(clip) ? canvasSize : plane, frame);
    if (content.isNull()) return canvas;
    const QSizeF sheet = content.size();

    const QMatrix4x4 model = layerPlaneMatrix(layer.transform, frame);

    const model3d::Camera camera = sceneCamera(canvasSize, frame);
    const QMatrix4x4 viewProjection = camera.viewProjection(canvasSize);
    const QMatrix4x4 mvp = viewProjection * model;
    const double width = canvasSize.width();
    const double height = canvasSize.height();
    // A point of the sheet (pixels, Y down) on screen; false behind the eye.
    const auto project = [&](const QPointF& pixel, QPointF* screen) {
        const QVector4D clipPos = mvp * QVector4D(float(pixel.x() - sheet.width() / 2.0),
                                                  float(sheet.height() / 2.0 - pixel.y()), 0.0f, 1.0f);
        if (clipPos.w() <= 1e-3f) return false;
        const double ndcX = double(clipPos.x()) / clipPos.w();
        const double ndcY = double(clipPos.y()) / clipPos.w();
        *screen = QPointF((ndcX * 0.5 + 0.5) * width, (1.0 - (ndcY * 0.5 + 0.5)) * height);
        return true;
    };

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    // Whole when the sheet is in front of the camera; otherwise cell by cell,
    // leaving out what is behind it.
    const auto drawPart = [&](const QRectF& part) {
        QPolygonF to;
        for (const QPointF& corner : {part.topLeft(), part.topRight(), part.bottomRight(), part.bottomLeft()}) {
            QPointF screen;
            if (!project(corner, &screen)) return;
            to << screen;
        }
        QTransform mapping;
        if (!QTransform::quadToQuad(QPolygonF(part), to, mapping)) return;
        painter.setTransform(mapping);
        painter.drawImage(part, content, part);
    };
    QPointF probe;
    const bool whole = project(QPointF(0, 0), &probe) && project(QPointF(sheet.width(), 0), &probe)
        && project(QPointF(sheet.width(), sheet.height()), &probe) && project(QPointF(0, sheet.height()), &probe);
    if (whole) {
        drawPart(QRectF(QPointF(0, 0), sheet));
    } else {
        constexpr int kCells = 16;
        for (int row = 0; row < kCells; ++row)
            for (int column = 0; column < kCells; ++column)
                drawPart(QRectF(sheet.width() * column / kCells, sheet.height() * row / kCells,
                                sheet.width() / kCells, sheet.height() / kCells));
    }
    painter.end();

    // Fog by each pixel's distance from the camera: where the ray through the
    // pixel meets the layer's plane.
    const model3d::Fog fog = sceneFog();
    if (fog.enabled) {
        bool invertible = false;
        const QMatrix4x4 unproject = viewProjection.inverted(&invertible);
        const QVector3D origin = model.map(QVector3D(0.0f, 0.0f, 0.0f));
        const QVector3D normal = QVector3D::crossProduct(model.mapVector(QVector3D(1.0f, 0.0f, 0.0f)),
                                                         model.mapVector(QVector3D(0.0f, 1.0f, 0.0f)));
        const QVector3D eye = camera.position;
        const float toPlane = QVector3D::dotProduct(origin - eye, normal);
        for (int y = 0; invertible && y < canvas.height(); ++y) {
            uchar* row = canvas.scanLine(y);
            const double ndcY = 1.0 - 2.0 * (y + 0.5) / height;
            for (int x = 0; x < canvas.width(); ++x) {
                uchar* pixel = row + size_t(x) * 4;
                if (pixel[3] == 0) continue;
                const double ndcX = 2.0 * (x + 0.5) / width - 1.0;
                const QVector3D far = unproject.map(QVector3D(float(ndcX), float(ndcY), 1.0f));
                const QVector3D ray = far - eye;
                const float along = QVector3D::dotProduct(ray, normal);
                if (qFuzzyIsNull(along)) continue;
                const float hit = toPlane / along;
                if (hit <= 0.0f) continue;
                fog.apply(pixel, double(hit * ray.length()));
            }
        }
    }
    return canvas;
}

QImage RenderWorker::renderModelLayer(const composition::Layer& layer, const QSize& canvasSize,
                                      int frame) const
{
    if (!m_mediaManager || !layer.modelAssetId.isValid()) {
        return QImage();
    }
    const model3d::Mesh mesh = m_mediaManager->modelMesh(layer.modelAssetId);
    if (mesh.isEmpty()) {
        return QImage();
    }
    return model3d::renderMesh(mesh, layerModelMatrix(layer.transform, frame),
                               sceneCamera(canvasSize, frame), canvasSize, sceneLight(frame),
                               model3d::ShadingMode::Shaded, 1.0, sceneFog());
}

QImage RenderWorker::renderTextGeometry(const composition::Layer& layer,
                                        const composition::Clip& clip,
                                        const composition::TextStyle& style,
                                        const QVector<const composition::Effect*>& effects,
                                        const QSize& canvasSize, int frame) const
{
    // Flux builds this geometry for a TextBox with enabled Geometry effects
    // (FUN_1804d94e0) and runs the effects over it in order, each one
    // replacing it with what the module hands back.
    const QRectF box(-canvasSize.width() / 2.0, -canvasSize.height() / 2.0,
                     canvasSize.width(), canvasSize.height());
    QVector<plugin::NativeGeometryBatch> geometry = textGeometry(box, style);
    const double fps = m_composition
        ? double(m_composition->fpsNumerator()) / qMax(1, m_composition->fpsDenominator())
        : 30.0;
    const int localFrame = qMax(0, frame - qRound(clip.startSeconds * fps));
    const int durationFrames = qMax(1, qRound(clip.durationSeconds * fps));
    for (const composition::Effect* fx : effects) {
        QStringList values;
        for (int i = 0; i < fx->parameterValues.size(); ++i) {
            values.append(fx->parameterAt(i, frame).toString());
        }
        plugin::applyNativeGeometryEffect(geometry, fx->pluginId, values, frame, localFrame,
                                          durationFrames, fps);
    }
    fillGeometryPolygons(geometry);
    model3d::Material material;
    material.diffuseColor = style.fontColor;
    material.ambientColor = style.fontColor;
    material.specularReflectivity = 0.25;
    material.opacity = style.fontColor.alphaF();
    const model3d::Mesh mesh = meshFromGeometry(geometry, {material});

    // A 3D layer is placed and seen like a model; a 2D one keeps the 2D text
    // placement under the camera that shows the z = 0 plane at frame size.
    QMatrix4x4 placement;
    model3d::Camera camera;
    if (layer.dimension == composition::LayerDimension::ThreeD) {
        placement = layerModelMatrix(layer.transform, frame);
        camera = sceneCamera(canvasSize, frame);
    } else {
        const QPointF position = layer.transform.positionAt(frame);
        const QPointF scale = layer.transform.scaleAt(frame);
        placement.translate(float(position.x()), float(position.y()), 0.0f);
        placement.rotate(float(-layer.transform.rotationAt(frame)), 0.0f, 0.0f, 1.0f);
        placement.scale(float(scale.x() / 100.0), float(scale.y() / 100.0), 1.0f);
        camera = model3d::defaultCameraForCanvas(canvasSize);
    }
    // The rasteriser does not antialias; rendering at twice the size and
    // reducing keeps glyph edges close to the 2D text's.
    // Fog reaches the 3D layer only; a 2D one is not in the scene.
    const model3d::Fog fog = layer.dimension == composition::LayerDimension::ThreeD
        ? sceneFog() : model3d::Fog();
    QImage image = model3d::renderMesh(mesh, placement, camera, canvasSize * 2, sceneLight(frame),
                                       model3d::ShadingMode::Shaded, 1.0, fog);
    return image.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
        .scaled(canvasSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGBA8888);
}

QImage RenderWorker::renderFrameImage(double timeSeconds, const QSize& size, bool withEffects,
                                      bool transparentBackground) const
{
    QImage out(size, QImage::Format_RGBA8888);
    out.fill(transparentBackground ? Qt::transparent : QColor(18, 18, 22));

    if (!m_composition) {
        return out;
    }

    // Layer 1 is the topmost one, the way the reference numbers them (its own
    // screenshot has "1. New Point" drawn over "2. Sunrise.mp4"). The vector
    // holds them in that display order, so compositing has to run backwards -
    // painting it forwards put the bottom layer last and hid everything above
    // it behind the background.
    const QVector<composition::Layer>& layers = m_composition->layers();
    // Draws one layer - its clips, transitions, opacity and blend - into
    // `target`.
    const auto paintLayer = [&](const composition::Layer& layer, QImage& target,
                                QPainter::CompositionMode blend) {
        // A 3D model layer has no clips: its content is the imported geometry,
        // rasterised over the whole canvas and composited like any other layer.
        if (layer.kind == composition::LayerKind::Model3D) {
            const int frameNo = frameForTime(timeSeconds);
            QImage modelImage = renderModelLayer(layer, size, frameNo);
            if (modelImage.isNull()) {
                return;
            }
            applyLayerMasks(modelImage, layer);
            const double opacity = layer.transform.opacityAt(frameNo, layer.opacity);
            if (opacity <= 0.0) {
                return;
            }
            QPainter painter(&target);
            painter.setOpacity(opacity);
            painter.setCompositionMode(blend);
            painter.drawImage(QPoint(0, 0), modelImage);
            return;
        }

        const int frameNo = frameForTime(timeSeconds);
        // Layer opacity. This used to paint clipImage into itself through a
        // painter with the opacity set, which is a self-blit - source and
        // destination are one buffer - so the result was undefined and the
        // opacity never actually showed. QPainter::setOpacity on the
        // compositing painter multiplies the source alpha during the
        // source-over blend, which is what was meant.
        const double layerOpacity = layer.transform.opacityAt(frameNo, layer.opacity);
        const auto composite = [&](const QImage& image) {
            if (image.isNull() || layerOpacity <= 0.0) {
                return; // fully transparent contributes nothing
            }
            QPainter painter(&target);
            painter.setOpacity(layerOpacity);
            painter.setCompositionMode(blend);
            painter.drawImage(QPoint(0, 0), image);
        };

        // Clips inside an active video transition are drawn once, through it.
        QSet<int> transitioned;
        const double fps = m_composition->fpsDenominator() > 0
            ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
        const auto windows = composition::transitionWindows(
            layer, 0.5 / qMax(1.0, fps), [](const composition::Effect& effect) {
                return !plugin::isAudioTransition(effect.pluginId);
            });
        for (const composition::TransitionWindow& window : windows) {
            if (!window.contains(timeSeconds)) {
                continue;
            }
            const auto clipImage = [&](int clipIndex) {
                QImage image;
                if (clipIndex >= 0) {
                    image = renderLayerClipImage(layer, layer.clips.at(clipIndex), timeSeconds,
                                                 size, withEffects);
                    transitioned.insert(clipIndex);
                }
                if (image.isNull()) {
                    image = QImage(size, QImage::Format_RGBA8888);
                    image.fill(Qt::transparent);
                }
                return image;
            };
            const QImage from = clipImage(window.fromClip);
            const QImage to = clipImage(window.toClip);
            const composition::Effect& effect =
                layer.clips.at(window.ownerClip).effects.at(window.effectIndex);
            QStringList values;
            for (int i = 0; i < effect.parameterValues.size(); ++i) {
                values.append(effect.parameterAt(i, frameNo).toString());
            }
            const float progress = float(window.progressAt(timeSeconds));
            QImage mixed;
            if (!withEffects
                || !plugin::nativeVideoTransitionRenderingVerified(effect.pluginId)
                || !plugin::applyNativeVideoTransition(mixed, from, to, progress,
                                                       effect.pluginId, values)) {
                // Effects disabled by the quality profile, or a module this
                // build cannot run: keep the cut soft with a plain dissolve.
                mixed = dissolveImages(from, to, progress);
            }
            composite(mixed);
        }

        for (int clipIndex = 0; clipIndex < layer.clips.size(); ++clipIndex) {
            const composition::Clip& clip = layer.clips.at(clipIndex);
            const double end = clip.startSeconds + clip.durationSeconds;
            if (transitioned.contains(clipIndex)
                || timeSeconds < clip.startSeconds || timeSeconds > end) {
                continue;
            }
            composite(renderLayerClipImage(layer, clip, timeSeconds, size, withEffects));
        }
    };

    // Neighbouring 3D layers share one scene, as in the reference: within it
    // what is nearer the camera covers what is further, whatever the stack
    // says, and a 2D layer - or a Grade - between them starts a new one.
    struct SceneLayer { const composition::Layer* layer; QImage image; };
    QVector<SceneLayer> scene;
    const auto closeScene = [&] {
        if (scene.isEmpty()) return;
        if (scene.size() == 1) {
            // Alone, it is drawn as before, with its own blend.
            QPainter painter(&out);
            painter.setCompositionMode(compositionModeFor(scene.first().layer->blendMode));
            painter.drawImage(QPoint(0, 0), scene.first().image);
            scene.clear();
            return;
        }
        const int frameNo = frameForTime(timeSeconds);
        QVector<QVector<float>> distances;
        QVector<QImage> premultiplied;
        for (const SceneLayer& member : scene) {
            distances.append(sceneDistances(*member.layer, member.image, frameNo));
            premultiplied.append(member.image.convertToFormat(QImage::Format_RGBA8888_Premultiplied));
        }
        QImage base = out.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        QVarLengthArray<int, 16> order;
        for (int y = 0; y < size.height(); ++y) {
            uchar* target = base.scanLine(y);
            for (int x = 0; x < size.width(); ++x) {
                const int at = y * size.width() + x;
                order.clear();
                for (int i = 0; i < premultiplied.size(); ++i)
                    if (premultiplied.at(i).constScanLine(y)[x * 4 + 3] != 0) order.append(i);
                if (order.isEmpty()) continue;
                // Far to near; a tie keeps the stack's order (lower first).
                std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                    return distances.at(a).at(at) > distances.at(b).at(at);
                });
                uchar* pixel = target + x * 4;
                for (int i : order) {
                    const uchar* source = premultiplied.at(i).constScanLine(y) + x * 4;
                    const int keep = 255 - source[3];
                    for (int c = 0; c < 4; ++c)
                        pixel[c] = uchar(qMin(255, source[c] + (pixel[c] * keep + 127) / 255));
                }
            }
        }
        out = base.convertToFormat(QImage::Format_RGBA8888);
        scene.clear();
    };

    for (int li = layers.size() - 1; li >= 0; --li) {
        const composition::Layer& layer = layers.at(li);
        if (!layer.visible) {
            continue; // the eye in the layer tree has to mean something
        }

        // Point, Light and Camera are scene objects, not picture. A Camera
        // decides what the 3D layers are seen through and a Light shades them
        // (see sceneCamera/sceneLight), but neither draws anything itself.
        if (layer.kind == composition::LayerKind::Point
            || layer.kind == composition::LayerKind::Light
            || layer.kind == composition::LayerKind::Camera) {
            continue;
        }

        if (inScene(layer)) {
            QImage image(size, QImage::Format_RGBA8888);
            image.fill(Qt::transparent);
            paintLayer(layer, image, QPainter::CompositionMode_SourceOver);
            scene.append({&layer, image});
            continue;
        }
        closeScene();

        // A Grade layer is an adjustment layer: its effects act on everything
        // composited beneath it. Layers are painted bottom-up, so `out` already
        // holds exactly that.
        if (layer.kind == composition::LayerKind::Grade) {
            const int frameNo = frameForTime(timeSeconds);
            for (const composition::Clip& clip : layer.clips) {
                const double end = clip.startSeconds + clip.durationSeconds;
                if (timeSeconds < clip.startSeconds || timeSeconds > end) {
                    continue;
                }
                if (withEffects) {
                    if (layer.masks.isEmpty()) {
                        applyClipEffects(out, clip, frameNo);
                    } else {
                        QImage graded = out;
                        applyClipEffects(graded, clip, frameNo);
                        applyLayerMasks(graded, layer);
                        QPainter painter(&out);
                        painter.drawImage(QPoint(0, 0), graded);
                    }
                }
            }
            continue;
        }
        paintLayer(layer, out, compositionModeFor(layer.blendMode));
    }
    closeScene();

    return out;
}

QImage RenderWorker::renderLayerClipImage(const composition::Layer& layer,
                                          const composition::Clip& clip, double timeSeconds,
                                          const QSize& size, bool withEffects) const
{
    if (layer.kind != composition::LayerKind::Plane && !clip.nestedComposition
        && !contributesVideo(clip.mediaId)) {
        return QImage();
    }
    // Time inside the clip's own material, not just inside the clip: the Slip
    // tool moves the source in-point and Rate Stretch changes the speed, and
    // neither would show on screen if the renderer kept reading the material
    // from its beginning at 1x.
    const double speed = clip.speed > 0.0 ? clip.speed : 1.0;
    const double localTime = clip.sourceStartSeconds + (timeSeconds - clip.startSeconds) * speed;
    const int frameNo = frameForTime(timeSeconds);
    QImage clipImage;
    const composition::CompositionRenderSettings& shutter = m_composition->renderSettings();
    const QVector<double> offsets = withEffects && layer.motionBlur && shutter.motionBlurEnabled
            && shutter.shutterAngle > 0.0 && hasMotion(layer)
        ? shutterOffsets(shutter, framePixelTravel(layer, frameNo, size))
        : QVector<double>();
    if (offsets.size() < 2) {
        clipImage = renderClip(offsets.isEmpty() ? layer : layerAtSubFrame(layer, frameNo + offsets[0]),
                               clip, qMax(0.0, localTime), size, frameNo);
    } else {
        // Averaged in premultiplied space, so the smeared edges fade out
        // instead of picking up the colour of transparent pixels.
        std::vector<float> sum;
        QSize sampleSize;
        for (double offset : offsets) {
            const QImage sample = renderClip(layerAtSubFrame(layer, frameNo + offset), clip,
                                             qMax(0.0, localTime), size, frameNo)
                                      .convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            if (sum.empty()) {
                sampleSize = sample.size();
                sum.assign(size_t(sampleSize.width()) * size_t(sampleSize.height()) * 4, 0.0f);
            }
            if (sample.size() != sampleSize) continue;
            for (int y = 0; y < sampleSize.height(); ++y) {
                const uchar* row = sample.constScanLine(y);
                float* target = sum.data() + size_t(y) * size_t(sampleSize.width()) * 4;
                for (int x = 0; x < sampleSize.width() * 4; ++x) target[x] += row[x];
            }
        }
        QImage averaged(sampleSize, QImage::Format_RGBA8888_Premultiplied);
        const float scale = 1.0f / float(offsets.size());
        for (int y = 0; y < sampleSize.height(); ++y) {
            uchar* row = averaged.scanLine(y);
            const float* source = sum.data() + size_t(y) * size_t(sampleSize.width()) * 4;
            for (int x = 0; x < sampleSize.width() * 4; ++x)
                row[x] = uchar(qBound(0, int(std::lround(source[x] * scale)), 255));
        }
        clipImage = averaged.convertToFormat(QImage::Format_RGBA8888);
    }
    if (withEffects) {
        applyClipEffects(clipImage, clip, frameNo);
    }
    const int localFrame = frameForTime(timeSeconds - clip.startSeconds);
    const double behaviorOpacity = withEffects
        ? applyClipBehaviors(clipImage, layer, clip, frameNo, localFrame, size)
        : 1.0;
    applyLayerMasks(clipImage, layer);
    if (behaviorOpacity < 1.0) {
        QImage faded(clipImage.size(), QImage::Format_RGBA8888);
        faded.fill(Qt::transparent);
        QPainter painter(&faded);
        painter.setOpacity(qMax(0.0, behaviorOpacity));
        painter.drawImage(QPoint(0, 0), clipImage);
        painter.end();
        clipImage = std::move(faded);
    }
    return clipImage;
}

void RenderWorker::renderFrame(const FrameRequest& request, QByteArray mediaBytes)
{
    Q_UNUSED(mediaBytes);
    if (request.compositionSnapshot) m_composition = request.compositionSnapshot;
    m_frameComplete = true;
    m_preRenderDirectory = request.preRenderDirectory;
    const bool reportTiming = QSettings().value(
        QStringLiteral("Debug/ShowRenderTimings"), false).toBool();
    QElapsedTimer renderTimer;
    if (reportTiming) renderTimer.start();
    // Layers are placed in the shot's square space (positions, media at its
    // own size, text, the 3D camera), so the frame is rendered at that size -
    // width x PAR by height - and then resampled to the pixels asked for. That
    // squeezes a non-square-pixel shot into its frame the way Flux does, and
    // a Half/Quarter preview or the Layer panel's 960x540 one no longer gets
    // full-scale content cropped into the smaller frame.
    const QSize native = m_composition ? m_composition->displaySize() : QSize();
    QImage frame = renderFrameImage(request.timeSeconds, native.isEmpty() ? request.size : native,
                                    request.renderEffects, request.transparentBackground);
    if (frame.size() != request.size && !request.size.isEmpty()) {
        frame = frame.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                    .scaled(request.size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_RGBA8888);
    }
    if (reportTiming) {
        OV_LOG_INFO(QStringLiteral("Rendered frame %1 at %2 s, %3x%4, effects=%5 in %6 ms")
                        .arg(request.frameIndex)
                        .arg(request.timeSeconds, 0, 'f', 3)
                        .arg(request.size.width())
                        .arg(request.size.height())
                        .arg(request.renderEffects ? QStringLiteral("on")
                                                   : QStringLiteral("off"))
                        .arg(renderTimer.nsecsElapsed() / 1000000.0, 0, 'f', 3));
    }
    emit frameReady(request.frameIndex, QByteArray(reinterpret_cast<const char*>(frame.constBits()),
                                                   frame.sizeInBytes()),
                    frame.size(), m_frameComplete ? request.cacheKey.value() : QString());
}

RenderManager::RenderManager(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<FrameRequest>("FrameRequest");
}

RenderManager::~RenderManager()
{
    cancelAll();
}

void RenderManager::spawnWorker()
{
    m_workerThread = new QThread(this);
    m_workerThread->setObjectName(QStringLiteral("openvegas-render-worker"));
    m_worker = new RenderWorker();
    m_worker->setComposition(m_composition);
    m_worker->setMediaManager(m_mediaManager);
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &RenderWorker::frameReady, this, &RenderManager::onFrameReady);
    connect(m_worker, &RenderWorker::failed, this, [this](int frameIndex, const QString& message) {
        OV_LOG_ERROR(QStringLiteral("Render failed: %1").arg(message));
        if (m_caching && frameIndex == m_cacheRequest) {
            m_caching = false;
            --m_cacheRequest;
            emit playbackCacheFailed(message);
        }
        m_state = RenderState::Failed;
        emit stateChanged(m_state);
    });
    m_workerThread->start();
}

void RenderManager::requestFrame(int frameIndex, double timeSeconds, const QSize& size,
                                 bool renderEffects, bool transparentBackground)
{
    if (!size.isValid() || size.isEmpty()) {
        return;
    }

    FrameRequest request;
    request.frameIndex = frameIndex;
    request.timeSeconds = timeSeconds;
    request.size = size;
    request.renderEffects = renderEffects;
    request.transparentBackground = transparentBackground;
    request.preRenderDirectory = m_preRenderDirectory;
    if (m_composition) {
        request.compositionSnapshot = std::make_shared<composition::Composition>(*m_composition);
        request.cacheKey = core::Identifier(
            frameCacheKey(timeSeconds, size, renderEffects, transparentBackground));
        QByteArray cachedFrame;
        if (auto* cached = m_frameCache.object(request.cacheKey.value())) {
            cachedFrame = *cached;
        } else if (m_diskCache.load(request.cacheKey.value(), size, &cachedFrame)) {
            ++m_diskCacheHits;
            m_frameCache.insert(request.cacheKey.value(), new QByteArray(cachedFrame),
                                qMax(1, int(cachedFrame.size() / 1024)));
        }
        if (!cachedFrame.isEmpty()) {
            QTimer::singleShot(0, this, [this, frameIndex, cachedFrame, size,
                                         key = request.cacheKey.value()] {
                onFrameReady(frameIndex, cachedFrame, size, key);
            });
            return;
        }
    }
    if (!m_workerThread) {
        spawnWorker();
    }

    m_state = RenderState::Rendering;
    emit stateChanged(m_state);

    QMetaObject::invokeMethod(m_worker, "renderFrame", Qt::QueuedConnection,
                              Q_ARG(FrameRequest, request), Q_ARG(QByteArray, QByteArray()));
}

void RenderManager::cancelAll()
{
    cancelPlaybackCache();
    if (m_workerThread) {
        QMetaObject::invokeMethod(
            m_worker,
            [] { plugin::releaseNativeEffectThreadRenderer(); },
            Qt::BlockingQueuedConnection);
        m_workerThread->quit();
        m_workerThread->wait();
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
        m_worker = nullptr;
    }
    m_state = RenderState::Idle;
    emit stateChanged(m_state);
}

QString renderStateKey(const composition::Composition& composition,
                       const media::MediaManager* media, double timeSeconds, const QSize& size,
                       bool renderEffects, bool transparentBackground, const QString& variant)
{
    QByteArray signature;
    QDataStream stream(&signature, QIODevice::WriteOnly);
    stream << timeSeconds << size << renderEffects << variant;
    if (transparentBackground) stream << QStringLiteral("transparent");
    // The disk caches outlive the session, so the key covers what the old
    // memory-only key could get away with missing: nested shots' own layers
    // and media files replaced under the same path.
    QSet<const composition::Composition*> visited;
    std::function<void(const composition::Composition&)> append =
        [&](const composition::Composition& shot) {
        if (visited.contains(&shot)) return;
        visited.insert(&shot);
        stream << composition::layerState(shot.layers()) << shot.width() << shot.height()
               << shot.fpsNumerator() << shot.fpsDenominator();
        // A non-square pixel changes the picture; square ones keep old keys.
        if (shot.pixelAspectValue() != 1.0) stream << shot.pixelAspectValue();
        // The shutter only matters once a layer asks for motion blur; keys of
        // shots without any stay what they were.
        const bool blurred = std::any_of(shot.layers().cbegin(), shot.layers().cend(),
                                         [](const composition::Layer& l) { return l.motionBlur; });
        if (blurred) {
            const composition::CompositionRenderSettings& s = shot.renderSettings();
            stream << s.motionBlurEnabled << s.shutterAngle << s.shutterPhase
                   << s.maxNumOfSamples << s.useAdaptiveSamples;
        }
        // 3D layers are seen through the camera since October 2026 (they
        // used to be drawn flat), and fog reaches only them.
        const bool deep = std::any_of(shot.layers().cbegin(), shot.layers().cend(),
                                      [](const composition::Layer& l) {
            return l.dimension == composition::LayerDimension::ThreeD
                   || l.kind == composition::LayerKind::Model3D;
        });
        if (deep) {
            const composition::CompositionRenderSettings& s = shot.renderSettings();
            stream << QStringLiteral("perspective-1") << s.fogEnabled;
            if (s.fogEnabled)
                stream << s.fogNearDistance << s.fogFarDistance << s.fogDensity << s.fogFalloff
                       << s.fogColor;
        }
        // Native modules got the time (2D effects), milliseconds, their
        // opacity back and their layer (Behaviors) in October 2026: frames
        // cached before that show them standing still.
        const bool native = std::any_of(shot.layers().cbegin(), shot.layers().cend(),
                                        [](const composition::Layer& l) {
            return std::any_of(l.clips.cbegin(), l.clips.cend(), [](const composition::Clip& c) {
                return std::any_of(c.effects.cbegin(), c.effects.cend(), [](const composition::Effect& e) {
                    return !e.pluginId.value().startsWith(QLatin1String("openvegas.builtin."));
                });
            });
        });
        if (native) stream << QStringLiteral("native-time-1");
        for (const composition::Layer& layer : shot.layers()) {
            for (const composition::Clip& clip : layer.clips) {
                if (clip.nestedComposition) {
                    append(*clip.nestedComposition);
                } else if (media && clip.mediaId.isValid()) {
                    const media::MediaAsset asset = media->assetById(clip.mediaId);
                    const QFileInfo file(asset.sourcePath());
                    stream << file.absoluteFilePath() << file.size()
                           << file.lastModified().toMSecsSinceEpoch()
                           << qint64(asset.sequenceFiles().size());
                    // Non-square footage is drawn wider; square keeps old keys.
                    if (asset.pixelAspectValue() != 1.0) stream << asset.pixelAspectValue();
                    // Overrides that change which frame or which pixels.
                    if (asset.overridesFrameRate() || asset.alphaMode() != 0
                        || asset.colorLevels() != 0 || asset.colorSpace() != 0) {
                        stream << asset.frameRate() << asset.alphaMode() << asset.colorLevels()
                               << asset.colorSpace();
                    }
                }
            }
        }
    };
    append(composition);
    return QString::fromLatin1(
        QCryptographicHash::hash(signature, QCryptographicHash::Sha256).toHex());
}

QString preRenderFolder(const QString& projectPreRenderDirectory,
                        const composition::Composition& composition)
{
    if (projectPreRenderDirectory.isEmpty() || !composition.id().isValid()) return {};
    QString name = composition.id().value();
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    return QDir(projectPreRenderDirectory).filePath(name);
}

QImage RenderWorker::loadPreRenderedFrame(const composition::Composition& shot,
                                          double localSeconds, const QSize& size) const
{
    // Reference AssetPreRenderMenu "Make Pre-Render(s)": a shot rendered once
    // to disk is read back frame by frame. Only a pre-render of the shot's
    // current state matches the key; anything edited since renders live.
    const QString folder = preRenderFolder(m_preRenderDirectory, shot);
    if (folder.isEmpty()) return {};
    const double fps = shot.fpsDenominator() > 0
        ? double(shot.fpsNumerator()) / shot.fpsDenominator() : 30.0;
    const int frame = qMax(0, int(std::floor(localSeconds * fps + 1e-6)));
    FrameDiskCache cache;
    cache.setDirectory(folder);
    QByteArray rgba;
    if (!cache.load(renderStateKey(shot, m_mediaManager.get(), frame / fps, size, true, true),
                    size, &rgba)) {
        return {};
    }
    return QImage(reinterpret_cast<const uchar*>(rgba.constData()), size.width(), size.height(),
                  size.width() * 4, QImage::Format_RGBA8888).copy();
}

QString RenderManager::frameCacheKey(double timeSeconds, const QSize& size,
                                     bool renderEffects, bool transparentBackground) const
{
    return renderStateKey(*m_composition, m_mediaManager.get(), timeSeconds, size,
                          renderEffects, transparentBackground, m_mediaVariant);
}

void RenderManager::onFrameReady(int frameIndex, QByteArray rgba, QSize size, QString cacheKey)
{
    if (!cacheKey.isEmpty()) m_frameCache.insert(cacheKey, new QByteArray(rgba), qMax(1, int(rgba.size() / 1024)));
    // Frames rendered for the playback cache also go to the timeline cache.
    if (frameIndex < 0 && !cacheKey.isEmpty() && m_diskCache.isEnabled()
        && !m_diskCache.contains(cacheKey)) {
        m_diskCache.store(cacheKey, size, rgba);
    }
    if (frameIndex < 0) {
        if (!m_caching || frameIndex != m_cacheRequest) return;
        if (cacheKey.isEmpty()) {
            if (!m_cacheWait.isValid()) m_cacheWait.start();
            if (m_cacheWait.elapsed() > 10000) {
                cancelPlaybackCache(); emit playbackCacheFailed(tr("Video decoding did not deliver the requested cache frame.")); return;
            }
            QTimer::singleShot(50, this, &RenderManager::requestNextCacheFrame);
            return;
        }
        m_cacheWait.invalidate();
        ++m_cacheFrame;
        emit playbackCacheProgress(m_cacheFrame - m_cacheFirst, m_cacheLast - m_cacheFirst);
        requestNextCacheFrame();
        return;
    }
    m_state = RenderState::Done;
    emit stateChanged(m_state);
    emit frameReady(frameIndex, rgba, size);
}

void RenderManager::startPlaybackCache(double fromSeconds, const QSize& size, bool renderEffects,
                                       bool transparentBackground)
{
    cancelPlaybackCache();
    m_cacheWait.invalidate();
    if (!m_composition || size.isEmpty()) return;
    const double fps = double(m_composition->fpsNumerator()) / qMax(1, m_composition->fpsDenominator());
    if (fps <= 0) return;
    m_cacheFrame = m_cacheFirst = qMax(0, qRound(fromSeconds * fps));
    const qint64 frameBytes = qint64(size.width()) * size.height() * 4;
    const int capacity = int((qint64(m_frameCache.maxCost()) * 1024) / qMax(qint64(1), frameBytes));
    // With the timeline cache on disk the range is no longer bounded by the
    // memory budget; frames already there are read back, not re-rendered.
    const int lastFrame = qRound(m_composition->durationSeconds() * fps);
    m_cacheLast = m_diskCache.isEnabled() ? lastFrame : qMin(lastFrame, m_cacheFirst + capacity);
    m_cacheSize = size; m_cacheEffects = renderEffects; m_caching = true;
    m_cacheTransparent = transparentBackground;
    requestNextCacheFrame();
}
void RenderManager::cancelPlaybackCache()
{
    if (!m_caching) return;
    m_caching = false; --m_cacheRequest; emit playbackCacheFinished();
}
void RenderManager::requestNextCacheFrame()
{
    if (!m_caching || !m_composition) return;
    if (m_cacheFrame >= m_cacheLast) {
        m_caching = false;
        m_state = RenderState::Done;
        emit stateChanged(m_state);
        emit playbackCacheFinished();
        return;
    }
    const double fps = double(m_composition->fpsNumerator()) / qMax(1, m_composition->fpsDenominator());
    requestFrame(--m_cacheRequest, m_cacheFrame / fps, m_cacheSize, m_cacheEffects,
                 m_cacheTransparent);
}

} // namespace render
} // namespace openvegas
