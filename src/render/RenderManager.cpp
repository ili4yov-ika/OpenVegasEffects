#include "render/RenderManager.h"
#include "composition/CompositionState.h"
#include <QCryptographicHash>
#include <QTimer>

#include <QMetaType>
#include <QFont>
#include <QPainter>
#include <QSettings>
#include <QPainterPath>
#include <QTransform>

#include "core/Identifier.h"
#include "core/Log.h"
#include "composition/TextStyle.h"
#include "plugin/EffectRender.h"
#include "plugin/NativeEffectRender.h"
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
    if (asset.kind() != media::MediaKind::Video) {
        return QImage();
    }
    const int sourceFrame = qMax(0, frameForTime(localSeconds));
    const QImage frame = m_mediaManager->videoFrame(mediaId, sourceFrame);
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
        // Render the child at its own native resolution, then let the parent
        // layer transform place it exactly like footage or a still image.
        RenderWorker nested;
        nested.setComposition(clip.nestedComposition);
        nested.setMediaManager(m_mediaManager);
        const QSize childSize(qMax(1, clip.nestedComposition->width()),
                              qMax(1, clip.nestedComposition->height()));
        media = nested.renderFrameImage(localTimeSeconds, childSize, true, true);
    } else {
        media = loadVideoFrame(clip.mediaId, localTimeSeconds);
        if (media.isNull()) {
            media = loadImageMedia(clip.mediaId);
        }
    }
    if (!media.isNull()) {
        // Place the media by the layer transform instead of stretching it over
        // the canvas: scale is a percentage of the media's own size, position is
        // an offset from the composition centre with Y pointing up, and rotation
        // turns the layer about its anchor point.
        const QPointF scalePct = transform.scaleAt(frame);
        const QPointF pos = transform.positionAt(frame);
        const QSizeF scaled(media.width() * scalePct.x() / 100.0,
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
    for (const composition::Effect& fx : clip.effects) {
        if (!fx.enabled) {
            continue;
        }
        // Behaviors operate on the layer transformation/opacity through their
        // own Notify(102/104) ABI. They are applied once in
        // applyClipBehaviors, after the pixel effects have completed.
        if (plugin::nativeBehaviorRenderingVerified(fx.pluginId)) {
            continue;
        }
        QStringList resolved;
        resolved.reserve(fx.parameterValues.size());
        for (int i = 0; i < fx.parameterValues.size(); ++i) {
            resolved.push_back(fx.parameterAt(i, frame).toString());
        }
        plugin::applyEffectToImage(image, fx.pluginId, resolved);
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
    const auto applyTransformation = [&image](const QMatrix4x4& matrix) {
        const float* m = matrix.constData();
        const QTransform transform(double(m[0]), double(m[1]),
                                   double(m[4]), double(m[5]),
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
    for (const composition::Effect& fx : clip.effects) {
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
                fx.pluginId, values, m_composition.get(), layer.id)) {
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

    const double sx = m_composition && m_composition->width() > 0
        ? double(image.width()) / m_composition->width() : 1.0;
    const double sy = m_composition && m_composition->height() > 0
        ? double(image.height()) / m_composition->height() : 1.0;
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

    matrix.rotate(float(transform.rotationAt(frame)), 0.0f, 0.0f, 1.0f);
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
                               model3d::ShadingMode::Shaded);
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

        // A 3D model layer has no clips: its content is the imported geometry,
        // rasterised over the whole canvas and composited like any other layer.
        if (layer.kind == composition::LayerKind::Model3D) {
            const int frameNo = frameForTime(timeSeconds);
            QImage modelImage = renderModelLayer(layer, size, frameNo);
            if (modelImage.isNull()) {
                continue;
            }
            applyLayerMasks(modelImage, layer);
            const double opacity = layer.transform.opacityAt(frameNo, layer.opacity);
            if (opacity <= 0.0) {
                continue;
            }
            QPainter painter(&out);
            painter.setOpacity(opacity);
            painter.setCompositionMode(compositionModeFor(layer.blendMode));
            painter.drawImage(QPoint(0, 0), modelImage);
            continue;
        }

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

        for (const composition::Clip& clip : layer.clips) {
            const double end = clip.startSeconds + clip.durationSeconds;
            if (timeSeconds < clip.startSeconds || timeSeconds > end) {
                continue;
            }
            if (layer.kind != composition::LayerKind::Plane && !clip.nestedComposition
                && !contributesVideo(clip.mediaId)) {
                continue;
            }
            // Time inside the clip's own material, not just inside the clip:
            // the Slip tool moves the source in-point and Rate Stretch changes
            // the speed, and neither would show on screen if the renderer kept
            // reading the material from its beginning at 1x.
            const double speed = clip.speed > 0.0 ? clip.speed : 1.0;
            const double localTime =
                clip.sourceStartSeconds + (timeSeconds - clip.startSeconds) * speed;
            const int frameNo = frameForTime(timeSeconds);
            QImage clipImage = renderClip(layer, clip, localTime, size, frameNo);

            if (withEffects) {
                applyClipEffects(clipImage, clip, frameNo);
            }
            const int localFrame = frameForTime(timeSeconds - clip.startSeconds);
            const double behaviorOpacity = withEffects
                ? applyClipBehaviors(clipImage, layer, clip, frameNo, localFrame, size)
                : 1.0;
            applyLayerMasks(clipImage, layer);

            // Layer opacity. This used to paint clipImage into itself through a
            // painter with the opacity set, which is a self-blit - source and
            // destination are one buffer - so the result was undefined and the
            // opacity never actually showed. QPainter::setOpacity on the
            // compositing painter multiplies the source alpha during the
            // source-over blend, which is what was meant.
            const double opacity = layer.transform.opacityAt(frameNo, layer.opacity)
                                   * behaviorOpacity;
            if (opacity <= 0.0) {
                continue; // fully transparent contributes nothing
            }

            QPainter painter(&out);
            painter.setOpacity(opacity);
            painter.setCompositionMode(compositionModeFor(layer.blendMode));
            painter.drawImage(QPoint(0, 0), clipImage);
        }
    }

    return out;
}

void RenderWorker::renderFrame(const FrameRequest& request, QByteArray mediaBytes)
{
    Q_UNUSED(mediaBytes);
    if (request.compositionSnapshot) m_composition = request.compositionSnapshot;
    m_frameComplete = true;
    const bool reportTiming = QSettings().value(
        QStringLiteral("Debug/ShowRenderTimings"), false).toBool();
    QElapsedTimer renderTimer;
    if (reportTiming) renderTimer.start();
    const QImage frame = renderFrameImage(request.timeSeconds, request.size, request.renderEffects);
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
                                 bool renderEffects)
{
    if (!size.isValid() || size.isEmpty()) {
        return;
    }
    if (!m_workerThread) {
        spawnWorker();
    }

    FrameRequest request;
    request.frameIndex = frameIndex;
    request.timeSeconds = timeSeconds;
    request.size = size;
    request.renderEffects = renderEffects;
    if (m_composition) {
        request.compositionSnapshot = std::make_shared<composition::Composition>(*m_composition);
        QByteArray signature = composition::layerState(m_composition->layers());
        QDataStream stream(&signature, QIODevice::Append);
        stream << timeSeconds << size << renderEffects << m_composition->width() << m_composition->height()
               << m_composition->fpsNumerator() << m_composition->fpsDenominator();
        request.cacheKey = core::Identifier(QString::fromLatin1(QCryptographicHash::hash(signature, QCryptographicHash::Sha256).toHex()));
        if (auto* cached = m_frameCache.object(request.cacheKey.value())) {
            const QByteArray copy = *cached;
            QTimer::singleShot(0, this, [this, frameIndex, copy, size, key = request.cacheKey.value()] {
                onFrameReady(frameIndex, copy, size, key);
            });
            return;
        }
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

void RenderManager::onFrameReady(int frameIndex, QByteArray rgba, QSize size, QString cacheKey)
{
    if (!cacheKey.isEmpty()) m_frameCache.insert(cacheKey, new QByteArray(rgba), qMax(1, int(rgba.size() / 1024)));
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

void RenderManager::startPlaybackCache(double fromSeconds, const QSize& size, bool renderEffects)
{
    cancelPlaybackCache();
    m_cacheWait.invalidate();
    if (!m_composition || size.isEmpty()) return;
    const double fps = double(m_composition->fpsNumerator()) / qMax(1, m_composition->fpsDenominator());
    if (fps <= 0) return;
    m_cacheFrame = m_cacheFirst = qMax(0, qRound(fromSeconds * fps));
    const qint64 frameBytes = qint64(size.width()) * size.height() * 4;
    const int capacity = int((qint64(m_frameCache.maxCost()) * 1024) / qMax(qint64(1), frameBytes));
    m_cacheLast = qMin(qRound(m_composition->durationSeconds() * fps), m_cacheFirst + capacity);
    m_cacheSize = size; m_cacheEffects = renderEffects; m_caching = true;
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
    requestFrame(--m_cacheRequest, m_cacheFrame / fps, m_cacheSize, m_cacheEffects);
}

} // namespace render
} // namespace openvegas
