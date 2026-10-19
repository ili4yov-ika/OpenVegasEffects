#include "render/RenderManager.h"
#include "composition/CompositionState.h"
#include <QCryptographicHash>
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
#include "render/LayerGeometry.h"
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

QImage translatedSubpixel(const QImage& image, double dx, double dy);

// `image` moved by `transform` about its centre: a whole translation as a
// sub-pixel shift, anything else painted through.
QImage movedAboutCentre(const QImage& image, const QTransform& transform)
{
    if (transform.isIdentity()) return image;
    if (transform.type() == QTransform::TxTranslate) return translatedSubpixel(image, transform.dx(), transform.dy());
    QImage transformed(image.size(), QImage::Format_RGBA8888);
    transformed.fill(Qt::transparent);
    QPainter painter(&transformed);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QPointF centre(image.width() / 2.0, image.height() / 2.0);
    painter.translate(centre);
    painter.setTransform(transform, true);
    painter.translate(-centre);
    painter.drawImage(QPoint(0, 0), image);
    painter.end();
    return transformed;
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
plugin::NativeBehaviorLayer behaviorLayerAt(const composition::Composition* composition,
                                            const composition::Layer& layer,
                                            const composition::Clip& clip,
                                            const composition::Effect* text, int frame,
                                            const QSize& canvas,
                                            const media::MediaManager* media)
{
    plugin::NativeBehaviorLayer info;
    info.composition = canvas;
    info.world = nativeLayerWorld(composition, layer, frame);

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

// The image moved by (dx, dy) pixels, between pixels too: Qt's raster engine
// puts an image that is only translated on whole pixels, while a module's
// move - and the same move in a reduced preview - is rarely whole. Bilinear,
// in premultiplied space, transparent beyond the edges.
QImage translatedSubpixel(const QImage& image, double dx, double dy)
{
    const QImage source = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    QImage out(source.size(), QImage::Format_RGBA8888_Premultiplied);
    out.fill(Qt::transparent);
    const int ix = int(std::floor(dx)), iy = int(std::floor(dy));
    const double fx = dx - ix, fy = dy - iy;
    // Each output pixel mixes the source pixel (x - ix, y - iy) with its
    // neighbours up and to the left.
    const double w11 = (1.0 - fx) * (1.0 - fy), w01 = fx * (1.0 - fy);
    const double w10 = (1.0 - fx) * fy, w00 = fx * fy;
    const int width = source.width(), height = source.height();
    const auto pixel = [&](int x, int y, int channel) -> double {
        if (x < 0 || y < 0 || x >= width || y >= height) return 0.0;
        return source.constScanLine(y)[x * 4 + channel];
    };
    for (int y = qMax(0, iy); y < qMin(height, height + iy + 1); ++y) {
        uchar* row = out.scanLine(y);
        for (int x = qMax(0, ix); x < qMin(width, width + ix + 1); ++x) {
            const int sx = x - ix, sy = y - iy;
            for (int c = 0; c < 4; ++c) {
                const double value = pixel(sx, sy, c) * w11 + pixel(sx - 1, sy, c) * w01
                    + pixel(sx, sy - 1, c) * w10 + pixel(sx - 1, sy - 1, c) * w00;
                row[x * 4 + c] = uchar(qBound(0, int(std::lround(value)), 255));
            }
        }
    }
    return out.convertToFormat(QImage::Format_RGBA8888);
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
    return textEffectOfClip(clip);
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
                                double localTimeSeconds, const QSize& canvasSize, int frame,
                                const QSize& reducedSpace) const
{
    // A 3D layer is seen through the shot's camera. Text that Geometry
    // modules turned into a mesh is already 3D and keeps the mesh path below.
    if (layerDrawnAsSheet(layer, &clip)) return renderClipIn3D(layer, clip, localTimeSeconds, canvasSize, frame);

    const composition::LayerTransform& transform = layer.transform;
    QImage canvas(canvasSize, QImage::Format_RGBA8888);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    // A reduced preview draws in the shot's full square units through its
    // scale: positions, sizes and text stay as they are.
    QSize space = canvasSize;
    if (m_renderScale < 1.0 && m_composition) {
        space = reducedSpace.isValid() ? reducedSpace : m_composition->displaySize();
        painter.scale(double(canvasSize.width()) / space.width(),
                      double(canvasSize.height()) / space.height());
        // Edges smoothed as a full frame reduced would have them.
        painter.setRenderHint(QPainter::Antialiasing);
    }
    // Under its parent (AbstractLayer::ParentTransformationAtTime).
    const QTransform parent = layerCanvasParentAt(m_composition.get(), layer, frame, QSizeF(space));
    if (!parent.isIdentity()) {
        painter.setTransform(parent, true);
        painter.setRenderHint(QPainter::Antialiasing);
    }

    // A Plane is its own content: a solid fill placed by the layer transform,
    // so it needs neither media nor the placeholder below.
    if (layer.kind == composition::LayerKind::Plane) {
        const QPointF scalePct = transform.scaleAt(frame);
        const QPointF pos = transform.positionAt(frame);
        const QSizeF scaled(space.width() * scalePct.x() / 100.0,
                            space.height() * scalePct.y() / 100.0);
        painter.translate(space.width() / 2.0 + pos.x(),
                          space.height() / 2.0 - pos.y());
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
    // The content's size in the shot's units: the footage's pixels, or a
    // nested shot's square size however many pixels it was drawn with.
    QSizeF contentSize;
    if (clip.nestedComposition) {
        // Render the child in its own square space (width x PAR by height),
        // then let the parent layer transform place it exactly like footage
        // or a still image. A pre-render holds the child's pixels, so it is
        // stretched to that shape.
        const QSize childSize = clip.nestedComposition->displaySize();
        contentSize = childSize;
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
            nested.m_antialiasingMode = antialiasingMode();
            // A reduced preview draws the child reduced too.
            media = nested.renderShotImage(localTimeSeconds, m_renderScale, true, true);
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
        if (contentSize.isEmpty()) contentSize = media.size();
        const QSizeF scaled(contentSize.width() * aspect * scalePct.x() / 100.0,
                            contentSize.height() * scalePct.y() / 100.0);
        const QPointF centre(space.width() / 2.0 + pos.x(),
                             space.height() / 2.0 - pos.y());
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
        composition::TextStyle unused;
        QVector<const composition::Effect*> geometryEffects;
        if (textGeometryOf(clip, frame, &unused, &geometryEffects)) {
            painter.end();
            return renderTextGeometry(layer, clip, style, geometryEffects, canvasSize, frame);
        }
        // Text follows the same layer transform as any other content.
        const QPointF scalePct = transform.scaleAt(frame);
        const QPointF pos = transform.positionAt(frame);

        painter.translate(space.width() / 2.0 + pos.x(),
                          space.height() / 2.0 - pos.y());
        const double angle = transform.rotationAt(frame);
        if (!qFuzzyIsNull(angle)) {
            painter.rotate(angle);
        }
        painter.scale(scalePct.x() / 100.0, scalePct.y() / 100.0);
        // The text box is the frame, centred on the origin - where the reference
        // puts a text box's own centre.
        const QRectF box(-space.width() / 2.0, -space.height() / 2.0,
                         space.width(), space.height());
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
                        space.width(), space.height(), fps,
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
        painter.fillRect(QRect(QPoint(0, 0), space), color.darker(120));

        const int bars = 16;
        const int inset = 40;
        const double phase = qSin(localTimeSeconds * 3.0) * 0.5 + 0.5;
        for (int i = 0; i < bars; ++i) {
            const int opacity = 30 + static_cast<int>(25.0 * phase * ((i % 3) + 1));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(color.lighter(150).red(), color.lighter(150).green(),
                                    color.lighter(150).blue(), opacity));
            painter.drawRect(inset + i * (space.width() - 2 * inset) / bars,
                             0, (space.width() - 2 * inset) / bars, space.height());
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QColor(255, 255, 255, 220));
        painter.drawText(QRect(0, 0, space.width(), space.height()),
                         Qt::AlignCenter, clipLabel(clip.mediaId));
    }

    return canvas;
}

namespace {

// A Behavior's parameters at `frame`, between frames too: numbers in between
// their values at the frames either side, the rest as at the frame before.
QStringList behaviorValuesAt(const composition::Effect& fx, double frame)
{
    const int whole = int(std::floor(frame));
    const double t = frame - whole;
    QStringList values = fx.parameterValues;
    for (int parameter = 0; parameter < values.size(); ++parameter) {
        const QVariant before = fx.parameterAt(parameter, whole);
        values[parameter] = before.toString();
        if (t <= 1e-9) continue;
        bool a = false, b = false;
        const double from = before.toString().toDouble(&a);
        const double to = fx.parameterAt(parameter, whole + 1).toString().toDouble(&b);
        if (a && b && from != to) values[parameter] = QString::number(from + (to - from) * t, 'g', 12);
    }
    return values;
}

// The Behaviors of `clip` at `frame` (between frames too, as the reference
// runs them at the sub-frame's time): their matrices in the clip's order -
// from the frame's centre, Y up - and their opacity together. Simulated
// Behaviors give no matrix here (SimulateLayers moves the body).
struct ClipBehaviors
{
    QVector<QMatrix4x4> matrices;
    double opacity = 1.0;
};

ClipBehaviors clipBehaviorsAt(const composition::Composition& composition, const media::MediaManager* media,
                              const composition::Layer& layer, const composition::Clip& clip, double frame,
                              const QSize& scene)
{
    ClipBehaviors result;
    const double rate = composition.framesPerSecond();
    const int whole = int(std::floor(frame));
    const double fraction = frame - whole;
    const int localFrame = whole - int(std::lround(clip.startSeconds * rate));
    const int durationFrames = qMax(1, qRound(clip.durationSeconds * rate));
    const qsizetype clipIndex = &clip - layer.clips.constData();
    for (qsizetype effectIndex = 0; effectIndex < clip.effects.size(); ++effectIndex) {
        const composition::Effect& fx = clip.effects.at(effectIndex);
        if (fx.enabled && plugin::nativeBehaviorUsesInstance(fx.pluginId)) {
            // Matrices the module's own instance gave on the GUI thread, on
            // the canvas from its centre, Y up (NativeInstanceHost::bake).
            std::array<float, 16> matrix {};
            if (clipIndex >= 0 && clipIndex < layer.clips.size()
                && plugin::nativeInstanceTransformAt(nativeInstanceKey(layer.id, int(clipIndex), int(effectIndex)),
                                                     frame, &matrix))
                result.matrices.append(QMatrix4x4(matrix.data()).transposed());
            continue;
        }
        if (!fx.enabled || !plugin::nativeBehaviorRenderingVerified(fx.pluginId)
            || plugin::nativeBehaviorSubObjectRenderingVerified(fx.pluginId))
            continue;
        const QStringList values = behaviorValuesAt(fx, frame);
        plugin::NativeBehaviorResult behavior;
        if (!plugin::evaluateNativeBehaviorFrame(behavior, whole, localFrame, durationFrames, scene.width(),
                                                 scene.height(), rate, fx.pluginId, values, &composition, layer.id,
                                                 behaviorLayerAt(&composition, layer, clip, textEffectOfClip(clip), whole,
                                                                 scene, media),
                                                 fraction))
            continue;
        result.opacity *= behavior.opacity;
        result.matrices.append(behavior.transformation);
    }
    return result;
}

const composition::Clip* clipShownAt(const composition::Composition& composition,
                                     const composition::Layer& layer, int frame);

// The Behaviors' matrices of the layer's clip at `frame`, in the clip's
// order - from the frame's centre, Y up - whatever the layer's dimension.
// The simulation its simulated Behaviors drive goes to `simulation` instead:
// it is no Behavior matrix but where the simulated body is, which takes the
// place of the layer's whole world (FUN_18036c010 after SimulateLayers).
// While a motion blur sample of a sheet is drawn, they are read between
// frames (behaviorFrameShift).
QVector<QMatrix4x4> behaviorMatricesAt(const composition::Composition& composition,
                                       const media::MediaManager* media,
                                       const composition::Layer& layer, int frame,
                                       QMatrix4x4* simulation = nullptr)
{
    if (simulation) simulation->setToIdentity();
    const double at = frame + behaviorFrameShift();
    const composition::Clip* clip = clipShownAt(composition, layer, int(std::floor(at)));
    if (!clip) return {};
    QVector<QMatrix4x4> total = clipBehaviorsAt(composition, media, layer, *clip, at, composition.displaySize()).matrices;
    QVector3D offset;
    if (simulation && simulationSuppressed() == 0
        && layerSimulationOffsetAt(composition, media, layer, at, &offset))
        simulation->translate(offset);
    return total;
}

const composition::Clip* clipShownAt(const composition::Composition& composition,
                                     const composition::Layer& layer, int frame)
{
    const double seconds = frame / composition.framesPerSecond();
    const composition::Clip* clip = nullptr;
    for (const composition::Clip& candidate : layer.clips)
        if (seconds >= candidate.startSeconds && seconds < candidate.endSeconds()) clip = &candidate;
    return clip;
}

} // namespace

bool layerDrawnAsSheet(const composition::Layer& layer, const composition::Clip* clip)
{
    if (layer.dimension != composition::LayerDimension::ThreeD || layer.kind == composition::LayerKind::Model3D)
        return false;
    if (!clip || !textEffectOfClip(*clip)) return true;
    return std::none_of(clip->effects.cbegin(), clip->effects.cend(), [](const composition::Effect& fx) {
        return fx.enabled && plugin::nativeGeometryRenderingVerified(fx.pluginId);
    });
}

QTransform layerBehaviorTransformAt(const composition::Composition& composition,
                                    const media::MediaManager* media,
                                    const composition::Layer& layer, int frame)
{
    QTransform total;
    if (layer.dimension == composition::LayerDimension::ThreeD
        && layerDrawnAsSheet(layer, clipShownAt(composition, layer, frame)))
        return total;
    // As applyClipBehaviors puts each one on the picture: about the canvas's
    // centre, the module's Y-up matrix mirrored into rows.
    const QSize scene = composition.displaySize();
    const QPointF centre(scene.width() / 2.0, scene.height() / 2.0);
    QMatrix4x4 simulation;
    for (const QMatrix4x4& matrix : behaviorMatricesAt(composition, media, layer, frame, &simulation)) {
        const float* m = matrix.constData();
        const QTransform transform(double(m[0]), -double(m[1]), -double(m[4]), double(m[5]),
                                   double(m[12]), -double(m[13]));
        // B1 x B2 x ...: the last one acts first (applyClipBehaviors).
        total = QTransform::fromTranslate(-centre.x(), -centre.y()) * transform
                * QTransform::fromTranslate(centre.x(), centre.y()) * total;
    }
    // They act inside the parent (its world x the Behaviors x the layer's
    // own): after the parent on the canvas, that is the parent undone, them,
    // the parent again.
    const QTransform parent = layerCanvasParentAt(&composition, layer, frame, QSizeF(scene));
    bool invertible = true;
    const QTransform unparent = parent.inverted(&invertible);
    if (!parent.isIdentity() && invertible && !total.isIdentity()) total = unparent * total * parent;
    // The simulated body is moved in the world, after all of that.
    const float* s = simulation.constData();
    if (!simulation.isIdentity()) total = total * QTransform::fromTranslate(double(s[12]), -double(s[13]));
    return total;
}

QMatrix4x4 layerOwnBehaviorMatrixAt(const composition::Composition& composition,
                                    const media::MediaManager* media,
                                    const composition::Layer& layer, int frame, QMatrix4x4* simulation)
{
    if (simulation) simulation->setToIdentity();
    QMatrix4x4 total;
    for (const QMatrix4x4& matrix : behaviorMatricesAt(composition, media, layer, frame, simulation)) total *= matrix;
    return total;
}

bool layerIsSimulatedBody(const composition::Layer& layer)
{
    for (const composition::Clip& clip : layer.clips)
        for (const composition::Effect& fx : clip.effects)
            if (fx.enabled && plugin::nativeBehaviorSimulationRenderingVerified(fx.pluginId)) return true;
    return false;
}

namespace {

// Runs kept by the state of the shot they were made for.
QMutex g_simulationMutex;
QHash<QByteArray, plugin::NativeSimulationTrack> g_simulationTracks;
QList<QByteArray> g_simulationOrder;

QByteArray simulationKeyOf(const composition::Composition& composition)
{
    QByteArray state = composition::layerState(composition.layers());
    state += QByteArray::number(composition.framesPerSecond(), 'g', 12);
    state += QByteArray::number(composition.durationSeconds(), 'g', 12);
    state += QByteArray::number(composition.displaySize().width()) + 'x'
             + QByteArray::number(composition.displaySize().height());
    return QCryptographicHash::hash(state, QCryptographicHash::Sha1);
}

} // namespace

bool layerSimulationOffsetAt(const composition::Composition& composition, const media::MediaManager* media,
                             const composition::Layer& layer, double frame, QVector3D* offset)
{
    if (!offset || simulationSuppressed() > 0) return false;
    const QVector<composition::Layer>& layers = composition.layers();
    int source = -1;
    for (int i = 0; i < layers.size(); ++i)
        if (layers.at(i).id == layer.id) source = i;
    if (source < 0) return false;
    // Quick refusal: no simulated Behavior on it.
    if (!layerIsSimulatedBody(layer)) return false;
    const double rate = composition.framesPerSecond();
    const int whole = int(std::ceil(frame - 1e-9));
    const auto clipOf = [&](int index, int at) { return clipShownAt(composition, layers.at(index), at); };
    // A layer's range: its clip's, or its span (StartFrame .. EndFrame).
    const auto firstClipOf = [&](int index) -> const composition::Clip* {
        const composition::Layer& candidate = layers.at(index);
        const composition::Clip* clip = clipOf(index, whole);
        if (!clip && !candidate.clips.isEmpty()) clip = &candidate.clips.first();
        return clip;
    };
    plugin::NativeSimulationScene scene;
    scene.layerCount = layers.size();
    scene.worldAt = [&](int index, int at) {
        return layerWorldWithoutSimulationAt(&composition, layers.at(index), at);
    };
    scene.behaviorsAt = [&](int index, int at) {
        QVector<plugin::NativeBehaviorRequest> requests;
        const composition::Clip* clip = clipOf(index, at);
        if (!clip) clip = firstClipOf(index);
        if (!clip) return requests;
        for (const composition::Effect& fx : clip->effects) {
            if (!fx.enabled || !plugin::nativeBehaviorSimulationRenderingVerified(fx.pluginId)) continue;
            QStringList values = fx.parameterValues;
            for (int parameter = 0; parameter < values.size(); ++parameter)
                values[parameter] = fx.parameterAt(parameter, at).toString();
            requests.push_back({fx.pluginId, values});
        }
        return requests;
    };
    scene.activeAt = [&](int index, int at) {
        const composition::Layer& candidate = layers.at(index);
        if (composition::layerHasSpan(candidate)) return composition::layerSpanCovers(candidate, at, rate);
        return clipOf(index, at) != nullptr;
    };
    scene.startFrame = [&](int index) {
        const composition::Layer& candidate = layers.at(index);
        if (composition::layerHasSpan(candidate)) return int(std::lround(candidate.spanStartSeconds * rate));
        const composition::Clip* clip = firstClipOf(index);
        return clip ? int(std::lround(clip->startSeconds * rate)) : 0;
    };
    scene.endFrame = [&](int index) {
        const composition::Layer& candidate = layers.at(index);
        if (composition::layerHasSpan(candidate))
            return int(std::lround(composition::layerSpanEnd(candidate, composition.durationSeconds()) * rate));
        const composition::Clip* clip = firstClipOf(index);
        return clip ? int(std::lround(clip->endSeconds() * rate)) : 0;
    };
    scene.parentOf = [&](int index) {
        const core::Identifier parent = layers.at(index).parentLayerId;
        if (!parent.isValid()) return -1;
        for (int i = 0; i < layers.size(); ++i)
            if (layers.at(i).id == parent) return i;
        return -1;
    };
    scene.flatAt = [&](int index) { return layers.at(index).dimension == composition::LayerDimension::TwoD; };
    // FUN_18036c710: footage its asset's size, a grade (and a plane, a
    // frame-sized asset) the frame's, text its box.
    scene.sizeOf = [&](int index) {
        const composition::Layer& candidate = layers.at(index);
        const composition::Clip* clip = firstClipOf(index);
        if (candidate.kind == composition::LayerKind::Grade || candidate.kind == composition::LayerKind::Plane)
            return composition.displaySize();
        if (!clip) return QSize();
        if (const composition::Effect* text = textEffectOfClip(*clip)) {
            QStringList values = text->parameterValues;
            for (int p = 0; p < values.size(); ++p) values[p] = text->parameterAt(p, whole).toString();
            const composition::TextStyle style = composition::textStyleFromParameters(values);
            if (style.textMode == composition::TextStyle::TextMode::Paragraph) return style.paragraphSize.toSize();
            return QSize();
        }
        if (clip->nestedComposition) return clip->nestedComposition->displaySize();
        if (media && clip->mediaId.isValid()) return media->assetById(clip->mediaId).frameSize();
        return QSize();
    };
    // The run so far for this state of the shot, gone on from where it
    // stopped when it does not reach the frame yet.
    const QByteArray key = simulationKeyOf(composition);
    plugin::NativeSimulationTrack track;
    {
        QMutexLocker lock(&g_simulationMutex);
        track = g_simulationTracks.value(key);
    }
    if (!track.reaches(whole)) {
        const QSize canvas = composition.displaySize();
        if (!plugin::simulateNativeBehaviorScene(track, whole, canvas.width(), canvas.height(), rate, scene,
                                                 &composition))
            return false;
        QMutexLocker lock(&g_simulationMutex);
        if (!g_simulationTracks.contains(key)) {
            g_simulationOrder.append(key);
            while (g_simulationOrder.size() > 8) g_simulationTracks.remove(g_simulationOrder.takeFirst());
        }
        if (!g_simulationTracks.value(key).reaches(track.last())) g_simulationTracks.insert(key, track);
    }
    const int body = int(track.bodies.indexOf(source));
    if (body < 0) return false;
    if (whole < track.first) {
        *offset = QVector3D();
        return true;
    }
    const int at = qMin(whole, track.last()) - track.first;
    QVector3D position = track.positions.at(at).at(body);
    QVector3D point = track.points.at(at).at(body);
    const double back = double(whole) - frame;
    if (back > 1e-6 && at > 0) {
        // Taken back along its velocity; the world's point in between.
        position -= track.velocities.at(at).at(body) * float(back / rate);
        const QVector3D before = track.points.at(at - 1).at(body);
        point = before + (point - before) * float(1.0 - back);
    }
    *offset = position - point;
    return true;
}

QMatrix4x4 layerBehaviorMatrixAt(const composition::Composition& composition,
                                 const media::MediaManager* media,
                                 const composition::Layer& layer, int frame, QMatrix4x4* simulation)
{
    if (simulation) simulation->setToIdentity();
    QMatrix4x4 total;
    if (!layerDrawnAsSheet(layer, clipShownAt(composition, layer, frame))) return total;
    for (const QMatrix4x4& matrix : behaviorMatricesAt(composition, media, layer, frame, simulation)) total *= matrix;
    return total;
}

std::array<float, 16> nativeLayerWorld(const composition::Composition* composition,
                                       const composition::Layer& layer, int frame)
{
    const composition::LayerTransform& t = layer.transform;
    const QPointF position = t.positionAt(frame);
    const QPointF scale = t.scaleAt(frame);
    // Under its parent (WorldTransformationAtTime: the parent's
    // ParentingWorldTransformationAtTime first).
    QMatrix4x4 world = layerParentMatrixAt(composition, layer, frame);
    world.translate(float(position.x()), float(position.y()), float(t.positionZAt(frame)));
    world.translate(float(t.anchorPoint.x()), float(t.anchorPoint.y()));
    world.rotate(float(-t.rotationAt(frame)), 0.0f, 0.0f, 1.0f);
    world.translate(float(-t.anchorPoint.x()), float(-t.anchorPoint.y()));
    world.scale(float(scale.x() / 100.0), float(scale.y() / 100.0), 1.0f);
    std::array<float, 16> result {};
    std::copy(world.constData(), world.constData() + 16, result.begin());
    return result;
}

plugin::NativeAssetInfo nativeFootageInfo(const composition::Clip& clip,
                                          const media::MediaManager* media, double shotRate)
{
    plugin::NativeAssetInfo info;
    if (clip.nestedComposition) {
        // A composite shot (tagBiffAssetInfo type 3): its frames at the
        // shot's rate, from where the clip starts in it.
        const double rate = shotRate > 0.0 ? shotRate : 30.0;
        info.valid = true;
        info.type = 3;
        info.frameRate = rate;
        info.frameCount = qMax(1, int(std::floor(clip.nestedComposition->durationSeconds() * rate)));
        info.startFrame = qMax(0, int(std::lround(clip.sourceStartSeconds * rate)));
        info.endFrame = info.startFrame + qMax(1, int(std::lround(clip.durationSeconds * rate))) - 1;
        info.key = QStringLiteral("composition:") + clip.nestedComposition->id().value();
        return info;
    }
    if (!media || !clip.mediaId.isValid()) return info;
    const media::MediaAsset asset = media->assetById(clip.mediaId);
    if (!asset.isValid()) return info;
    const double rate = shotRate > 0.0 ? shotRate : 30.0;
    if (asset.isImageSequence()) {
        info.frameCount = int(asset.sequenceFiles().size());
        info.frameRate = asset.sequenceFrameRate() > 0.0 ? asset.sequenceFrameRate() : rate;
    } else if (asset.kind() == media::MediaKind::Video) {
        info.frameRate = rate;
        info.frameCount = qMax(1, int(std::floor(asset.durationSeconds() * info.frameRate)));
    } else {
        return info;
    }
    info.valid = true;
    info.type = 0;
    info.startFrame = qMax(0, int(std::lround(clip.sourceStartSeconds * info.frameRate)));
    info.endFrame = info.startFrame + qMax(1, int(std::lround(clip.durationSeconds * info.frameRate))) - 1;
    info.key = asset.id().value();
    return info;
}

QImage renderCompositionFrame(const std::shared_ptr<composition::Composition>& shot,
                              const std::shared_ptr<media::MediaManager>& media, double seconds,
                              const QString& preRenderDirectory)
{
    if (!shot) return QImage();
    return RenderWorker::renderComposition(shot, media, seconds, preRenderDirectory);
}

QString nativeInstanceKey(const core::Identifier& layerId, int clip, int effect)
{
    return QStringLiteral("%1/%2/%3").arg(layerId.value()).arg(clip).arg(effect);
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
    time.renderScale = float(m_renderScale);
    // The layer and clip this is, for effects that live on an instance of
    // their own (AutomaticStabilizer, MotionLock): the clip is one of the
    // shot's own.
    const composition::Layer* owner = nullptr;
    int clipIndex = -1;
    if (m_composition) {
        for (const composition::Layer& layer : m_composition->layers()) {
            const qsizetype at = &clip - layer.clips.constData();
            if (at >= 0 && at < layer.clips.size()) {
                owner = &layer;
                clipIndex = int(at);
                break;
            }
        }
    }
    for (qsizetype effectIndex = 0; effectIndex < clip.effects.size(); ++effectIndex) {
        const composition::Effect& fx = clip.effects.at(effectIndex);
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
        // The built-in blur's radius is in the shot's pixels.
        if (m_renderScale < 1.0 && fx.pluginId.value() == QLatin1String(plugin::kBuiltinBlur)
            && !resolved.isEmpty()) {
            resolved[0] = QString::number(resolved.first().toDouble() * m_renderScale);
        }
        // A module blurring motion reads its keyframed values between frames.
        plugin::NativeFrameTime at = time;
        at.valueAt = [&fx](int index, double when) { return fx.parameterAtTime(index, when).toString(); };
        if (owner && plugin::nativeEffectAnalysesOwnLayer(fx.pluginId)) {
            // Drawn from the instance's data (NativeInstanceHost keeps it in
            // the effect); without any yet, the module draws the frame as it is.
            plugin::NativeFrameTime own = at;
            own.instanceKey = nativeInstanceKey(owner->id, clipIndex, int(effectIndex));
            own.instanceData = fx.instanceData;
            own.layerId = owner->id.value();
            own.layerAsset = nativeFootageInfo(clip, m_mediaManager.get(), time.frameRate);
            plugin::applyEffectToImage(image, fx.pluginId, resolved, own);
            continue;
        }
        plugin::applyEffectToImage(image, fx.pluginId, resolved, at);
    }
}

QTransform RenderWorker::clipBehaviorTransform(const composition::Layer& layer, const composition::Clip& clip,
                                               double frame, const QSize& imageSize, const QSize& canvasSize,
                                               double* opacity) const
{
    if (opacity) *opacity = 1.0;
    if (!m_composition) return QTransform();
    // A 3D sheet took its Behaviors' matrices into its place in the scene
    // (layerSheetMatrixAt); only their opacity is left for the picture.
    const bool sheet = layerDrawnAsSheet(layer, &clip);
    // The modules see the shot at full size, also in a reduced preview; what
    // they move is moved by as much of the reduced picture.
    const QSize scene = sceneCanvas(canvasSize);
    const ClipBehaviors behaviors = clipBehaviorsAt(*m_composition, m_mediaManager.get(), layer, clip, frame, scene);
    if (opacity) *opacity = qBound(0.0, behaviors.opacity, 1.0);
    if (sheet) return QTransform();
    const QTransform toScene = QTransform::fromScale(double(scene.width()) / qMax(1, imageSize.width()),
                                                     double(scene.height()) / qMax(1, imageSize.height()));
    // The picture is already under the layer's parent; the Behaviors act
    // inside it (ParentTransformationAtTime: the parent's world x the
    // Behaviors), so each is taken through the parent undone and redone -
    // about the frame's centre, as the matrices are.
    QTransform intoParent, outOfParent;
    const QTransform parent = layerCanvasParentAt(m_composition.get(), layer, int(std::floor(frame)), QSizeF(scene));
    bool invertible = true;
    const QTransform unparent = parent.inverted(&invertible);
    if (!parent.isIdentity() && invertible) {
        const QTransform toCentre = QTransform::fromTranslate(-scene.width() / 2.0, -scene.height() / 2.0);
        const QTransform fromCentre = QTransform::fromTranslate(scene.width() / 2.0, scene.height() / 2.0);
        intoParent = fromCentre * unparent * toCentre;
        outOfParent = fromCentre * parent * toCentre;
    }
    // Behavior matrices are in composition space - from the frame's centre,
    // Y up (Drop lifts a layer's lower edge to +height/2, the top) - so the
    // whole matrix is mirrored into the image's rows, rotation and shear
    // included: a module's clockwise turn stays clockwise on screen.
    // Gathered in the reference's order - B1 x B2 x ..., the last one
    // nearest the layer, so it acts first.
    QTransform combined;
    for (const QMatrix4x4& matrix : behaviors.matrices) {
        const float* m = matrix.constData();
        combined = toScene * intoParent
                   * QTransform(double(m[0]), -double(m[1]), -double(m[4]), double(m[5]),
                                double(m[12]), -double(m[13]))
                   * outOfParent * toScene.inverted() * combined;
    }
    // The simulated body takes the place of the layer's world
    // (FUN_18036c010): its move is in the world, after the parent.
    QVector3D simulated;
    if (layerSimulationOffsetAt(*m_composition, m_mediaManager.get(), layer, frame, &simulated))
        combined = combined * toScene * QTransform::fromTranslate(double(simulated.x()), -double(simulated.y()))
                   * toScene.inverted();
    return combined;
}

double RenderWorker::applyClipBehaviors(QImage& image,
                                        const composition::Layer& layer,
                                        const composition::Clip& clip,
                                        int frame, int localFrame,
                                        const QSize& canvasSize) const
{
    Q_UNUSED(localFrame);
    double opacity = 1.0;
    const QTransform transform = clipBehaviorTransform(layer, clip, frame, image.size(), canvasSize, &opacity);
    if (!transform.isIdentity()) image = movedAboutCentre(image, transform);
    return opacity;
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
    // Shared with the model nodes and the viewer's node picking.
    return layerModelMatrixAt(transform, frame);
}

model3d::Camera RenderWorker::sceneCamera(const QSize& canvasSize, int frame) const
{
    if (m_reflectionCamera) return *m_reflectionCamera;
    if (m_viewCamera) return *m_viewCamera;
    return sceneCameraOf(m_composition.get(), canvasSize, frame);
}

void RenderWorker::paintFloorGrid(QImage& image) const
{
    // Lines every 100 units over +-5000 on the y = 0 plane, cut into short
    // pieces so the ones running behind a perspective camera can be left out.
    if (!m_viewCamera || image.isNull()) return;
    const QMatrix4x4 viewProjection = m_viewCamera->viewProjection(image.size());
    const double width = image.width(), height = image.height();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto place = [&](const QVector3D& point, QPointF* out) {
        const QVector4D clip = viewProjection * QVector4D(point, 1.0f);
        if (clip.w() <= 1e-3f) return false;
        *out = QPointF((clip.x() / clip.w() * 0.5 + 0.5) * width, (1.0 - (clip.y() / clip.w() * 0.5 + 0.5)) * height);
        return true;
    };
    constexpr int kExtent = 5000, kStep = 100, kPieces = 50;
    for (int line = -kExtent; line <= kExtent; line += kStep) {
        painter.setPen(QPen(line == 0 ? QColor(120, 120, 130, 200) : QColor(90, 90, 100, 140), line == 0 ? 1.5 : 1.0));
        for (int axis = 0; axis < 2; ++axis) {
            for (int piece = 0; piece < kPieces; ++piece) {
                const float a = float(-kExtent + 2.0 * kExtent * piece / kPieces);
                const float b = float(-kExtent + 2.0 * kExtent * (piece + 1) / kPieces);
                const QVector3D from = axis == 0 ? QVector3D(a, 0.0f, float(line)) : QVector3D(float(line), 0.0f, a);
                const QVector3D to = axis == 0 ? QVector3D(b, 0.0f, float(line)) : QVector3D(float(line), 0.0f, b);
                QPointF p, q;
                if (place(from, &p) && place(to, &q)) painter.drawLine(p, q);
            }
        }
    }
}

model3d::Lighting RenderWorker::sceneLighting(const composition::Layer& layer, int frame) const
{
    model3d::Lighting lighting;
    lighting.lights = sceneLightsOf(m_composition.get(), frame);
    lighting.surface = surfaceLightingOf(layer, frame);
    if (m_shadowMaps.size() == lighting.lights.size()) lighting.shadows = m_shadowMaps;
    return lighting;
}

QVector<std::shared_ptr<const model3d::ShadowMap>> RenderWorker::sceneShadowMaps(double timeSeconds,
                                                                                 const QSize& canvasSize) const
{
    if (!m_composition) return {};
    const int frame = frameForTime(timeSeconds);
    const QVector<model3d::Light> lights = sceneLightsOf(m_composition.get(), frame);
    if (std::none_of(lights.cbegin(), lights.cend(), [](const model3d::Light& light) { return light.castShadows; }))
        return {};
    QVector<model3d::ShadowCaster> casters;
    // A sheet as triangles in cells, so one passing beside a light loses
    // only the cells behind it, not the whole of itself.
    const auto sheetCaster = [](const SheetPicture& picture, const QMatrix4x4& model) {
        constexpr int kCells = 16;
        model3d::ShadowCaster caster;
        caster.picture = picture.content;
        const auto corner = [&](int column, int row) {
            const double u = double(column) / kCells, v = double(row) / kCells;
            caster.corners << model.map(QVector3D(float(u * picture.sheet.width() - picture.sheet.width() / 2.0),
                                                  float(picture.sheet.height() / 2.0 - v * picture.sheet.height()),
                                                  0.0f));
            caster.uvs << QPointF(u, v);
        };
        for (int row = 0; row < kCells; ++row)
            for (int column = 0; column < kCells; ++column) {
                corner(column, row); corner(column + 1, row); corner(column + 1, row + 1);
                corner(column, row); corner(column + 1, row + 1); corner(column, row + 1);
            }
        return caster;
    };
    // Billboarded Shadows: a sheet that turns to the camera turns to each
    // light for its shadow instead (the reference draws it with the light's
    // camera in the shadow pass) - made per light below.
    struct FacingLight
    {
        SheetPicture picture;
        const composition::Layer* layer = nullptr;
    };
    QVector<FacingLight> facingLights;
    for (const composition::Layer& layer : m_composition->layers()) {
        // Cast Shadows If Visibility Is Off: a hidden layer that still
        // casts (castShadowsIfLayerDisabled).
        if (!inScene(layer) || !layer.surface.castShadows) continue;
        if (!layer.visible && !layer.surface.castShadowsIfInvisible) continue;
        if (layer.kind == composition::LayerKind::Model3D) {
            if (!m_mediaManager || !layer.modelAssetId.isValid()
                || !composition::layerSpanCovers(layer, frame, m_composition->framesPerSecond()))
                continue;
            model3d::Mesh mesh = m_mediaManager->modelMesh(layer.modelAssetId);
            if (mesh.isEmpty()) continue;
            const double fps = double(m_composition->fpsNumerator()) / qMax(1, m_composition->fpsDenominator());
            mesh = posedModelMesh(layer, mesh, frame, fps, m_mediaManager->assetById(layer.modelAssetId).sourcePath());
            const QMatrix4x4 model = layerModelPlacementAt(m_composition.get(), layer, frame);
            model3d::ShadowCaster caster;
            caster.corners.reserve(mesh.triangles.size() * 3);
            for (const model3d::Triangle& triangle : mesh.triangles)
                for (int c = 0; c < 3; ++c) caster.corners << model.map(mesh.positions.at(triangle.position[c]));
            casters << caster;
            continue;
        }
        for (const composition::Clip& clip : layer.clips) {
            if (timeSeconds < clip.startSeconds || timeSeconds > clip.startSeconds + clip.durationSeconds) continue;
            composition::TextStyle style;
            QVector<const composition::Effect*> geometryEffects;
            if (layer.kind == composition::LayerKind::Text && textGeometryOf(clip, frame, &style, &geometryEffects)) {
                // Text with Geometry casts with its mesh, walls and all, as a
                // model does - not as the flat picture of it.
                const model3d::Mesh mesh = textGeometryMesh(layer, clip, style, geometryEffects, canvasSize, frame);
                const QMatrix4x4 placement = textGeometryPlacement(layer, frame);
                model3d::ShadowCaster caster;
                caster.corners.reserve(mesh.triangles.size() * 3);
                for (const model3d::Triangle& triangle : mesh.triangles)
                    for (int c = 0; c < 3; ++c) caster.corners << placement.map(mesh.positions.at(triangle.position[c]));
                if (!caster.corners.isEmpty()) casters << caster;
                continue;
            }
            const double speed = clip.speed > 0.0 ? clip.speed : 1.0;
            const double local = clip.sourceStartSeconds + (timeSeconds - clip.startSeconds) * speed;
            const SheetPicture picture = sheetPicture(layer, clip, qMax(0.0, local), canvasSize, frame);
            if (picture.content.isNull()) continue;
            if (layer.autoOrient == composition::AutoOrient::TowardsActiveCamera && layer.surface.billboardedShadows) {
                facingLights.append({picture, &layer});
                continue;
            }
            casters << sheetCaster(picture, picture.model);
        }
    }
    QVector<std::shared_ptr<const model3d::ShadowMap>> maps(lights.size());
    if (casters.isEmpty() && facingLights.isEmpty()) return maps;
    // Project Settings "Shadow Map Size", fewer texels for a reduced preview;
    // a point light's six at most 1024 each.
    const int asked = qMax(1, m_composition->projectSettings().shadowMapSize);
    const int size = qBound(64, int(std::lround(asked * qMin(1.0, m_renderScale))), 2048);
    for (int i = 0; i < lights.size(); ++i) {
        const model3d::Light& light = lights.at(i);
        const bool cube = light.type == model3d::Light::Type::Point
            || (light.type == model3d::Light::Type::Spot && light.spotOuterCosine <= 0.867);
        QVector<model3d::ShadowCaster> lit = casters;
        for (const FacingLight& sheet : facingLights) {
            // Its front to the light: against a spot's or a directional
            // light's direction, away from a point light (decision of the
            // port; the reference turns it as the light's shadow camera).
            const QVector3D at = sheet.picture.model.map(QVector3D());
            const QVector3D away = light.type == model3d::Light::Type::Point ? at - light.position : light.direction;
            lit << sheetCaster(sheet.picture, layerPlaneMatrixAt(sheet.layer->transform, frame, aimAlong(away)));
        }
        maps[i] = model3d::makeShadowMap(light, lit, cube ? qMin(size, 1024) : size);
    }
    return maps;
}

bool RenderWorker::inScene(const composition::Layer& layer)
{
    if (layer.kind == composition::LayerKind::Model3D) return true;
    return layer.dimension == composition::LayerDimension::ThreeD
           && (layer.kind == composition::LayerKind::Media || layer.kind == composition::LayerKind::Plane
               || layer.kind == composition::LayerKind::Text);
}

namespace {

// A model or Geometry text is a body, drawn as a mesh; any other scene layer
// a sheet.
bool sceneBody(const composition::Layer& layer)
{
    return layer.kind == composition::LayerKind::Model3D
           || (layer.kind == composition::LayerKind::Text && !layer.clips.isEmpty()
               && std::any_of(layer.clips.first().effects.cbegin(), layer.clips.first().effects.cend(),
                              [](const composition::Effect& fx) {
                                  return fx.enabled && plugin::nativeGeometryRenderingVerified(fx.pluginId);
                              }));
}

// The camera seen in a mirror through `origin` square to `normal`.
model3d::Camera mirroredCamera(model3d::Camera camera, const QVector3D& origin, const QVector3D& normal)
{
    const auto point = [&](const QVector3D& p) { return p - 2.0f * QVector3D::dotProduct(p - origin, normal) * normal; };
    camera.position = point(camera.position);
    camera.target = point(camera.target);
    camera.up = camera.up - 2.0f * QVector3D::dotProduct(camera.up, normal) * normal;
    return camera;
}

} // namespace

QVector<float> RenderWorker::sceneDistances(const composition::Layer& layer, const QImage& image,
                                            int frame) const
{
    const QSize size = image.size();
    QVector<float> distances(size.width() * size.height(), std::numeric_limits<float>::infinity());
    if (size.isEmpty()) return distances;
    const model3d::Camera camera = sceneCamera(sceneCanvas(size), frame);
    const QMatrix4x4 viewProjection = camera.viewProjection(size);
    bool invertible = false;
    const QMatrix4x4 unproject = viewProjection.inverted(&invertible);
    const QVector3D eye = camera.position;
    // A body is met where its drawn surface lies (its mesh's pixels, noted
    // as it was drawn), else by where the layer stands. A sheet is met pixel
    // by pixel on its plane.
    const bool sheet = !sceneBody(layer);
    const auto drawn = sheet ? m_meshSurfaces.constEnd() : m_meshSurfaces.constFind(layer.id.value());
    const model3d::SurfaceBuffer* surface =
        drawn != m_meshSurfaces.constEnd() && drawn->size == size ? &drawn.value() : nullptr;
    const QMatrix4x4 model = sheet ? layerSheetMatrixAt(m_composition.get(), layer, frame, m_mediaManager.get())
                                   : QMatrix4x4();
    const QVector3D origin = sheet ? model.map(QVector3D(0.0f, 0.0f, 0.0f))
                                   : layerPositionAt(m_composition.get(), layer, frame);
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
            if (surface && surface->has(y * size.width() + x))
                distance = (surface->positions.at(y * size.width() + x) - eye).length();
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

model3d::SurfaceBuffer RenderWorker::sceneSurfaces(const composition::Layer& layer, const QImage& image,
                                                   int frame, int leastAlpha) const
{
    model3d::SurfaceBuffer buffer;
    const QSize size = image.size();
    if (size.isEmpty()) return buffer;
    if (sceneBody(layer)) {
        const auto drawn = m_meshSurfaces.constFind(layer.id.value());
        if (drawn == m_meshSurfaces.constEnd() || drawn->size != size) return buffer;
        buffer = *drawn;
        for (int y = 0; y < size.height(); ++y) {
            const uchar* row = image.constScanLine(y);
            for (int x = 0; x < size.width(); ++x)
                if (row[x * 4 + 3] < leastAlpha)
                    buffer.depths[y * size.width() + x] = std::numeric_limits<float>::infinity();
        }
        return buffer;
    }
    buffer.reset(size);
    const model3d::Camera camera = sceneCamera(sceneCanvas(size), frame);
    bool invertible = false;
    const QMatrix4x4 unproject = camera.viewProjection(size).inverted(&invertible);
    if (!invertible) return buffer;
    const QMatrix4x4 model = layerSheetMatrixAt(m_composition.get(), layer, frame, m_mediaManager.get());
    const QVector3D origin = model.map(QVector3D(0.0f, 0.0f, 0.0f));
    const QVector3D normal = QVector3D::crossProduct(model.mapVector(QVector3D(1.0f, 0.0f, 0.0f)),
                                                     model.mapVector(QVector3D(0.0f, 1.0f, 0.0f))).normalized();
    if (normal.isNull()) return buffer;
    const double width = size.width(), height = size.height();
    for (int y = 0; y < size.height(); ++y) {
        const uchar* row = image.constScanLine(y);
        const float ndcY = float(1.0 - 2.0 * (y + 0.5) / height);
        for (int x = 0; x < size.width(); ++x) {
            if (row[x * 4 + 3] < leastAlpha || row[x * 4 + 3] == 0) continue;
            // The pixel's ray from the near plane to the far one (parallel
            // rays for an axis view).
            const float ndcX = float(2.0 * (x + 0.5) / width - 1.0);
            const QVector3D start = unproject.map(QVector3D(ndcX, ndcY, -1.0f));
            const QVector3D ray = unproject.map(QVector3D(ndcX, ndcY, 1.0f)) - start;
            const float along = QVector3D::dotProduct(ray, normal);
            if (qFuzzyIsNull(along)) continue;
            const float t = QVector3D::dotProduct(origin - start, normal) / along;
            if (t < 0.0f) continue;
            const int at = y * size.width() + x;
            const QVector3D point = start + ray * t;
            buffer.positions[at] = point;
            buffer.normals[at] = QVector3D::dotProduct(normal, ray) > 0.0f ? -normal : normal;
            buffer.alphas[at] = row[x * 4 + 3] / 255.0f;
            buffer.depths[at] = t;
        }
    }
    return buffer;
}

QImage RenderWorker::mirroredScene(const QVector<const composition::Layer*>& casters, const QVector<QImage>& images,
                                   const QVector3D& origin, const QVector3D& normal, int frame) const
{
    const QImage straight = reflectionComposite(casters, images, frame, true, origin, normal);
    if (straight.isNull()) return straight;
    const QSize size = straight.size();
    QImage turned(size, QImage::Format_RGBA8888);
    for (int y = 0; y < size.height(); ++y) {
        const quint32* from = reinterpret_cast<const quint32*>(straight.constScanLine(y));
        quint32* to = reinterpret_cast<quint32*>(turned.scanLine(y));
        for (int x = 0; x < size.width(); ++x) to[x] = from[size.width() - 1 - x];
    }
    return turned;
}

QImage RenderWorker::latLongFromCube(const QVector<QImage>& faces, const QVector<model3d::Camera>& cameras,
                                     const QVector3D& centre, int height)
{
    if (faces.isEmpty() || faces.size() != cameras.size() || height <= 0) return QImage();
    const QSize size = faces.first().size();
    QVector<QMatrix4x4> projections;
    QVector<QVector3D> forwards;
    for (const model3d::Camera& camera : cameras) {
        projections.append(camera.viewProjection(size));
        forwards.append((camera.target - camera.position).normalized());
    }
    const int width = 2 * height;
    QImage latLong(width, height, QImage::Format_RGBA8888);
    latLong.fill(Qt::transparent);
    constexpr double kPi = 3.14159265358979323846;
    for (int y = 0; y < height; ++y) {
        uchar* row = latLong.scanLine(y);
        const double phi = (y + 0.5) / height * kPi;
        for (int x = 0; x < width; ++x) {
            const double theta = ((x + 0.5) / width - 0.5) * 2.0 * kPi;
            const QVector3D direction(float(std::sin(phi) * std::sin(theta)), float(std::cos(phi)),
                                      float(std::sin(phi) * std::cos(theta)));
            int face = 0;
            for (int f = 1; f < forwards.size(); ++f)
                if (QVector3D::dotProduct(direction, forwards.at(f)) > QVector3D::dotProduct(direction, forwards.at(face)))
                    face = f;
            const QVector4D clip = projections.at(face) * QVector4D(centre + direction * 10.0f, 1.0f);
            if (clip.w() <= 0.0f) continue;
            const double px = (clip.x() / clip.w() * 0.5 + 0.5) * size.width() - 0.5;
            const double py = (1.0 - (clip.y() / clip.w() * 0.5 + 0.5)) * size.height() - 0.5;
            const QImage& image = faces.at(face);
            const int x0 = qBound(0, int(std::floor(px)), size.width() - 1), x1 = qMin(x0 + 1, size.width() - 1);
            const int y0 = qBound(0, int(std::floor(py)), size.height() - 1), y1 = qMin(y0 + 1, size.height() - 1);
            const double tx = qBound(0.0, px - std::floor(px), 1.0), ty = qBound(0.0, py - std::floor(py), 1.0);
            const uchar* top = image.constScanLine(y0);
            const uchar* bottom = image.constScanLine(y1);
            for (int c = 0; c < 4; ++c)
                row[x * 4 + c] = uchar(std::lround((top[x0 * 4 + c] * (1.0 - tx) + top[x1 * 4 + c] * tx) * (1.0 - ty)
                                                   + (bottom[x0 * 4 + c] * (1.0 - tx) + bottom[x1 * 4 + c] * tx) * ty));
        }
    }
    return latLong;
}

QImage RenderWorker::reflectionComposite(const QVector<const composition::Layer*>& casters, const QVector<QImage>& images,
                                         int frame, bool clipped, const QVector3D& origin, const QVector3D& normal) const
{
    if (casters.isEmpty() || images.size() != casters.size() || images.first().isNull()) return QImage();
    const QSize size = images.first().size();
    const QVector3D eye = sceneCamera(sceneCanvas(size), frame).position;
    QVector<model3d::SurfaceBuffer> shown;
    QVector<QImage> premultiplied;
    for (int i = 0; i < casters.size(); ++i) {
        shown.append(sceneSurfaces(*casters.at(i), images.at(i), frame, 1));
        premultiplied.append(images.at(i).convertToFormat(QImage::Format_RGBA8888_Premultiplied));
    }
    QImage merged(size, QImage::Format_RGBA8888_Premultiplied);
    merged.fill(Qt::transparent);
    QVarLengthArray<std::pair<float, int>, 16> order;
    for (int y = 0; y < size.height(); ++y) {
        uchar* target = merged.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            const int at = y * size.width() + x;
            order.clear();
            for (int i = 0; i < shown.size(); ++i) {
                const model3d::SurfaceBuffer& buffer = shown.at(i);
                if (buffer.size != size || !buffer.has(at)) continue;
                const QVector3D point = buffer.positions.at(at);
                if (clipped && QVector3D::dotProduct(normal, point - origin) < -1e-3f) continue;   // behind the mirror
                order.append({(point - eye).length(), i});
            }
            if (order.isEmpty()) continue;
            std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            uchar* pixel = target + x * 4;
            for (const auto& entry : order) {
                const uchar* source = premultiplied.at(entry.second).constScanLine(y) + x * 4;
                const int keep = 255 - source[3];
                for (int c = 0; c < 4; ++c) pixel[c] = uchar(qMin(255, source[c] + (pixel[c] * keep + 127) / 255));
            }
        }
    }
    return merged.convertToFormat(QImage::Format_RGBA8888);
}

void RenderWorker::addPlanarReflection(QImage& image, const QImage& mirror, double amount)
{
    if (mirror.size() != image.size() || amount <= 0.0) return;
    for (int y = 0; y < image.height(); ++y) {
        uchar* row = image.scanLine(y);
        const uchar* reflected = mirror.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            if (row[x * 4 + 3] == 0 || reflected[x * 4 + 3] == 0) continue;
            const double scale = reflected[x * 4 + 3] / 255.0 * amount * amount;
            for (int c = 0; c < 3; ++c)
                row[x * 4 + c] = uchar(qMin(255L, row[x * 4 + c] + std::lround(reflected[x * 4 + c] * scale)));
        }
    }
}

void RenderWorker::addEnvironment(QImage& image, const composition::Layer& layer, const QImage& environment,
                                  int frame) const
{
    const double specular = layer.surface.specularReflectivityAt(frame) / 100.0;
    const double diffuse = layer.surface.diffuseReflectivityAt(frame) / 100.0;
    if (image.isNull() || environment.isNull() || (specular <= 0.0 && diffuse <= 0.0)) return;
    const QImage latLong = environment.convertToFormat(QImage::Format_RGBA8888);
    const model3d::SurfaceBuffer shown = sceneSurfaces(layer, image, frame, 1);
    if (shown.size != image.size()) return;
    const QVector3D eye = sceneCamera(sceneCanvas(image.size()), frame).position;
    const model3d::Irradiance irradiance = diffuse > 0.0 ? model3d::irradianceOf(latLong) : model3d::Irradiance();
    for (int y = 0; y < image.height(); ++y) {
        uchar* row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const int at = y * image.width() + x;
            if (!shown.has(at)) continue;
            const QVector3D normal = shown.normals.at(at);
            const QVector3D view = (shown.positions.at(at) - eye).normalized();
            const QVector3D reflected = view - 2.0f * QVector3D::dotProduct(view, normal) * normal;
            const float alpha = row[x * 4 + 3] / 255.0f;
            const QVector3D colour(row[x * 4] / 255.0f, row[x * 4 + 1] / 255.0f, row[x * 4 + 2] / 255.0f);
            QVector3D out = colour;
            if (specular > 0.0) out += model3d::environmentColor(latLong, reflected) * alpha * float(specular);
            if (diffuse > 0.0) out += irradiance.at(normal) * colour * alpha * float(diffuse);
            row[x * 4] = uchar(qBound(0, int(std::lround(out.x() * 255.0f)), 255));
            row[x * 4 + 1] = uchar(qBound(0, int(std::lround(out.y() * 255.0f)), 255));
            row[x * 4 + 2] = uchar(qBound(0, int(std::lround(out.z() * 255.0f)), 255));
        }
    }
}

void RenderWorker::occludeScene(const QVector<const composition::Layer*>& layers, QVector<QImage>& images,
                                int frame) const
{
    if (layers.isEmpty() || images.size() != layers.size()) return;
    const QSize size = images.first().size();
    if (size.isEmpty()
        || std::none_of(layers.cbegin(), layers.cend(), [](const composition::Layer* layer) {
               return layer->surface.receiveAmbientOcclusion;
           }))
        return;
    const model3d::Camera camera = sceneCamera(sceneCanvas(size), frame);
    QMatrix4x4 view;
    view.lookAt(camera.position, camera.target, camera.up);
    QVector<model3d::SurfaceBuffer> shown(layers.size());
    for (int i = 0; i < layers.size(); ++i) {
        const composition::SurfaceShading& surface = layers.at(i)->surface;
        if (surface.receiveAmbientOcclusion || surface.castAmbientOcclusion)
            shown[i] = sceneSurfaces(*layers.at(i), images.at(i), frame);
    }
    const int count = size.width() * size.height();
    for (int r = 0; r < layers.size(); ++r) {
        const composition::Layer& receiver = *layers.at(r);
        if (!receiver.surface.receiveAmbientOcclusion || shown.at(r).size != size) continue;
        // Its G-buffer: itself and the layers casting occlusion, the nearest
        // at each pixel, in eye space - a model leaving out what is not
        // opaque unless it includes semi-transparent triangles.
        QVector<QVector3D> positions(count), normals(count);
        QVector<float> weights(count, 0.0f);
        QVector<float> nearest(count, std::numeric_limits<float>::infinity());
        for (int i = 0; i < layers.size(); ++i) {
            const composition::Layer& member = *layers.at(i);
            if (i != r && !member.surface.castAmbientOcclusion) continue;
            const model3d::SurfaceBuffer& buffer = shown.at(i);
            if (buffer.size != size) continue;
            const bool opaqueOnly = member.kind == composition::LayerKind::Model3D
                                    && !member.surface.includeSemiTransparentTriangles;
            for (int at = 0; at < count; ++at) {
                if (!buffer.has(at) || (opaqueOnly && buffer.alphas.at(at) < 1.0f)) continue;
                const QVector3D eye = view.map(buffer.positions.at(at));
                if (-eye.z() >= nearest.at(at)) continue;
                nearest[at] = -eye.z();
                positions[at] = eye;
                normals[at] = view.mapVector(buffer.normals.at(at));
                weights[at] = buffer.alphas.at(at);
            }
        }
        const composition::SurfaceShading& surface = receiver.surface;
        model3d::AmbientOcclusion settings;
        settings.samples = surface.aoSamples;
        settings.sampleRadius = surface.aoSampleRadius;
        settings.intensity = surface.aoIntensity / 100.0;
        settings.depthScale = surface.aoDepthScale / 100.0;
        settings.bias = surface.aoBias;
        // Blur Radius is in pixels of the picture: fewer in a reduced
        // preview, so it looks as the full frame does.
        settings.blurRadius = int(std::lround(surface.aoBlurRadius * qMin(1.0, m_renderScale)));
        settings.depthFalloff = surface.aoDepthFalloff / 100.0;
        const QVector<float> open = model3d::ambientOcclusionMap(size, positions, normals, weights, settings);
        const model3d::SurfaceBuffer& own = shown.at(r);
        const bool body = sceneBody(receiver);
        QImage& image = images[r];
        for (int y = 0; y < size.height(); ++y) {
            uchar* row = image.scanLine(y);
            for (int x = 0; x < size.width(); ++x) {
                const int at = y * size.width() + x;
                if (row[x * 4 + 3] == 0 || !own.has(at) || (body && own.alphas.at(at) < 1.0f)) continue;
                const float keep = open.at(at);
                for (int c = 0; c < 3; ++c) row[x * 4 + c] = uchar(std::lround(row[x * 4 + c] * keep));
            }
        }
    }
}

model3d::DepthOfField RenderWorker::sceneDepthOfField(int frame) const
{
    model3d::DepthOfField dof;
    if (!m_composition || m_viewCamera) return dof;
    for (const composition::Layer& layer : m_composition->layers()) {
        if (layer.kind != composition::LayerKind::Camera || !layer.visible
            || !composition::layerSpanCovers(layer, frame, m_composition->framesPerSecond()))
            continue;
        const composition::CameraSettings& camera = layer.camera;
        if (!camera.depthOfField || camera.blurAt(frame) <= 0.0) return dof;
        const double width = qMax(1, m_composition->displaySize().width());
        const double zoom = camera.zoomAt(frame) > 0.0
            ? camera.zoomAt(frame)
            : composition::CameraSettings::lensZoom(composition::CameraSettings::kNewCameraLensAngle, width);
        // FluxCamera's settings: CalculateFocalLength, CalculateFStop.
        dof.focalLength = zoom * camera.filmWidth / width;
        dof.lensDiameter = camera.apertureAt(frame) / dof.pixelsPerMM;
        const double focus = camera.focusDistanceAt(frame);
        dof.focusDistance = (focus > 0.0 ? focus : zoom) / dof.pixelsPerMM;
        dof.blurAmount = camera.blurAt(frame) / 100.0;
        dof.enabled = dof.focalLength > 0.0 && dof.lensDiameter > 0.0;
        return dof;
    }
    return dof;
}

QSize RenderWorker::sceneCanvas(const QSize& canvasSize) const
{
    return m_renderScale < 1.0 && m_composition ? m_composition->displaySize() : canvasSize;
}

int RenderWorker::antialiasingMode() const
{
    if (m_antialiasingMode > 0) return m_antialiasingMode;
    return m_composition ? m_composition->projectSettings().antialiasingMode : 1;
}

int RenderWorker::sceneSamples() const
{
    return model3d::antialiasingSamples(antialiasingMode());
}

model3d::Fog RenderWorker::sceneFog() const
{
    model3d::Fog fog;
    // A view with its own camera looks from far off: no fog there. Nor in
    // a reflection (Flux turns it off for the planar map).
    if (!m_composition || m_viewCamera || m_reflectionCamera) return fog;
    const composition::CompositionRenderSettings& s = m_composition->renderSettings();
    fog.enabled = s.fogEnabled;
    fog.nearDistance = s.fogNearDistance;
    fog.farDistance = s.fogFarDistance;
    fog.density = s.fogDensity;
    fog.falloff = model3d::Fog::Falloff(qBound(0, s.fogFalloff, 2));
    fog.color = s.fogColor;
    return fog;
}

RenderWorker::SheetPicture RenderWorker::sheetPicture(const composition::Layer& layer,
                                                      const composition::Clip& clip, double localTimeSeconds,
                                                      const QSize& canvasSize, int frame) const
{
    SheetPicture picture;
    if (canvasSize.isEmpty()) return picture;
    // The layer's own plane: its content as a 2D layer left at the frame's
    // centre would show it, on a sheet as large as the frame - or as the
    // footage or nested shot, when that is larger, so nothing is cut off
    // before the layer is scaled.
    // In a reduced preview the sheet keeps its size in the shot's units and
    // is drawn with fewer pixels; the camera is the full shot's.
    const QSize scene = sceneCanvas(canvasSize);
    QSize plane = scene;
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
    if (textEffectOf(clip)) plane = scene;
    composition::Layer flat = layer;
    flat.dimension = composition::LayerDimension::TwoD;
    flat.transform = composition::LayerTransform();
    flat.parentLayerId = core::Identifier();
    const QSize drawn = scene == canvasSize
        ? plane
        : QSize(qMax(1, int(std::lround(plane.width() * double(canvasSize.width()) / scene.width()))),
                qMax(1, int(std::lround(plane.height() * double(canvasSize.height()) / scene.height()))));
    picture.content = renderClip(flat, clip, localTimeSeconds, drawn, frame, plane);
    picture.sheet = plane;
    picture.model = layerSheetMatrixAt(m_composition.get(), layer, frame, m_mediaManager.get());
    return picture;
}

QImage RenderWorker::renderClipIn3D(const composition::Layer& layer, const composition::Clip& clip,
                                    double localTimeSeconds, const QSize& canvasSize, int frame) const
{
    QImage canvas(canvasSize, QImage::Format_RGBA8888);
    canvas.fill(Qt::transparent);
    if (canvasSize.isEmpty()) return canvas;
    const SheetPicture picture = sheetPicture(layer, clip, localTimeSeconds, canvasSize, frame);
    const QImage& content = picture.content;
    if (content.isNull()) return canvas;
    const QSize scene = sceneCanvas(canvasSize);
    const QSizeF sheet = picture.sheet;
    const double unitsX = sheet.width() / content.width();
    const double unitsY = sheet.height() / content.height();

    const QMatrix4x4& model = picture.model;

    const model3d::Camera camera = sceneCamera(scene, frame);
    const QMatrix4x4 viewProjection = camera.viewProjection(canvasSize);
    const QMatrix4x4 mvp = viewProjection * model;
    const double width = canvasSize.width();
    const double height = canvasSize.height();
    // A point of the content (its pixels, Y down) on screen; false behind
    // the eye.
    const auto project = [&](const QPointF& pixel, QPointF* screen) {
        const QVector4D clipPos = mvp * QVector4D(float(pixel.x() * unitsX - sheet.width() / 2.0),
                                                  float(sheet.height() / 2.0 - pixel.y() * unitsY),
                                                  0.0f, 1.0f);
        if (clipPos.w() <= 1e-3f) return false;
        const double ndcX = double(clipPos.x()) / clipPos.w();
        const double ndcY = double(clipPos.y()) / clipPos.w();
        *screen = QPointF((ndcX * 0.5 + 0.5) * width, (1.0 - (ndcY * 0.5 + 0.5)) * height);
        return true;
    };

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    // Whole when the sheet is in front of the camera; otherwise cell by cell,
    // leaving out what is behind it. The whole sheet's edges are smoothed as
    // the multisampled 3D target smooths them; cells are not, which would
    // leave seams between them.
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
    const QSizeF pixels = content.size();
    QPointF probe;
    const bool whole = project(QPointF(0, 0), &probe) && project(QPointF(pixels.width(), 0), &probe)
        && project(QPointF(pixels.width(), pixels.height()), &probe)
        && project(QPointF(0, pixels.height()), &probe);
    if (whole) {
        painter.setRenderHint(QPainter::Antialiasing, sceneSamples() > 1);
        drawPart(QRectF(QPointF(0, 0), pixels));
    } else {
        constexpr int kCells = 16;
        for (int row = 0; row < kCells; ++row)
            for (int column = 0; column < kCells; ++column)
                drawPart(QRectF(pixels.width() * column / kCells, pixels.height() * row / kCells,
                                pixels.width() / kCells, pixels.height() / kCells));
    }
    painter.end();

    // Lit, then fogged, pixel by pixel: at the point where the ray through
    // the pixel meets the layer's plane. Flux's layer shader lights the
    // sheet's own colour as its ambient, diffuse and specular colour alike,
    // facing the plane's normal; the fog goes by the distance from the eye.
    const model3d::Fog fog = sceneFog();
    const model3d::Lighting lighting = sceneLighting(layer, frame);
    if (fog.enabled || !lighting.lights.isEmpty()) {
        bool invertible = false;
        const QMatrix4x4 unproject = viewProjection.inverted(&invertible);
        const QVector3D origin = model.map(QVector3D(0.0f, 0.0f, 0.0f));
        const QVector3D normal = QVector3D::crossProduct(model.mapVector(QVector3D(1.0f, 0.0f, 0.0f)),
                                                         model.mapVector(QVector3D(0.0f, 1.0f, 0.0f)));
        const QVector3D eye = camera.position;
        const float toPlane = QVector3D::dotProduct(origin - eye, normal);
        const QVector3D emissive(float(lighting.surface.emissive.redF()), float(lighting.surface.emissive.greenF()),
                                 float(lighting.surface.emissive.blueF()));
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
                if (!lighting.lights.isEmpty()) {
                    const QVector4D own(pixel[0] / 255.0f, pixel[1] / 255.0f, pixel[2] / 255.0f, pixel[3] / 255.0f);
                    const QVector4D lit = model3d::illuminate(lighting, own, own, own, emissive,
                                                              eye + ray * hit, normal, eye);
                    for (int c = 0; c < 3; ++c)
                        pixel[c] = uchar(qBound(0, int(lit[c] * 255.0f + 0.5f), 255));
                }
                if (fog.enabled) fog.apply(pixel, double(hit * ray.length()));
            }
        }
    }
    return canvas;
}

QImage RenderWorker::layerTexture(const core::Identifier& layerId,
                                  const core::Identifier& forLayer, int frame) const
{
    if (!m_composition || !layerId.isValid() || layerId == forLayer) return QImage();
    const composition::Layer* source = nullptr;
    for (const composition::Layer& candidate : m_composition->layers())
        if (candidate.id == layerId) source = &candidate;
    // A model's own picture would be drawn through its materials again.
    if (!source || source->kind == composition::LayerKind::Model3D) return QImage();
    const double fps = m_composition->fpsDenominator() > 0
        ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
    const double seconds = frame / qMax(1.0, fps);
    for (const composition::Clip& clip : source->clips) {
        if (seconds < clip.startSeconds || seconds >= clip.endSeconds()) continue;
        // At its own size, as the 3D sheet of the layer is (renderClipIn3D).
        QSize plane = m_composition->displaySize();
        if (clip.nestedComposition) {
            plane = clip.nestedComposition->displaySize();
        } else if (m_mediaManager && clip.mediaId.isValid()) {
            const media::MediaAsset asset = m_mediaManager->assetById(clip.mediaId);
            if (asset.frameSize().isValid())
                plane = QSize(int(std::ceil(asset.frameSize().width() * asset.pixelAspectValue())),
                              asset.frameSize().height());
        }
        if (textEffectOf(clip)) plane = m_composition->displaySize();
        composition::Layer flat = *source;
        flat.dimension = composition::LayerDimension::TwoD;
        flat.transform = composition::LayerTransform();
        flat.parentLayerId = core::Identifier();
        const double speed = clip.speed > 0.0 ? clip.speed : 1.0;
        const double local = clip.sourceStartSeconds + (seconds - clip.startSeconds) * speed;
        const double scale = m_renderScale;
        m_renderScale = 1.0;
        QImage picture = renderClip(flat, clip, qMax(0.0, local), plane, frame, plane);
        if (!picture.isNull()) applyClipEffects(picture, clip, frame);
        m_renderScale = scale;
        return picture.isNull() ? picture : picture.convertToFormat(QImage::Format_RGBA8888);
    }
    return QImage();
}

QImage RenderWorker::assetTexture(const core::Identifier& assetId, int frame) const
{
    if (!m_composition || !m_mediaManager || !assetId.isValid()) return QImage();
    const media::MediaAsset asset = m_mediaManager->assetById(assetId);
    if (!asset.isValid() || !asset.frameSize().isValid()) return QImage();
    const double fps = m_composition->fpsDenominator() > 0
        ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
    const QSize plane(int(std::ceil(asset.frameSize().width() * asset.pixelAspectValue())), asset.frameSize().height());
    composition::Layer flat;
    flat.kind = composition::LayerKind::Media;
    composition::Clip clip;
    clip.mediaId = assetId;
    clip.durationSeconds = qMax(1.0, m_composition->durationSeconds());
    const double scale = m_renderScale;
    m_renderScale = 1.0;
    QImage picture = renderClip(flat, clip, qMax(0.0, frame / qMax(1.0, fps)), plane, frame, plane);
    m_renderScale = scale;
    return picture.isNull() ? picture : picture.convertToFormat(QImage::Format_RGBA8888);
}

QImage RenderWorker::environmentOf(const composition::SurfaceShading& surface, const QImage& composite,
                                   const core::Identifier& forLayer, int frame) const
{
    using Environment = composition::SurfaceShading::EnvironmentMap;
    switch (surface.environmentMap) {
    case Environment::CompositeShot: return composite;
    case Environment::Layer: return layerTexture(surface.environmentMapLayer, forLayer, frame);
    case Environment::Asset: return assetTexture(surface.environmentMapLayer, frame);
    case Environment::None: break;
    }
    return QImage();
}

model3d::Mesh RenderWorker::posedModel(const composition::Layer& layer, int frame) const
{
    if (!m_mediaManager || !layer.modelAssetId.isValid()) return model3d::Mesh();
    const model3d::Mesh mesh = m_mediaManager->modelMesh(layer.modelAssetId);
    if (mesh.isEmpty()) return mesh;
    // The animations playing on the layer, then its nodes' own transforms.
    const double fps = m_composition
        ? double(m_composition->fpsNumerator()) / qMax(1, m_composition->fpsDenominator()) : 30.0;
    return posedModelMesh(layer, mesh, frame, fps, m_mediaManager->assetById(layer.modelAssetId).sourcePath());
}

QImage RenderWorker::renderModelLayer(const composition::Layer& layer, const QSize& canvasSize,
                                      int frame) const
{
    model3d::Mesh mesh = posedModel(layer, frame);
    if (mesh.isEmpty()) {
        return QImage();
    }
    // Override Materials: a map left out, or taken from a layer.
    using Override = composition::ModelMaterialOverride;
    for (const Override& entry : layer.materialOverrides) {
        for (model3d::Material& material : mesh.materials) {
            if (material.name != entry.material) continue;
            QImage* images[Override::kMapCount] = {&material.diffuseImage, &material.specularImage,
                                                   &material.normalImage, &material.bumpImage,
                                                   &material.emissiveImage, &material.occlusionImage,
                                                   &material.metallicImage, &material.roughnessImage,
                                                   &material.specularGlossinessImage};
            for (int m = 0; m < Override::kMapCount; ++m) {
                const Override::MapOverride& map = entry.maps[size_t(m)];
                if (map.source == Override::MapSource::None) *images[m] = QImage();
                else if (map.source == Override::MapSource::UseLayer)
                    *images[m] = layerTexture(map.layer, layer.id, frame);
                if (m >= int(Override::Map::Metallic)
                    && map.source != Override::MapSource::FromFile)
                    material.metallicRoughnessPacked = false;
            }
            material.bumpScale *= entry.bumpMapScale;
        }
    }
    // Project Settings "Model Texture Max Size": larger maps are drawn from
    // a smaller copy, made once per picture.
    const int maxTexture = m_composition ? m_composition->projectSettings().modelTextureMaxSize : 0;
    if (maxTexture > 0) {
        for (model3d::Material& material : mesh.materials) {
            for (QImage* image : {&material.diffuseImage, &material.specularImage, &material.normalImage,
                                  &material.bumpImage, &material.emissiveImage, &material.occlusionImage,
                                  &material.metallicImage, &material.roughnessImage,
                                  &material.specularGlossinessImage}) {
                if (image->isNull() || qMax(image->width(), image->height()) <= maxTexture) continue;
                const QString key = QStringLiteral("%1:%2").arg(image->cacheKey()).arg(maxTexture);
                if (const QImage* limited = m_limitedTextures.object(key)) {
                    *image = *limited;
                    continue;
                }
                const QImage limited = model3d::limitTextureSize(*image, maxTexture);
                m_limitedTextures.insert(key, new QImage(limited), qMax(1, int(limited.sizeInBytes() / 1024)));
                *image = limited;
            }
        }
    }
    model3d::Lighting lighting = sceneLighting(layer, frame);
    lighting.reflections = m_modelReflections.value(layer.id.value());
    return model3d::renderMesh(mesh, layerModelPlacementAt(m_composition.get(), layer, frame),
                               sceneCamera(sceneCanvas(canvasSize), frame), canvasSize,
                               lighting, model3d::ShadingMode::Shaded, 1.0, sceneFog(),
                               sceneSamples(), m_collectSurfaces ? &m_meshSurfaces[layer.id.value()] : nullptr);
}

bool RenderWorker::layerHasTextGeometry(const composition::Layer& layer, int frame) const
{
    if (layer.kind != composition::LayerKind::Text || !m_composition) return false;
    const double fps = m_composition->fpsDenominator() > 0
        ? double(m_composition->fpsNumerator()) / m_composition->fpsDenominator() : 30.0;
    const double seconds = frame / qMax(1.0, fps);
    for (const composition::Clip& clip : layer.clips) {
        if (seconds < clip.startSeconds || seconds >= clip.endSeconds()) continue;
        composition::TextStyle style;
        QVector<const composition::Effect*> effects;
        return textGeometryOf(clip, frame, &style, &effects);
    }
    return false;
}

bool RenderWorker::textGeometryOf(const composition::Clip& clip, int frame, composition::TextStyle* style,
                                  QVector<const composition::Effect*>* effects)
{
    const composition::Effect* text = textEffectOf(clip);
    if (!text) return false;
    effects->clear();
    for (const composition::Effect& fx : clip.effects)
        if (fx.enabled && plugin::nativeGeometryRenderingVerified(fx.pluginId)) effects->append(&fx);
    if (effects->isEmpty()) return false;
    QStringList values = text->parameterValues;
    for (int p = 0; p < values.size(); ++p) values[p] = text->parameterAt(p, frame).toString();
    *style = composition::textStyleFromParameters(values);
    return !style->text.isEmpty();
}

QMatrix4x4 RenderWorker::textGeometryPlacement(const composition::Layer& layer, int frame) const
{
    // Its turn in the scene, made its own under its parent's.
    const QMatrix4x4 parent = layerParentMatrixAt(m_composition.get(), layer, frame);
    const QMatrix4x4 local = turnOf(parent).transposed() * layerTurnAt(m_composition.get(), layer, frame);
    return parent * layerModelMatrixAt(layer.transform, frame, local);
}

QImage RenderWorker::renderTextGeometry(const composition::Layer& layer,
                                        const composition::Clip& clip,
                                        const composition::TextStyle& style,
                                        const QVector<const composition::Effect*>& effects,
                                        const QSize& canvasSize, int frame) const
{
    const QSize scene = sceneCanvas(canvasSize);
    const model3d::Mesh mesh = textGeometryMesh(layer, clip, style, effects, canvasSize, frame);

    // A 3D layer is placed and seen like a model; a 2D one keeps the 2D text
    // placement under the camera that shows the z = 0 plane at frame size.
    QMatrix4x4 placement;
    model3d::Camera camera;
    if (layer.dimension == composition::LayerDimension::ThreeD) {
        placement = textGeometryPlacement(layer, frame);
        camera = sceneCamera(scene, frame);
    } else {
        const QPointF position = layer.transform.positionAt(frame);
        const QPointF scale = layer.transform.scaleAt(frame);
        placement.translate(float(position.x()), float(position.y()), 0.0f);
        placement.rotate(float(-layerAngleAt(layer, frame)), 0.0f, 0.0f, 1.0f);
        placement.scale(float(scale.x() / 100.0), float(scale.y() / 100.0), 1.0f);
        placement = layerParentMatrixAt(m_composition.get(), layer, frame) * placement;
        camera = model3d::defaultCameraForCanvas(scene);
    }
    // Multisampled as the rest of the 3D scene, which keeps glyph edges close
    // to the 2D text's. Fog and the lights reach the 3D layer only; a 2D one
    // is not in the scene.
    const bool inSpace = layer.dimension == composition::LayerDimension::ThreeD;
    const model3d::Fog fog = inSpace ? sceneFog() : model3d::Fog();
    model3d::Lighting lighting = inSpace ? sceneLighting(layer, frame) : model3d::Lighting();
    // Its Environment Map, as a model's: through the mesh's own normals.
    if (inSpace) lighting.reflections = m_modelReflections.value(layer.id.value());
    return model3d::renderMesh(mesh, placement, camera, canvasSize, lighting,
                               model3d::ShadingMode::Shaded, 1.0, fog, sceneSamples(),
                               m_collectSurfaces && inSpace ? &m_meshSurfaces[layer.id.value()] : nullptr);
}

model3d::Mesh RenderWorker::textGeometryMesh(const composition::Layer& layer, const composition::Clip& clip,
                                             const composition::TextStyle& style,
                                             const QVector<const composition::Effect*>& effects,
                                             const QSize& canvasSize, int frame) const
{
    // Flux builds this geometry for a TextBox with enabled Geometry effects
    // (FUN_1804d94e0) and runs the effects over it in order, each one
    // replacing it with what the module hands back.
    const QSize scene = sceneCanvas(canvasSize);
    const QRectF box(-scene.width() / 2.0, -scene.height() / 2.0, scene.width(), scene.height());
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
    // Lit as the layer's own surface: its Specular share of the colour and
    // its Shininess (a model's material would bring its own).
    model3d::Material material;
    material.diffuseColor = style.fontColor;
    material.ambientColor = style.fontColor;
    const double specularShare = qBound(0.0, layer.surface.specularAt(frame) / 100.0, 1.0);
    material.specularColor = QColor::fromRgbF(float(style.fontColor.redF() * specularShare),
                                              float(style.fontColor.greenF() * specularShare),
                                              float(style.fontColor.blueF() * specularShare));
    material.shininess = layer.surface.shininessAt(frame) / 100.0;
    material.opacity = style.fontColor.alphaF();
    // What the environment gives it: the text's Specular and Diffuse
    // Reflectivity.
    material.specularReflectivity = qMax(0.0, layer.surface.specularReflectivityAt(frame) / 100.0);
    material.diffuseReflectivity = qMax(0.0, layer.surface.diffuseReflectivityAt(frame) / 100.0);
    return meshFromGeometry(geometry, {material});
}

QImage RenderWorker::renderShotImage(double timeSeconds, double scale, bool withEffects,
                                     bool transparentBackground) const
{
    const QSize full = m_composition ? m_composition->displaySize() : QSize();
    if (full.isEmpty()) return QImage();
    if (scale >= 0.999) {
        return renderFrameImage(timeSeconds, full, withEffects, transparentBackground);
    }
    const QSize reduced(qMax(1, int(std::lround(full.width() * scale))),
                        qMax(1, int(std::lround(full.height() * scale))));
    const double previous = m_renderScale;
    m_renderScale = scale;
    QImage image = renderFrameImage(timeSeconds, reduced, withEffects, transparentBackground);
    m_renderScale = previous;
    return image;
}

QImage RenderWorker::renderFrameImage(double timeSeconds, const QSize& size, bool withEffects,
                                      bool transparentBackground) const
{
    QImage out(size, QImage::Format_RGBA8888);
    out.fill(transparentBackground ? Qt::transparent : QColor(18, 18, 22));

    if (!m_composition) {
        return out;
    }

    if (m_viewCamera && m_floorPlane) paintFloorGrid(out);
    m_shadowMaps = sceneShadowMaps(timeSeconds, size);

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
            // Drawn only within its span.
            if (!composition::layerSpanCovers(layer, frameNo, m_composition->framesPerSecond())) return;
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
            plugin::NativeFrameTime transitionTime;
            transitionTime.frameRate = fps;
            transitionTime.transitionSeconds = window.end - window.start;
            transitionTime.transitionCut = window.end > window.start
                ? float((window.cut - window.start) / (window.end - window.start)) : 0.0f;
            QImage mixed;
            if (!withEffects
                || !plugin::nativeVideoTransitionRenderingVerified(effect.pluginId)
                || !plugin::applyNativeVideoTransition(mixed, from, to, progress,
                                                       effect.pluginId, values,
                                                       transitionTime)) {
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
    // Whether a scene layer receives ambient occlusion: then the meshes note
    // what they show as they are drawn.
    const bool occlusion = std::any_of(layers.cbegin(), layers.cend(), [](const composition::Layer& layer) {
        return layer.visible && inScene(layer) && layer.surface.receiveAmbientOcclusion;
    });
    // With more than one layer in the space, a body (a model, a text made
    // solid) notes where its pixels lie, so what is nearer covers what is
    // further pixel by pixel - a word on a cube's far face stays behind it.
    const bool sharedSpace = std::count_if(layers.cbegin(), layers.cend(), [](const composition::Layer& layer) {
        return layer.visible && inScene(layer);
    }) > 1;
    m_meshSurfaces.clear();
    m_modelReflections.clear();
    // Whether a scene sheet receives reflections or a 3D text has an
    // environment map.
    const auto reflects = [](const composition::Layer& layer) {
        return layer.visible && inScene(layer) && !sceneBody(layer)
               && (layer.surface.receiveReflections
                   || (layer.kind == composition::LayerKind::Text
                       && layer.surface.environmentMap != composition::SurfaceShading::EnvironmentMap::None));
    };
    const bool reflecting = std::any_of(layers.cbegin(), layers.cend(), reflects);
    // A receiving sheet's planar reflection map: the scene's layers that
    // cast reflections - those up to the 2D layers or Grades on either
    // side, hidden ones too with Cast Reflections If Visibility Is Off -
    // drawn through the camera mirrored in its plane.
    const auto closesScene = [&](const composition::Layer& layer) {
        return layer.visible && !inScene(layer) && !m_viewCamera && layer.kind != composition::LayerKind::Point
               && layer.kind != composition::LayerKind::Light && layer.kind != composition::LayerKind::Camera;
    };
    const auto castersNear = [&](const composition::Layer& receiver) {
        const int index = int(&receiver - layers.constData());
        int first = index, last = index;
        while (first > 0 && !closesScene(layers.at(first - 1))) --first;
        while (last + 1 < layers.size() && !closesScene(layers.at(last + 1))) ++last;
        QVector<const composition::Layer*> casters;
        for (int i = qMax(0, first); i <= last; ++i) {
            const composition::Layer& layer = layers.at(i);
            if (&layer != &receiver && inScene(layer) && layer.surface.castReflections
                && (layer.visible || layer.surface.castReflectionsIfInvisible))
                casters.append(&layer);
        }
        return casters;
    };
    // The casters drawn through `view` (fogless, their meshes noting what
    // they show), then made into one picture by `compose`.
    const auto throughCamera = [&](const model3d::Camera& view, const QVector<const composition::Layer*>& casters,
                                   const std::function<QImage(const QVector<QImage>&)>& compose) {
        QHash<QString, model3d::SurfaceBuffer> drawn;
        drawn.swap(m_meshSurfaces);
        const bool collecting = m_collectSurfaces;
        const std::optional<model3d::Camera> kept = m_reflectionCamera;
        m_reflectionCamera = view;
        m_collectSurfaces = true;
        QVector<QImage> images;
        for (const composition::Layer* caster : casters) {
            QImage image(size, QImage::Format_RGBA8888);
            image.fill(Qt::transparent);
            paintLayer(*caster, image, QPainter::CompositionMode_SourceOver);
            images.append(image);
        }
        m_collectSurfaces = false;
        const QImage result = compose(images);
        m_reflectionCamera = kept;
        m_meshSurfaces.swap(drawn);
        m_collectSurfaces = collecting;
        return result;
    };
    // A planar reflection map for a plane through `origin` facing `normal`.
    const auto mirrorIn = [&](const composition::Layer& receiver, const QVector3D& origin, QVector3D normal,
                              int frameNo) -> QImage {
        const QVector<const composition::Layer*> casters = castersNear(receiver);
        if (casters.isEmpty() || normal.isNull()) return QImage();
        const model3d::Camera camera = sceneCamera(sceneCanvas(size), frameNo);
        if (QVector3D::dotProduct(normal, camera.position - origin) < 0.0f) normal = -normal;
        return throughCamera(mirroredCamera(camera, origin, normal), casters, [&](const QVector<QImage>& images) {
            return mirroredScene(casters, images, origin, normal, frameNo);
        });
    };
    const auto reflectionOf = [&](const composition::Layer& receiver, int frameNo) -> QImage {
        const QMatrix4x4 sheet = layerSheetMatrixAt(m_composition.get(), receiver, frameNo, m_mediaManager.get());
        return mirrorIn(receiver, sheet.map(QVector3D(0.0f, 0.0f, 0.0f)),
                        QVector3D::crossProduct(sheet.mapVector(QVector3D(1.0f, 0.0f, 0.0f)),
                                                sheet.mapVector(QVector3D(0.0f, 1.0f, 0.0f))).normalized(),
                        frameNo);
    };
    // What a model reflects (model3d::Reflections): receiving reflections,
    // the scene round its middle (the mean of its corners) as Flux's
    // DynamicReflectionsCubeMap draws it (Flux.dll 0x18043d630: six faces,
    // fogless, Reflection Map Size texels), and for each Planar material the
    // scene mirrored in its plane (its triangles' area-weighted middle and
    // normal); without a cube - nothing casting reflections, or not
    // receiving them - its Environment Map. (Which of the cube and the
    // environment Flux takes when it has both is not recovered: here the
    // cube.)
    const auto modelReflectionsOf = [&](const composition::Layer& model, int frameNo)
        -> std::shared_ptr<const model3d::Reflections> {
        auto reflections = std::make_shared<model3d::Reflections>();
        const model3d::Mesh mesh = posedModel(model, frameNo);
        if (mesh.isEmpty()) return nullptr;
        const QMatrix4x4 placement = layerModelPlacementAt(m_composition.get(), model, frameNo);
        if (model.surface.receiveReflections) {
            const QVector<const composition::Layer*> casters = castersNear(model);
            if (!casters.isEmpty()) {
                QVector3D centre;
                for (const QVector3D& corner : mesh.positions) centre += placement.map(corner);
                centre /= float(qMax(1, int(mesh.positions.size())));
                const QVector3D directions[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                QVector<model3d::Camera> cameras;
                QVector<QImage> faces;
                const double aspect = size.height() > 0 ? double(size.width()) / size.height() : 1.0;
                for (const QVector3D& direction : directions) {
                    model3d::Camera face;
                    face.position = centre;
                    face.target = centre + direction;
                    face.up = std::abs(direction.y()) > 0.5f ? QVector3D(0.0f, 0.0f, 1.0f) : QVector3D(0.0f, 1.0f, 0.0f);
                    // At least 90 degrees across and up.
                    face.fieldOfViewDegrees = aspect >= 1.0 ? 90.0 : qRadiansToDegrees(2.0 * std::atan(1.0 / aspect));
                    face.nearPlane = 1.0;
                    cameras.append(face);
                    faces.append(throughCamera(face, casters, [&](const QVector<QImage>& images) {
                        return reflectionComposite(casters, images, frameNo, false, QVector3D(), QVector3D());
                    }));
                }
                const int asked = m_composition ? m_composition->projectSettings().reflectionMapSize : 512;
                const int height = qBound(16, int(std::lround(asked * qMin(1.0, m_renderScale))), 2048);
                *reflections = model3d::Reflections::fromLatLong(latLongFromCube(faces, cameras, centre, height));
            }
            for (int m = 0; m < mesh.materials.size(); ++m) {
                if (mesh.materials.at(m).reflectionMode != model3d::ReflectionMode::Planar) continue;
                QVector3D middle, normal;
                double weight = 0.0;
                for (const model3d::Triangle& triangle : mesh.triangles) {
                    if (triangle.material != m) continue;
                    const QVector3D a = placement.map(mesh.positions.at(triangle.position[0]));
                    const QVector3D b = placement.map(mesh.positions.at(triangle.position[1]));
                    const QVector3D c = placement.map(mesh.positions.at(triangle.position[2]));
                    const QVector3D cross = QVector3D::crossProduct(b - a, c - a);
                    const double area = cross.length();
                    normal += cross;
                    middle += (a + b + c) / 3.0f * float(area);
                    weight += area;
                }
                if (weight <= 0.0) continue;
                const QImage map = mirrorIn(model, middle / float(weight), normal.normalized(), frameNo);
                if (!map.isNull()) reflections->planar.insert(m, map);
            }
        }
        using Environment = composition::SurfaceShading::EnvironmentMap;
        if (reflections->levels.isEmpty() && model.surface.environmentMap != Environment::None) {
            const QImage environment = environmentOf(model.surface, out, model.id, frameNo);
            if (!environment.isNull()) {
                const QHash<int, QImage> planar = reflections->planar;
                *reflections = model3d::Reflections::fromLatLong(environment);
                reflections->planar = planar;
            }
        }
        if (reflections->levels.isEmpty() && reflections->planar.isEmpty()) return nullptr;
        return reflections;
    };
    const auto closeScene = [&] {
        if (scene.isEmpty()) return;
        if (reflecting) {
            // Reflections: a sheet's planar map, then a text's environment
            // ("Use Composite Shot": the shot beneath the scene).
            const int frameNo = frameForTime(timeSeconds);
            for (SceneLayer& member : scene) {
                const composition::Layer& layer = *member.layer;
                if (!reflects(layer)) continue;
                const composition::SurfaceShading& surface = layer.surface;
                if (surface.receiveReflections) {
                    const QImage mirror = reflectionOf(layer, frameNo);
                    if (!mirror.isNull())
                        addPlanarReflection(member.image, mirror, surface.specularReflectivityAt(frameNo) / 100.0);
                }
                using Environment = composition::SurfaceShading::EnvironmentMap;
                if (layer.kind == composition::LayerKind::Text && surface.environmentMap != Environment::None
                    && !layerHasTextGeometry(layer, frameNo)) {
                    const QImage environment = environmentOf(surface, out, layer.id, frameNo);
                    if (!environment.isNull()) addEnvironment(member.image, layer, environment, frameNo);
                }
            }
        }
        if (occlusion) {
            QVector<const composition::Layer*> members;
            QVector<QImage> images;
            for (const SceneLayer& member : scene) {
                members.append(member.layer);
                images.append(member.image);
            }
            occludeScene(members, images, frameForTime(timeSeconds));
            for (int i = 0; i < scene.size(); ++i) scene[i].image = images.at(i);
        }
        const model3d::DepthOfField dof = sceneDepthOfField(frameForTime(timeSeconds));
        if (dof.enabled) {
            // The scene on its own, near over far, its depth kept, blurred
            // by the camera's depth of field, then over what is beneath.
            const int frameNo = frameForTime(timeSeconds);
            QVector<QVector<float>> distances;
            QVector<QImage> premultiplied;
            for (const SceneLayer& member : scene) {
                distances.append(sceneDistances(*member.layer, member.image, frameNo));
                premultiplied.append(member.image.convertToFormat(QImage::Format_RGBA8888_Premultiplied));
            }
            QImage merged(size, QImage::Format_RGBA8888_Premultiplied);
            merged.fill(Qt::transparent);
            QVector<float> nearest(size.width() * size.height(), std::numeric_limits<float>::infinity());
            QVarLengthArray<int, 16> order;
            for (int y = 0; y < size.height(); ++y) {
                uchar* target = merged.scanLine(y);
                for (int x = 0; x < size.width(); ++x) {
                    const int at = y * size.width() + x;
                    order.clear();
                    for (int i = 0; i < premultiplied.size(); ++i)
                        if (premultiplied.at(i).constScanLine(y)[x * 4 + 3] != 0) order.append(i);
                    if (order.isEmpty()) continue;
                    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                        return distances.at(a).at(at) > distances.at(b).at(at);
                    });
                    uchar* pixel = target + x * 4;
                    for (int i : order) {
                        const uchar* source = premultiplied.at(i).constScanLine(y) + x * 4;
                        const int keep = 255 - source[3];
                        for (int c = 0; c < 4; ++c)
                            pixel[c] = uchar(qMin(255, source[c] + (pixel[c] * keep + 127) / 255));
                        nearest[at] = qMin(nearest[at], distances.at(i).at(at));
                    }
                }
            }
            model3d::applyDepthOfField(merged, nearest, sceneCamera(sceneCanvas(size), frameNo), dof,
                                       m_composition ? double(size.height()) / qMax(1, sceneCanvas(size).height()) : 1.0);
            QPainter painter(&out);
            painter.setCompositionMode(scene.size() == 1 ? compositionModeFor(scene.first().layer->blendMode)
                                                         : QPainter::CompositionMode_SourceOver);
            painter.drawImage(QPoint(0, 0), merged.convertToFormat(QImage::Format_RGBA8888));
            painter.end();
            scene.clear();
            return;
        }
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

        // A view with its own camera shows the 3D scene alone: 2D layers and
        // adjustments belong to the shot's picture, not to the space.
        if (m_viewCamera && !inScene(layer)) continue;

        if (inScene(layer)) {
            using Environment = composition::SurfaceShading::EnvironmentMap;
            if (layer.kind == composition::LayerKind::Model3D
                && (layer.surface.receiveReflections || layer.surface.environmentMap != Environment::None)) {
                const auto reflections = modelReflectionsOf(layer, frameForTime(timeSeconds));
                if (reflections) m_modelReflections.insert(layer.id.value(), reflections);
            }
            // Text drawn with Geometry takes its Environment Map on its mesh.
            if (layer.surface.environmentMap != Environment::None
                && layerHasTextGeometry(layer, frameForTime(timeSeconds))) {
                const int frameNo = frameForTime(timeSeconds);
                const QImage environment = environmentOf(layer.surface, out, layer.id, frameNo);
                if (!environment.isNull())
                    m_modelReflections.insert(layer.id.value(), std::make_shared<model3d::Reflections>(
                                                                    model3d::Reflections::fromLatLong(environment)));
            }
            QImage image(size, QImage::Format_RGBA8888);
            image.fill(Qt::transparent);
            m_collectSurfaces = occlusion || (sharedSpace && sceneBody(layer));
            paintLayer(layer, image, QPainter::CompositionMode_SourceOver);
            m_collectSurfaces = false;
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
    m_shadowMaps.clear();
    m_meshSurfaces.clear();
    m_modelReflections.clear();

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
    if (layer.dimension == composition::LayerDimension::TwoD
        && layer.autoOrient == composition::AutoOrient::AlongMotionPath) {
        // Along Motion Path in the plane: drawn at the angle its motion gives.
        composition::Layer aligned = layer;
        aligned.transform.rotationDegrees = layerAngleAt(layer, frameForTime(timeSeconds));
        aligned.transform.rotationCurve.clear();
        aligned.autoOrient = composition::AutoOrient::None;
        return renderLayerClipImage(aligned, clip, timeSeconds, size, withEffects);
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
    // The travel in the pixels drawn: a reduced preview needs fewer samples
    // for the same smear.
    // What moves the layer over the frame besides its keys - its Behaviors,
    // a simulated body, its parent - shows in its box: the farthest a
    // corner of it goes counts as well.
    const bool blurring = withEffects && layer.motionBlur && shutter.motionBlurEnabled && shutter.shutterAngle > 0.0;
    double boxTravel = 0.0;
    if (blurring && m_composition) {
        const LayerGeometry now = layerGeometry(*m_composition, layer, m_mediaManager.get(), frameNo);
        const LayerGeometry next = layerGeometry(*m_composition, layer, m_mediaManager.get(), frameNo + 1);
        if (now.valid && next.valid && now.quad.size() == next.quad.size())
            for (int corner = 0; corner < now.quad.size(); ++corner)
                boxTravel = qMax(boxTravel, QLineF(now.quad.at(corner), next.quad.at(corner)).length());
    }
    const double travel = qMax(framePixelTravel(layer, frameNo, sceneCanvas(size)), boxTravel);
    const QVector<double> offsets = blurring && (hasMotion(layer) || boxTravel > 0.01)
        ? shutterOffsets(shutter, travel * qMin(1.0, m_renderScale))
        : QVector<double>();
    if (offsets.size() < 2) {
        clipImage = renderClip(offsets.isEmpty() ? layer : layerAtSubFrame(layer, frameNo + offsets[0]),
                               clip, qMax(0.0, localTime), size, frameNo);
    } else {
        // Averaged in premultiplied space, so the smeared edges fade out
        // instead of picking up the colour of transparent pixels.
        std::vector<float> sum;
        QSize sampleSize;
        // Each sample's Behaviors at its own time, as the reference runs them
        // at the sub-frame's (simulated bodies taken back between frames):
        // a sheet takes them in its matrix while the sample is drawn, a 2D
        // picture is moved by how its Behaviors there differ from the
        // frame's (applyClipBehaviors puts the frame's on after), and faded
        // by how their opacity does.
        const bool sheet = layerDrawnAsSheet(layer, &clip);
        double opacityAtFrame = 1.0;
        const QTransform atFrameMove = withEffects
            ? clipBehaviorTransform(layer, clip, frameNo, size, size, &opacityAtFrame) : QTransform();
        bool invertible = true;
        const QTransform undoFrame = atFrameMove.inverted(&invertible);
        for (double offset : offsets) {
            if (withEffects && sheet) behaviorFrameShift() = offset;
            QImage sample = renderClip(layerAtSubFrame(layer, frameNo + offset), clip,
                                       qMax(0.0, localTime), size, frameNo)
                                .convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            behaviorFrameShift() = 0.0;
            if (withEffects) {
                double opacityThen = 1.0;
                const QTransform then = clipBehaviorTransform(layer, clip, frameNo + offset, sample.size(), size,
                                                              &opacityThen);
                if (!sheet && invertible) {
                    const QTransform difference = then * undoFrame;
                    if (!difference.isIdentity())
                        sample = movedAboutCentre(sample.convertToFormat(QImage::Format_RGBA8888), difference)
                                     .convertToFormat(QImage::Format_RGBA8888_Premultiplied);
                }
                if (opacityAtFrame > 1e-6 && std::abs(opacityThen - opacityAtFrame) > 1e-6) {
                    const float ratio = float(opacityThen / opacityAtFrame);
                    for (int y = 0; y < sample.height(); ++y) {
                        uchar* row = sample.scanLine(y);
                        for (int x = 0; x < sample.width() * 4; ++x)
                            row[x] = uchar(qBound(0, int(std::lround(row[x] * ratio)), 255));
                    }
                }
            }
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
    m_viewCamera = request.viewCamera;
    m_floorPlane = request.floorPlane;
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
    // A preview smaller than the shot (Half, Quarter, the Layer panel) is
    // drawn at that size rather than drawn full and thrown away - native 2D
    // effects too, on the reduced canvas, as Flux's effect pass draws them.
    const QSize native = m_composition ? m_composition->displaySize() : QSize();
    const double scale = native.isEmpty() || request.size.isEmpty()
        ? 1.0 : qMin(1.0, double(request.size.height()) / native.height());
    QImage frame = native.isEmpty()
        ? renderFrameImage(request.timeSeconds, request.size, request.renderEffects,
                           request.transparentBackground)
        : renderShotImage(request.timeSeconds, scale < 0.75 ? scale : 1.0, request.renderEffects,
                          request.transparentBackground);
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

    // A request can follow a complete frame immediately (including while an
    // export renderer retries video decoding). Do not expose the previous
    // frame's completion state while this request is in flight.
    m_lastFrameComplete = false;

    FrameRequest request;
    request.frameIndex = frameIndex;
    request.timeSeconds = timeSeconds;
    request.size = size;
    request.renderEffects = renderEffects;
    request.transparentBackground = transparentBackground;
    request.preRenderDirectory = m_preRenderDirectory;
    request.viewCamera = m_viewCamera;
    request.floorPlane = m_floorPlane;
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
            // Lit by the light layers, turned as the reference turns a layer,
            // with shadows and the materials' illumination models since
            // October 2026.
            stream << QStringLiteral("lights-1");
            // Model textures are limited by the project since October 2026.
            if (std::any_of(shot.layers().cbegin(), shot.layers().cend(), [](const composition::Layer& l) {
                    return l.kind == composition::LayerKind::Model3D;
                }))
                stream << QStringLiteral("texture-max") << composition.projectSettings().modelTextureMaxSize;
            // Multisampled since October 2026, by the project's mode.
            stream << QStringLiteral("msaa-1") << composition.projectSettings().antialiasingMode;
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
        // Behaviors and Geometry modules are drawn reduced in Half and
        // Quarter since October 2026; video transitions are told their side
        // of the edit point since 19 October 2026 (Fade to Color drew none).
        if (native)
            stream << QStringLiteral("native-time-1") << QStringLiteral("reduced-moves-1")
                   << QStringLiteral("transition-side-1");
        for (const composition::Layer& layer : shot.layers()) {
            // A model as it was read and its materials as they were edited.
            if (layer.kind == composition::LayerKind::Model3D && media && media->hasModel(layer.modelAssetId)) {
                const model3d::ImportSettings settings = media->modelSettings(layer.modelAssetId);
                stream << settings.flipYZAxis << settings.centerAnchorPoint << int(settings.unit)
                       << settings.singleUnitScale << settings.autoNormalize << int(settings.normalMethod)
                       << settings.autoSmoothingAngle << settings.unifyNormals << settings.flipNormals
                       << settings.flipUVCoordinates;
                for (const model3d::Material& m : media->modelMaterialEdits(layer.modelAssetId))
                    stream << m.name << m.diffuseColor << m.specularColor << m.ambientColor << m.emissiveColor
                           << m.shininess << m.opacity << int(m.illuminationModel) << m.metallic << m.roughness
                           << m.cookTorranceRoughness << m.cookTorranceSpecularReflectance << m.cookTorranceFresnel
                           << m.glossiness << int(m.alphaMode) << m.alphaCutOff << m.occlusionStrength
                           << m.bumpScale << m.diffuseReflectivity << m.specularReflectivity
                           << m.indexOfRefraction << m.refractionAmount << m.clearCoat << m.clearCoatRoughness
                           << int(m.reflectionMode);
            }
            // An animation read from a file of its own changes with it.
            for (const composition::ModelAnimationInstance& animation : layer.modelAnimations) {
                if (animation.filePath.isEmpty()) continue;
                const QFileInfo file(animation.filePath);
                stream << file.absoluteFilePath() << file.size() << file.lastModified().toMSecsSinceEpoch();
            }
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

QImage RenderWorker::renderComposition(const std::shared_ptr<composition::Composition>& shot,
                                       const std::shared_ptr<media::MediaManager>& media, double seconds,
                                       const QString& preRenderDirectory)
{
    const QSize size(qMax(1, shot->width()), qMax(1, shot->height()));
    RenderWorker worker;
    worker.setComposition(shot);
    worker.setMediaManager(media);
    worker.m_preRenderDirectory = preRenderDirectory;
    QImage picture = worker.loadPreRenderedFrame(*shot, seconds, size);
    if (picture.isNull()) picture = worker.renderShotImage(seconds, 1.0, true, true);
    if (!picture.isNull() && picture.size() != shot->displaySize() && !shot->displaySize().isEmpty())
        picture = picture.convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                      .scaled(shot->displaySize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                      .convertToFormat(QImage::Format_RGBA8888);
    return picture;
}

QString preRenderAudioPath(const QString& projectPreRenderDirectory,
                           const composition::Composition& composition, const media::MediaManager* media)
{
    const QString folder = preRenderFolder(projectPreRenderDirectory, composition);
    if (folder.isEmpty()) return {};
    const QString state = renderStateKey(composition, media, 0.0,
                                         QSize(qMax(1, composition.width()), qMax(1, composition.height())), true,
                                         true);
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(state.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    return QDir(folder).filePath(QStringLiteral("audio-%1.wav").arg(key));
}

QString preRenderedAudio(const QString& projectPreRenderDirectory,
                         const composition::Composition& composition, const media::MediaManager* media)
{
    if (projectPreRenderDirectory.isEmpty()) return {};
    const QString path = preRenderAudioPath(projectPreRenderDirectory, composition, media);
    return !path.isEmpty() && QFileInfo(path).isFile() && QFileInfo(path).size() > 44 ? path : QString();
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
    QString key = renderStateKey(*m_composition, m_mediaManager.get(), timeSeconds, size,
                                 renderEffects, transparentBackground, m_mediaVariant);
    if (m_viewCamera) {
        const model3d::Camera& c = *m_viewCamera;
        key += QStringLiteral("|view:%1,%2,%3|%4,%5,%6|%7,%8,%9|%10|%11|%12")
                   .arg(c.position.x()).arg(c.position.y()).arg(c.position.z())
                   .arg(c.target.x()).arg(c.target.y()).arg(c.target.z())
                   .arg(c.up.x()).arg(c.up.y()).arg(c.up.z())
                   .arg(c.orthographic ? c.orthographicHeight : -c.fieldOfViewDegrees)
                   .arg(m_floorPlane ? 1 : 0).arg(c.fieldOfViewDegrees);
    }
    return key;
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
    // The worker leaves the key out of a frame that is missing a picture.
    m_lastFrameComplete = !cacheKey.isEmpty() || !m_composition;
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
