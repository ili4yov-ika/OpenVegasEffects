#include "ui/TimelineWidget.h"
#include "ui_Timeline.h"
#include "composition/CompositionState.h"
#include "composition/Transition.h"
#include "ui/TimelineRowDelegate.h"

#include "app/Settings.h"
#include "media/AudioWaveform.h"
#include "ui/EffectPlacement.h"
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include "media/MediaManager.h"
#include "ui/TimelineValueGraphView.h"
#include "model3d/Mesh.h"

#include "plugin/PluginManager.h"
#include <QCoreApplication>
#include <QSettings>
#include <QTreeWidgetItemIterator>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPushButton>
#include <QMenu>
#include <QScrollBar>
#include <QComboBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QSet>
#include <algorithm>
#include <QSplitter>
#include <QToolButton>
#include <QShortcut>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>
#include <QSignalBlocker>
#include <QStyle>
#include <QSlider>
#include <QTimer>
#include <QRegularExpression>
#include <QApplication>

namespace openvegas {
namespace ui {

namespace {
constexpr double kPixelsPerSecond = 80.0;
constexpr int kTrackHeight = 23;
constexpr int kRulerHeight = kTimelineRulerHeight;
// Half-extent of a keyframe diamond, in pixels. crop_screenshot.png is a
// half-scale capture, so the reference's keys are twice the size they look
// there - roughly 14 px across, which is also what makes them comfortable to
// grab with the mouse.
constexpr int kKeyframeRadius = 7;

QString clipLabel(const composition::Clip& clip)
{
    if (clip.nestedComposition) return clip.nestedComposition->name();
    QString label = clip.mediaId.value();
    const int slash = label.lastIndexOf(QLatin1Char('/'));
    const int backslash = label.lastIndexOf(QLatin1Char('\\'));
    const int cut = qMax(slash, backslash);
    if (cut >= 0) {
        label = label.mid(cut + 1);
    }
    if (!clip.effects.isEmpty()) {
        label += QStringLiteral(" +%1").arg(clip.effects.size());
    }
    return label;
}
// Draws the keyframe marker for a temporal type. Same language as the toolbar
// icons: the left half is the incoming side and the right the outgoing, with an
// angular half for linear, a square one for a hold and a round one for eased -
// so the shape on the timeline says how the key behaves without selecting it.
void drawKeyFrameMarker(QPainter& painter, int cx, int cy, int r,
                        composition::TemporalType type)
{
    const auto angularHalf = [cx, cy, r](bool left) {
        QPainterPath p;
        p.moveTo(cx, cy - r);
        p.lineTo(left ? cx - r : cx + r, cy);
        p.lineTo(cx, cy + r);
        p.closeSubpath();
        return p;
    };
    const auto squareHalf = [cx, cy, r](bool left) {
        QPainterPath p;
        p.addRect(QRectF(left ? cx - r : cx, cy - r, r, 2.0 * r));
        return p;
    };
    const auto roundHalf = [cx, cy, r](bool left) {
        QPainterPath p;
        p.moveTo(cx, cy - r);
        p.arcTo(QRectF(cx - r, cy - r, 2.0 * r, 2.0 * r), 90.0, left ? 180.0 : -180.0);
        p.closeSubpath();
        return p;
    };

    QPainterPath path;
    switch (type) {
    case composition::TemporalType::Hold:
        path = squareHalf(true).united(squareHalf(false));
        break;
    case composition::TemporalType::EasyEase:
    case composition::TemporalType::ManualBezier:
        path = roundHalf(true).united(roundHalf(false));
        break;
    case composition::TemporalType::EaseIn:
        path = roundHalf(true).united(angularHalf(false));
        break;
    case composition::TemporalType::EaseOut:
        path = angularHalf(true).united(roundHalf(false));
        break;
    case composition::TemporalType::Linear:
        path = angularHalf(true).united(angularHalf(false));
        break;
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.drawPath(path);
    // Manual bezier keeps the round body but carries its handle bar, which is
    // what tells it apart from Smooth.
    if (type == composition::TemporalType::ManualBezier) {
        const QPen old = painter.pen();
        painter.setPen(QPen(painter.brush().color(), 1.2));
        painter.drawLine(cx - r - 3, cy, cx + r + 3, cy);
        painter.setPen(old);
    }
    painter.setRenderHint(QPainter::Antialiasing, false);
}

// Frame number for a time on the timeline; transform curves are keyed by frame.
int frameForTimeOf(const composition::Composition& comp, double seconds)
{
    const int den = comp.fpsDenominator() > 0 ? comp.fpsDenominator() : 1;
    const double fps = static_cast<double>(comp.fpsNumerator()) / den;
    return static_cast<int>(qRound(seconds * fps));
}

// Value column for a Transform row, formatted the way the reference shows it:
// one number for the scalars, "x  y" for the point-valued properties, and
// opacity in percent.
QString transformValueText(const composition::Layer& layer, composition::TransformProperty prop,
                           int frame)
{
    const composition::LayerTransform& t = layer.transform;
    const QString degree(QChar(0x00B0));
    // A 3D layer's point properties carry a third number; the axis count is
    // what decides how many are shown, so this stays in step with the editors.
    const int axes = composition::axisCount(prop, layer.dimension);

    switch (prop) {
    case composition::TransformProperty::Opacity:
        return QString::number(t.opacityAt(frame, layer.opacity) * 100.0, 'f', 1)
               + QStringLiteral(" %");
    case composition::TransformProperty::Rotation:
        return QString::number(t.rotationAt(frame), 'f', 1) + degree;
    case composition::TransformProperty::RotationX:
        return QString::number(t.rotationXAt(frame), 'f', 1) + degree;
    case composition::TransformProperty::RotationY:
        return QString::number(t.rotationYAt(frame), 'f', 1) + degree;
    case composition::TransformProperty::AudioLevel:
        return QString::number(t.valueAt(prop, 0, frame), 'f', 1) + QStringLiteral(" dB");
    case composition::TransformProperty::AnchorPoint:
    case composition::TransformProperty::Position:
    case composition::TransformProperty::Scale:
    case composition::TransformProperty::Orientation:
        break;
    }

    QStringList parts;
    parts.reserve(axes);
    for (int axis = 0; axis < axes; ++axis) {
        const double value = t.valueAt(prop, axis, frame);
        if (prop == composition::TransformProperty::Scale) {
            parts.append(QStringLiteral("%1 %").arg(value, 0, 'f', 1));
        } else if (prop == composition::TransformProperty::Orientation) {
            parts.append(QString::number(value, 'f', 1) + degree);
        } else {
            parts.append(QString::number(value, 'f', 1));
        }
    }
    return parts.join(QStringLiteral("  "));
}

} // namespace

// ---------------------------------------------------------------------------
// TimelineCanvas
// ---------------------------------------------------------------------------

TimelineCanvas::TimelineCanvas(QWidget* parent)
    : QWidget(parent)
{
    // Keyframes report themselves under the cursor, which needs move events
    // even when no button is down.
    setMouseTracking(true);
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAcceptDrops(true); // effects and transitions from the Effects panel

    m_hScroll = new QScrollBar(Qt::Horizontal, this);
    m_hScroll->setObjectName(QStringLiteral("timelineHScroll"));
    connect(m_hScroll, &QScrollBar::valueChanged, this, [this](int value) {
        m_hOffset = value;
        update();
        emit viewportChanged();
    });
}

double TimelineCanvas::pixelsPerSecond() const
{
    return kPixelsPerSecond * m_zoom;
}

void TimelineCanvas::zoomToFit()
{
    const double duration = m_comp ? m_comp->durationSeconds() : 0.0;
    if (duration <= 0.0 || width() <= 8) {
        return;
    }
    // Leaves a little slack so the last frame is not flush against the edge.
    m_hOffset = 0;
    setZoom((width() - 8) / (duration * kPixelsPerSecond), 0);
}

void TimelineCanvas::setZoom(double factor, int anchorContentX)
{
    const double oldPps = pixelsPerSecond();
    const double anchorTime =
        (anchorContentX >= 0) ? static_cast<double>(anchorContentX) / qMax(1.0, oldPps)
                              : m_playhead;
    // Screen X that currently carries the anchor time (the mouse cursor, or the
    // playhead when no cursor anchor is supplied).
    const int screenAnchor = (anchorContentX >= 0)
                                 ? anchorContentX - m_hOffset
                                 : static_cast<int>(anchorTime * oldPps) - m_hOffset;

    const double newZoom = qBound(0.01, factor, 20.0);
    if (qFuzzyCompare(newZoom, m_zoom) && anchorContentX < 0) {
        update();
        return;
    }
    m_zoom = newZoom;

    const int newContentX = static_cast<int>(anchorTime * pixelsPerSecond());
    syncScrollbar();
    scrollTo(newContentX - screenAnchor);
    // scrollTo only notifies when the offset actually moves; the zoom itself
    // changed the time axis either way, so the other page is told here.
    emit viewportChanged();
}

void TimelineCanvas::setComposition(std::shared_ptr<composition::Composition> comp)
{
    m_comp = std::move(comp);
    syncScrollbar();
    update();
    emit viewportChanged();   // a new duration is a new content width
}


void TimelineCanvas::setPlayheadPosition(double timeSeconds)
{
    m_playhead = timeSeconds;
    ensurePlayheadVisible();
    update();
}

double TimelineCanvas::contentWidth() const
{
    const double duration = m_comp ? m_comp->durationSeconds() : 10.0;
    return duration * pixelsPerSecond();
}

// Height available for content after the horizontal scroll bar is reserved.
int TimelineCanvas::usableHeight() const
{
    return qMax(0, height() - m_scrollbarHeight);
}

void TimelineCanvas::layoutScrollbar()
{
    if (!m_hScroll) {
        return;
    }
    const int sbH = m_hScroll->sizeHint().height();
    m_hScroll->setGeometry(0, qMax(0, height() - sbH), width(), sbH);
    m_hScroll->show();
    m_scrollbarHeight = sbH;
}

// Re-ranges the scroll bar from the current content width and viewport width,
// then clamps the offset so it never points past the content end.
void TimelineCanvas::syncScrollbar()
{
    if (!m_hScroll) {
        return;
    }
    const int viewW = qMax(1, width());
    const int contentW = qMax(viewW, static_cast<int>(contentWidth()));
    m_hScroll->setRange(0, qMax(0, contentW - viewW));
    m_hScroll->setPageStep(viewW);
    m_hScroll->setSingleStep(qMax(10, viewW / 20));
    if (m_hScroll->value() > m_hScroll->maximum()) {
        m_hScroll->setValue(m_hScroll->maximum());
    }
    m_hOffset = m_hScroll->value();
    update();
}

void TimelineCanvas::setScrollOffset(int contentX)
{
    scrollTo(contentX);
}

void TimelineCanvas::scrollTo(int value)
{
    if (!m_hScroll) {
        return;
    }
    const int clamped = qBound(m_hScroll->minimum(), value, m_hScroll->maximum());
    m_hScroll->setValue(clamped); // valueChanged() -> m_hOffset, update
}

void TimelineCanvas::ensurePlayheadVisible()
{
    if (!m_hScroll) {
        return;
    }
    const double pps = pixelsPerSecond();
    const int playheadX = static_cast<int>(m_playhead * pps);
    const int viewW = qMax(1, width());
    if (playheadX < m_hOffset + 20) {
        scrollTo(playheadX - 40);
    } else if (playheadX > m_hOffset + viewW - 40) {
        scrollTo(playheadX - viewW + 40);
    }
}

double TimelineCanvas::frameRate() const
{
    if (m_comp && m_comp->fpsNumerator() > 0 && m_comp->fpsDenominator() > 0) {
        return static_cast<double>(m_comp->fpsNumerator()) / m_comp->fpsDenominator();
    }
    return 30.0;
}

QString TimelineCanvas::formatTimecode(double seconds) const
{
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const double fps = (m_comp && m_comp->fpsNumerator() > 0)
                           ? static_cast<double>(m_comp->fpsNumerator()) / m_comp->fpsDenominator()
                           : 30.0;
    const double frame = seconds * fps;
    const int frames = static_cast<int>(frame + 0.5);
    const int ff = frames % static_cast<int>(fps + 0.5);
    const int totalSeconds = frames / static_cast<int>(fps + 0.5);
    const int h = totalSeconds / 3600;
    const int m = (totalSeconds % 3600) / 60;
    const int s = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(ff, 2, 10, QLatin1Char('0'));
}

void TimelineCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(39, 39, 39));

    const double pps = pixelsPerSecond();
    const double duration = m_comp ? m_comp->durationSeconds() : 10.0;
    const int totalWidth = static_cast<int>(duration * pps);
    if (totalWidth <= 0) {
        return;
    }

    // Everything below uses content coordinates that start at 0; shifting the
    // painter by the scroll offset pans the view, and the clip keeps the drawn
    // content within the visible portion (above the reserved scroll-bar strip).
    painter.translate(-m_hOffset, 0);
    painter.setClipRect(QRect(m_hOffset, 0, qMax(1, width()), usableHeight()));

    painter.fillRect(m_hOffset, 0, width(), kRulerHeight, QColor(73, 80, 85));
    // Frame-rate-aware time ruler with HH:MM:SS:FF labels.
    painter.setPen(QColor(90, 90, 96));
    painter.drawLine(0, kRulerHeight, totalWidth, kRulerHeight);

    // Round to a "nice" tick interval (seconds, then 0.1s/0.5s at high zoom).
    double tickSeconds = 15.0;
    while ((tickSeconds * pps) < 100.0) {
        tickSeconds *= 2.0;
    }
    while ((tickSeconds * pps) > 320.0) {
        tickSeconds *= 0.5;
    }
    QFont rulerFont = painter.font(); rulerFont.setPointSize(7); painter.setFont(rulerFont);
    painter.setPen(QColor(158, 166, 174));
    for (double t = 0.0; t <= duration; t += tickSeconds) {
        const int x = static_cast<int>(t * pps);
        painter.drawLine(x, 0, x, kRulerHeight);
        painter.drawText(x + 3, kRulerHeight - 9, t == 0.0 ? QStringLiteral("0") : formatTimecode(t));
    }
    // Sub-ticks.
    painter.setPen(QColor(116, 124, 131));
    const double subSeconds = tickSeconds / 5.0;
    for (double t = subSeconds; t <= duration; t += subSeconds) {
        const int x = static_cast<int>(t * pps);
        const bool major = qAbs(std::fmod(t, tickSeconds)) < 0.0001;
        if (!major) {
            painter.drawLine(x, 0, x, kRulerHeight / 2);
        }
    }

    // One strip per visible row of the layer tree. Rows that are not the
    // layer's own row (Effects, parameters, ...) get an empty lane, which is
    // where keyframes will live once they exist.
    int contentBottom = kRulerHeight;
    if (!m_lanes.isEmpty()) {
        for (const TimelineLane& lane : m_lanes) {
            const int y = lane.y;
            contentBottom = qMax(contentBottom, y + lane.height);
            if (y + lane.height <= kRulerHeight) {
                continue;
            }
            painter.fillRect(0, y, totalWidth, lane.height,
                             (lane.layerIndex >= 0) ? QColor(53, 53, 53) : QColor(39, 39, 39));
            painter.setPen(QColor(34, 34, 34));
            painter.drawLine(0, y + lane.height - 1, totalWidth, y + lane.height - 1);

            if (lane.layerIndex < 0 || !m_comp
                || lane.layerIndex >= m_comp->layers().size()) {
                continue;
            }

            // Transform row: this property's keys live on the layer's
            // LayerTransform rather than on an effect. A point-valued property
            // (Position, Scale) has one curve per axis but a single row, so the
            // diamonds drawn are the union of both axes - which is how the
            // reference shows a keyed Position
            // (SAMPLES/screenshots/crop_screenshot.png).
            if (lane.isTransformRow()) {
                const composition::Layer& l = m_comp->layers().at(lane.layerIndex);
                const auto prop = static_cast<composition::TransformProperty>(lane.transformProp);
                QSet<int> frames;
                for (int axis = 0; axis < composition::axisCount(prop, l.dimension); ++axis) {
                    const composition::KeyFrameList* c = l.transform.curve(prop, axis);
                    if (!c) {
                        continue;
                    }
                    const QVector<int> locations = c->locations();
                    for (int frameNo : locations) {
                        frames.insert(frameNo);
                    }
                }
                if (frames.isEmpty()) {
                    continue;
                }
                const double fps = frameRate();
                const int cy = y + lane.height / 2;
                painter.setBrush(QColor(232, 232, 232));
                painter.setPen(Qt::NoPen);
                const int laneIndex = static_cast<int>(&lane - m_lanes.constData());
                for (int frameNo : frames) {
                    const int kx = static_cast<int>((frameNo / fps) * pps);
                    const bool dragged = laneIndex == m_dragLane && frameNo == m_dragFrame;
                    painter.setBrush(dragged ? QColor(18, 176, 255) : QColor(232, 232, 232));
                    drawKeyFrameMarker(painter, kx, cy, kKeyframeRadius,
                                       keyFrameTypeAt(lane, frameNo));
                }
                painter.setBrush(Qt::NoBrush);
                continue;
            }

            // Parameter row: draw its keyframes as diamonds, the way the
            // reference marks an animated property along the timeline.
            if (lane.isParameterRow()) {
                const composition::Layer& l = m_comp->layers().at(lane.layerIndex);
                if (lane.clipIndex < 0 || lane.clipIndex >= l.clips.size()) {
                    continue;
                }
                const composition::Clip& cl = l.clips.at(lane.clipIndex);
                if (lane.effectIndex < 0 || lane.effectIndex >= cl.effects.size()) {
                    continue;
                }
                const composition::Effect& fx = cl.effects.at(lane.effectIndex);
                const auto animIt = fx.animation.constFind(lane.parameterIndex);
                if (animIt == fx.animation.constEnd() || animIt->isEmpty()) {
                    continue;
                }
                const double fps = frameRate();
                const int cy = y + lane.height / 2;


                painter.setBrush(QColor(232, 232, 232));
                painter.setPen(Qt::NoPen);
                const int laneIndex = static_cast<int>(&lane - m_lanes.constData());
                for (int frameNo : animIt->locations()) {
                    const int kx = static_cast<int>((frameNo / fps) * pps);
                    const bool dragged = laneIndex == m_dragLane && frameNo == m_dragFrame;
                    painter.setBrush(dragged ? QColor(18, 176, 255) : QColor(232, 232, 232));
                    const composition::KeyFrame* key = animIt->at(frameNo);
                    drawKeyFrameMarker(painter, kx, cy, kKeyframeRadius,
                                       key ? key->temporal : composition::TemporalType::Linear);
                }
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QColor(34, 34, 34));
                continue;
            }

            const composition::Layer& layer = m_comp->layers().at(lane.layerIndex);
            for (int c = 0; c < layer.clips.size(); ++c) {
                const composition::Clip& clip = layer.clips.at(c);
                // Drawn where the clip actually starts. Packing them end to end
                // regardless, as this used to, made the timeline disagree with
                // the rendered picture the moment a clip did not begin where
                // the previous one ended.
                const int cx = static_cast<int>(clip.startSeconds * pps);
                const int cw = qMax(1, static_cast<int>(clip.durationSeconds * pps));
                // The reference tints every shot of the selected layer slate
                // (#60737d) and leaves the rest neutral grey; the clip picked
                // within that layer additionally gets a light outline.
                const bool layerSelected = lane.layerIndex == m_selLayer;
                const bool selected = layerSelected && c == m_selClip;
                painter.fillRect(cx, y + 1, cw, lane.height - 2,
                                 layerSelected ? QColor(96, 115, 125) : QColor(80, 80, 80));
                if (selected && layer.clips.size() > 1) {
                    painter.setPen(QColor(143, 163, 175));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRect(cx, y + 1, cw - 1, lane.height - 3);
                }
                painter.setPen(selected ? QColor(200, 255, 200) : QColor(220, 220, 225));
                drawClipWaveform(painter, clip, QRect(cx, y + 1, cw, lane.height - 2), pps);

                painter.setPen(QColor(34, 34, 34));
            }
            // Transitions span the cut (or the faded clip edge); drawn over the
            // clips as the usual NLE diagonal so their extent is visible.
            const auto windows = composition::transitionWindows(
                layer, 0.5 / qMax(1.0, frameRate()), nullptr);
            for (const composition::TransitionWindow& window : windows) {
                const QRectF band(window.start * pps, y + 1,
                                  qMax(2.0, (window.end - window.start) * pps), lane.height - 2);
                painter.fillRect(band, QColor(214, 170, 60, 110));
                painter.setPen(QColor(240, 210, 120));
                if (window.fromClip >= 0 && window.toClip < 0) {
                    painter.drawLine(band.topLeft(), band.bottomRight());
                } else {
                    painter.drawLine(band.bottomLeft(), band.topRight());
                }
                painter.setPen(QColor(34, 34, 34));
            }
        }
    }

    const int playheadX = static_cast<int>(m_playhead * pps);
    const int contentH = qMax(contentBottom, usableHeight());
    painter.setPen(QColor(255, 255, 255));
    painter.drawLine(playheadX, 0, playheadX, contentH);
}

void TimelineCanvas::setWaveformSource(std::shared_ptr<media::MediaManager> media,
                                       media::WaveformCache* cache)
{
    m_media = std::move(media);
    if (m_waveforms != cache) {
        if (m_waveforms) m_waveforms->disconnect(this);
        m_waveforms = cache;
        if (m_waveforms) {
            connect(m_waveforms, &media::WaveformCache::peaksReady, this,
                    qOverload<>(&QWidget::update));
        }
    }
    update();
}

void TimelineCanvas::drawClipWaveform(QPainter& painter, const composition::Clip& clip,
                                      const QRect& rect, double pps)
{
    if (!m_media || !m_waveforms || rect.height() < 6 || pps <= 0.0) return;
    const media::MediaAsset asset = m_media->assetById(clip.mediaId);
    if (asset.kind() != media::MediaKind::Audio && asset.kind() != media::MediaKind::Video) return;
    const media::WaveformPeaks* peaks = m_waveforms->peaks(asset.filePath(), asset.audioStreamIndex());
    if (!peaks) return;
    // Options > General "Audio Waveforms" and "Log waveform". Older settings
    // hold the translated item text rather than the English one.
    const QSettings settings = app::Settings::optionSettings();
    const QString style = settings.value(QStringLiteral("Options/AudioWaveforms")).toString();
    const bool peak = style == QLatin1String("Peak Amplitude")
        || style == QCoreApplication::translate("openvegas::ui::OptionsDialog", "Peak Amplitude");
    const bool logarithmic = settings.value(QStringLiteral("Options/LogWaveform"), true).toBool();
    // Only the visible part of the clip is sampled, one column per pixel;
    // slip (source start) and rate stretch (speed) map it onto the media.
    const int left = qMax(rect.left(), m_hOffset);
    const int right = qMin(rect.right() + 1, m_hOffset + width());
    if (right <= left) return;
    const double speed = clip.speed > 0.0 ? clip.speed : 1.0;
    const auto sourceAt = [&](int x) {
        return clip.sourceStartSeconds + (x / pps - clip.startSeconds) * speed;
    };
    const QVector<float> heights = media::waveformColumns(
        *peaks, sourceAt(left), sourceAt(right), right - left,
        peak ? media::WaveformStyle::Peak : media::WaveformStyle::Rms, logarithmic);
    const double middle = rect.center().y() + 0.5;
    const double half = rect.height() * 0.5 - 1.0;
    painter.save();
    painter.setPen(QColor(172, 214, 236, 170));
    for (int column = 0; column < heights.size(); ++column) {
        const double extent = heights.at(column) * half;
        if (extent < 0.5) continue;
        const int x = left + column;
        painter.drawLine(QPointF(x + 0.5, middle - extent), QPointF(x + 0.5, middle + extent));
    }
    painter.restore();
}

void TimelineCanvas::setLanes(const QVector<TimelineLane>& lanes)
{
    m_lanes = lanes;
    update();
}

int TimelineCanvas::frameAtContentX(double contentX) const
{
    const double pps = pixelsPerSecond();
    if (pps <= 0.0) {
        return 0;
    }
    return qMax(0, static_cast<int>(qRound((contentX / pps) * frameRate())));
}

// Both the track canvas and the value graph turn a tree row into the curves it
// stands for through this, so a lane means the same thing on either page.
QVector<composition::KeyFrameList*> laneCurves(composition::Composition* comp,
                                               const TimelineLane& lane)
{
    QVector<composition::KeyFrameList*> out;
    if (!comp || lane.layerIndex < 0 || lane.layerIndex >= comp->layers().size()) {
        return out;
    }
    composition::Layer& layer = comp->layerRef(lane.layerIndex);

    if (lane.isTransformRow()) {
        const auto prop = static_cast<composition::TransformProperty>(lane.transformProp);
        for (int axis = 0; axis < composition::axisCount(prop, layer.dimension); ++axis) {
            if (composition::KeyFrameList* curve = layer.transform.curve(prop, axis)) {
                out.append(curve);
            }
        }
        return out;
    }

    if (lane.isParameterRow()) {
        if (lane.clipIndex < 0 || lane.clipIndex >= layer.clips.size()) {
            return out;
        }
        composition::Clip& clip = layer.clips[lane.clipIndex];
        if (lane.effectIndex < 0 || lane.effectIndex >= clip.effects.size()) {
            return out;
        }
        composition::Effect& effect = clip.effects[lane.effectIndex];
        const auto it = effect.animation.find(lane.parameterIndex);
        if (it != effect.animation.end()) {
            out.append(&it.value());
        }
    }
    return out;
}

QVector<int> TimelineCanvas::keyFrameLocations(const TimelineLane& lane) const
{
    // Union of every axis the row carries: a keyed Position shows one diamond
    // for the pair, which is how the reference draws it.
    QSet<int> frames;
    const QVector<composition::KeyFrameList*> curves = laneCurves(m_comp.get(), lane);
    for (const composition::KeyFrameList* curve : curves) {
        const QVector<int> locations = curve->locations();
        for (int frameNo : locations) {
            frames.insert(frameNo);
        }
    }
    QVector<int> out(frames.begin(), frames.end());
    std::sort(out.begin(), out.end());
    return out;
}

bool TimelineCanvas::keyFrameAt(const QPoint& pos, int* laneIndex, int* frame) const
{
    const double contentX = pos.x() + m_hOffset;
    const double pps = pixelsPerSecond();
    const double fps = frameRate();
    for (int i = 0; i < m_lanes.size(); ++i) {
        const TimelineLane& lane = m_lanes.at(i);
        if (!lane.isParameterRow() && !lane.isTransformRow()) {
            continue;
        }
        if (pos.y() < lane.y || pos.y() >= lane.y + lane.height) {
            continue;
        }
        const QVector<int> locations = keyFrameLocations(lane);
        for (int frameNo : locations) {
            const double kx = (frameNo / fps) * pps;
            if (qAbs(contentX - kx) <= kKeyframeRadius) {
                if (laneIndex) {
                    *laneIndex = i;
                }
                if (frame) {
                    *frame = frameNo;
                }
                return true;
            }
        }
    }
    return false;
}

bool TimelineCanvas::moveKeyFrames(const TimelineLane& lane, int fromFrame, int toFrame)
{
    if (!m_comp || fromFrame == toFrame || toFrame < 0) {
        return false;
    }
    if (lane.layerIndex < 0 || lane.layerIndex >= m_comp->layers().size()) {
        return false;
    }
    composition::Layer& layer = m_comp->layerRef(lane.layerIndex);

    const auto moveIn = [fromFrame, toFrame](composition::KeyFrameList* curve) {
        if (!curve) {
            return false;
        }
        const composition::KeyFrame* source = curve->at(fromFrame);
        if (!source) {
            return false;
        }
        // moveById inserts at the target frame, which would overwrite whatever
        // sits there. Refuse instead, so dragging one key over another cannot
        // silently destroy it.
        const composition::KeyFrame* occupant = curve->at(toFrame);
        if (occupant && occupant->id != source->id) {
            return false;
        }
        return curve->moveById(source->id, toFrame);
    };

    bool moved = false;
    if (lane.isTransformRow()) {
        const auto prop = static_cast<composition::TransformProperty>(lane.transformProp);
        // Both axes of a point-valued property share the row, so they move
        // together and stay on the same frames.
        for (int axis = 0; axis < composition::axisCount(prop, layer.dimension); ++axis) {
            const bool axisMoved = moveIn(layer.transform.curve(prop, axis));
            moved = axisMoved || moved;
        }
        return moved;
    }

    if (lane.isParameterRow()) {
        if (lane.clipIndex < 0 || lane.clipIndex >= layer.clips.size()) {
            return false;
        }
        composition::Clip& clip = layer.clips[lane.clipIndex];
        if (lane.effectIndex < 0 || lane.effectIndex >= clip.effects.size()) {
            return false;
        }
        composition::Effect& effect = clip.effects[lane.effectIndex];
        const auto it = effect.animation.find(lane.parameterIndex);
        if (it == effect.animation.end()) {
            return false;
        }
        moved = moveIn(&it.value());
    }
    return moved;
}

// Temporal type of the key this lane shows at `frame`; the first axis that
// carries one wins, since a point property keeps both in step.
composition::TemporalType TimelineCanvas::keyFrameTypeAt(const TimelineLane& lane, int frame) const
{
    if (!m_comp || lane.layerIndex < 0 || lane.layerIndex >= m_comp->layers().size()) {
        return composition::TemporalType::Linear;
    }
    const composition::Layer& layer = m_comp->layers().at(lane.layerIndex);
    if (lane.isTransformRow()) {
        const auto prop = static_cast<composition::TransformProperty>(lane.transformProp);
        for (int axis = 0; axis < composition::axisCount(prop, layer.dimension); ++axis) {
            if (const composition::KeyFrameList* curve = layer.transform.curve(prop, axis)) {
                if (const composition::KeyFrame* key = curve->at(frame)) {
                    return key->temporal;
                }
            }
        }
    }
    return composition::TemporalType::Linear;
}

bool TimelineCanvas::setKeyFrameType(const TimelineLane& lane, int frame,
                                     composition::TemporalType type)
{
    if (!m_comp || lane.layerIndex < 0 || lane.layerIndex >= m_comp->layers().size()) {
        return false;
    }
    composition::Layer& layer = m_comp->layerRef(lane.layerIndex);

    // set() rewrites value and type together, so the existing value is read
    // back and handed straight in - only the interpolation changes.
    const auto applyTo = [frame, type](composition::KeyFrameList* curve) {
        if (!curve) {
            return false;
        }
        const composition::KeyFrame* existing = curve->at(frame);
        if (!existing) {
            return false;
        }
        curve->set(frame, existing->value, type);
        return true;
    };

    bool changed = false;
    if (lane.isTransformRow()) {
        const auto prop = static_cast<composition::TransformProperty>(lane.transformProp);
        // Both axes of a point property share the row, so they stay in step.
        for (int axis = 0; axis < composition::axisCount(prop, layer.dimension); ++axis) {
            const bool axisChanged = applyTo(layer.transform.curve(prop, axis));
            changed = axisChanged || changed;
        }
        return changed;
    }

    if (lane.isParameterRow()) {
        if (lane.clipIndex < 0 || lane.clipIndex >= layer.clips.size()) {
            return false;
        }
        composition::Clip& clip = layer.clips[lane.clipIndex];
        if (lane.effectIndex < 0 || lane.effectIndex >= clip.effects.size()) {
            return false;
        }
        composition::Effect& effect = clip.effects[lane.effectIndex];
        const auto it = effect.animation.find(lane.parameterIndex);
        if (it == effect.animation.end()) {
            return false;
        }
        changed = applyTo(&it.value());
    }
    return changed;
}

// Right-clicking a keyframe offers its interpolation type. The reference has a
// button per type on its keyframe bar - toolButtonKeyFrameTypeConstant,
// -Linear, -AutoSmooth, -AutoSmoothIn, -AutoSmoothOut, -ManualBezier - and the
// model here already carried the matching TemporalType with its handle presets;
// only the way to choose one was missing.
void TimelineCanvas::contextMenuEvent(QContextMenuEvent* event)
{
    int laneIndex = -1;
    int frame = -1;
    if (!keyFrameAt(event->pos(), &laneIndex, &frame)) {
        // A composite-shot clip offers the reference's AssetPreRenderMenu.
        int layerIndex = -1, clipIndex = -1;
        if (m_comp && clipAt(event->pos(), &layerIndex, &clipIndex, nullptr)
            && m_comp->layers().at(layerIndex).clips.at(clipIndex).nestedComposition) {
            QMenu menu(this);
            QMenu* preRender = menu.addMenu(tr("Pre-Render"));
            preRender->setObjectName(QStringLiteral("timelinePreRenderMenu"));
            QAction* make = preRender->addAction(tr("Make Pre-Render(s)"));
            QAction* remove = preRender->addAction(tr("Remove Pre-Render(s)"));
            QAction* chosen = menu.exec(event->globalPos());
            if (chosen == make || chosen == remove) {
                emit shotPreRenderRequested(layerIndex, clipIndex, chosen == make);
            }
            return;
        }
        QWidget::contextMenuEvent(event);
        return;
    }

    // Current type, taken from the first curve that carries this key, so the
    // menu can show which one is active.
    composition::TemporalType current = composition::TemporalType::Linear;
    const TimelineLane& lane = m_lanes.at(laneIndex);
    if (m_comp && lane.layerIndex >= 0 && lane.layerIndex < m_comp->layers().size()) {
        const composition::Layer& layer = m_comp->layers().at(lane.layerIndex);
        const composition::KeyFrame* key = nullptr;
        if (lane.isTransformRow()) {
            const auto prop = static_cast<composition::TransformProperty>(lane.transformProp);
            for (int axis = 0; axis < composition::axisCount(prop, layer.dimension) && !key; ++axis) {
                if (const composition::KeyFrameList* c = layer.transform.curve(prop, axis)) {
                    key = c->at(frame);
                }
            }
        } else if (lane.isParameterRow() && lane.clipIndex >= 0
                   && lane.clipIndex < layer.clips.size()) {
            const composition::Clip& clip = layer.clips.at(lane.clipIndex);
            if (lane.effectIndex >= 0 && lane.effectIndex < clip.effects.size()) {
                const auto it =
                    clip.effects.at(lane.effectIndex).animation.constFind(lane.parameterIndex);
                if (it != clip.effects.at(lane.effectIndex).animation.constEnd()) {
                    key = it->at(frame);
                }
            }
        }
        if (key) {
            current = key->temporal;
        }
    }

    QMenu menu(this);
    const composition::TemporalType types[] = {
        composition::TemporalType::Hold,      composition::TemporalType::Linear,
        composition::TemporalType::EasyEase,  composition::TemporalType::EaseIn,
        composition::TemporalType::EaseOut,   composition::TemporalType::ManualBezier,
    };
    for (composition::TemporalType type : types) {
        QAction* action = menu.addAction(
            QCoreApplication::translate("TemporalType", composition::temporalTypeName(type)));
        action->setCheckable(true);
        action->setChecked(type == current);
        connect(action, &QAction::triggered, this, [this, laneIndex, frame, type] {
            if (laneIndex < 0 || laneIndex >= m_lanes.size()) {
                return;
            }
            emit editStarted();
            if (setKeyFrameType(m_lanes.at(laneIndex), frame, type)) {
                emit keyFramesEdited();
                update();
            }
        });
    }
    menu.exec(event->globalPos());
    event->accept();
}

void TimelineCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit editStarted();
        // A press that lands on a keyframe starts dragging it rather than
        // scrubbing - otherwise the playhead would jump under the pointer and
        // the drag would fight the scrub.
        int laneIndex = -1;
        int frame = -1;
        if (keyFrameAt(event->pos(), &laneIndex, &frame)) {
            if (m_comp && m_comp->layers()[m_lanes[laneIndex].layerIndex].locked) return;
            m_dragLane = laneIndex;
            m_dragFrame = frame;
            emit keySelected(m_lanes[laneIndex], frame);
            update();
            event->accept();
            return;
        }
        // What the press means is the active tool's business. Only Select
        // scrubs on empty space; the rest act on the clip under the pointer,
        // and every one of them used to fall through to a scrub because the
        // canvas never knew which tool was chosen.
        if (m_tool == EditorTool::Hand) {
            m_handPanning = true;
            m_handAnchorX = event->pos().x();
            m_handAnchorOffset = m_hOffset;
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        if (m_tool == EditorTool::Slice) {
            if (sliceClipAt(event->pos())) {
                update();
                emit clipsEdited();
            }
            event->accept();
            return;
        }
        if (m_tool == EditorTool::TrackSelect) {
            selectFromHere(event->pos());
            event->accept();
            return;
        }

        int layerIndex = -1;
        int clipIndex = -1;
        int effectIndex = -1;
        if (transitionHandleAt(event->pos(), &layerIndex, &clipIndex, &effectIndex)) {
            if (m_comp->layers()[layerIndex].locked) return;
            m_transitionLayer = layerIndex;
            m_transitionClip = clipIndex;
            m_transitionEffect = effectIndex;
            m_selLayer = layerIndex;
            m_selClip = clipIndex;
            emit clipSelected(layerIndex, clipIndex);
            update();
            event->accept();
            return;
        }
        ClipEdge edge = ClipEdge::None;
        if (clipAt(event->pos(), &layerIndex, &clipIndex, &edge)) {
            if (m_comp->layers()[layerIndex].locked) return;
            m_selLayer = layerIndex;
            m_selClip = clipIndex;
            emit clipSelected(layerIndex, clipIndex);
            beginClipDrag(layerIndex, clipIndex, edge, timeAtX(event->pos().x()));
            update();
            event->accept();
            return;
        }

        // Empty space: scrub, as before.
        m_playhead = timeAtX(event->pos().x());
        emit timeScrubbed(m_playhead);
        pickClipAt(event->pos());
    }
    QWidget::mousePressEvent(event);
}

void TimelineCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragLane >= 0 && m_dragLane < m_lanes.size()) {
        const int target = frameAtContentX(event->pos().x() + m_hOffset);
        if (target != m_dragFrame && moveKeyFrames(m_lanes.at(m_dragLane), m_dragFrame, target)) {
            m_dragFrame = target;
            emit keyFrameDragged();
            update();
        }
        event->accept();
        return;
    }
    if (m_handPanning) {
        // Hand drags the view under the pointer, so the content follows the
        // hand rather than running away from it.
        scrollTo(m_handAnchorOffset - (event->pos().x() - m_handAnchorX));
        event->accept();
        return;
    }

    if (m_dragClipLayer >= 0) {
        if (applyToolDrag(timeAtX(event->pos().x()) - m_dragStartTime)) {
            update();
        }
        event->accept();
        return;
    }
    if (m_transitionLayer >= 0) {
        if (dragTransitionTo(timeAtX(event->pos().x()))) {
            update();
        }
        event->accept();
        return;
    }

    if (event->buttons() & Qt::LeftButton) {
        m_playhead = timeAtX(event->pos().x());
        emit timeScrubbed(m_playhead);
    } else {
        updateToolCursor(event->pos());
    }
    QWidget::mouseMoveEvent(event);
}

void TimelineCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragLane >= 0) {
        m_dragLane = -1;
        m_dragFrame = -1;
        update();
        // Commit once, at the end: rebuilding the row tree on every mouse-move
        // would be wasteful and would churn the lane vector the drag indexes
        // into.
        emit keyFramesEdited();
        event->accept();
        return;
    }
    if (m_handPanning) {
        m_handPanning = false;
        updateToolCursor(event->pos());
        event->accept();
        return;
    }
    if (m_dragClipLayer >= 0) {
        m_dragClipLayer = -1;
        m_dragClipIndex = -1;
        m_dragEdge = ClipEdge::None;
        m_dragFollowStarts.clear();
        // Committed once, at the end: the rows and the project's dirty flag
        // follow the finished edit, not every intermediate mouse-move.
        emit clipsEdited();
        event->accept();
        return;
    }
    if (m_transitionLayer >= 0) {
        m_transitionLayer = m_transitionClip = m_transitionEffect = -1;
        emit clipsEdited();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void TimelineCanvas::dragEnterEvent(QDragEnterEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (mime->hasFormat(QString::fromLatin1(kEffectMimeType))
        || mime->hasFormat(QString::fromLatin1(kMediaMimeType))
        || mime->hasFormat(QString::fromLatin1(kCompositeShotMimeType))) {
        event->acceptProposedAction();
    }
}

void TimelineCanvas::dragMoveEvent(QDragMoveEvent* event)
{
    int layerIndex = -1, clipIndex = -1;
    const QPoint pos = event->position().toPoint();
    // Media and composite shots land anywhere: they make a layer of their own.
    if (m_comp && (event->mimeData()->hasFormat(QString::fromLatin1(kMediaMimeType))
                   || event->mimeData()->hasFormat(QString::fromLatin1(kCompositeShotMimeType)))) {
        event->acceptProposedAction();
        return;
    }
    if (event->mimeData()->hasFormat(QString::fromLatin1(kEffectMimeType))
        && clipAt(pos, &layerIndex, &clipIndex, nullptr)
        && !m_comp->layers().at(layerIndex).locked) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TimelineCanvas::dropEvent(QDropEvent* event)
{
    int layerIndex = -1, clipIndex = -1;
    const QPoint pos = event->position().toPoint();
    const QMimeData* mime = event->mimeData();
    const bool media = mime->hasFormat(QString::fromLatin1(kMediaMimeType));
    if (m_comp && (media || mime->hasFormat(QString::fromLatin1(kCompositeShotMimeType)))) {
        // Above the layer whose rows it is let go over (its property rows
        // included), below them all past the last row, on top above the first.
        int above = -1;
        int bottom = 0;
        for (const TimelineLane& lane : m_lanes) {
            bottom = qMax(bottom, lane.y + lane.height);
            if (lane.y > pos.y()) break;
            if (lane.layerIndex >= 0) above = lane.layerIndex;
        }
        if (!m_lanes.isEmpty() && pos.y() >= bottom) above = m_comp->layers().size();
        const double seconds = qMax(0.0, timeAtX(pos.x()));
        event->acceptProposedAction();
        if (media)
            emit mediaDropped(QString::fromUtf8(mime->data(QString::fromLatin1(kMediaMimeType))), above, seconds);
        else
            emit compositeShotDropped(QString::fromUtf8(mime->data(QString::fromLatin1(kCompositeShotMimeType))),
                                      above, seconds);
        return;
    }
    const QByteArray id = event->mimeData()->data(QString::fromLatin1(kEffectMimeType));
    if (id.isEmpty() || !clipAt(pos, &layerIndex, &clipIndex, nullptr)
        || m_comp->layers().at(layerIndex).locked) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    m_selLayer = layerIndex;
    m_selClip = clipIndex;
    emit clipSelected(layerIndex, clipIndex);
    emit effectDropped(layerIndex, clipIndex, QString::fromUtf8(id), timeAtX(pos.x()));
    update();
}

bool TimelineCanvas::transitionHandleAt(const QPoint& pos, int* layerIndex, int* clipIndex,
                                        int* effectIndex) const
{
    if (!m_comp || m_tool != EditorTool::Select) return false;
    constexpr int kGrab = 4;
    for (const TimelineLane& lane : m_lanes) {
        if (lane.isParameterRow() || lane.isTransformRow()) continue;
        if (lane.layerIndex < 0 || lane.layerIndex >= m_comp->layers().size()) continue;
        if (pos.y() < lane.y || pos.y() >= lane.y + lane.height) continue;
        const composition::Layer& layer = m_comp->layers().at(lane.layerIndex);
        const double tolerance = 0.5 / qMax(1.0, frameRate());
        for (const auto& window : composition::transitionWindows(layer, tolerance, nullptr)) {
            // Only edges that are not also a clip edge: those stay trim
            // handles. An overlap transition is sized by the clips themselves.
            const composition::Clip& owner = layer.clips.at(window.ownerClip);
            const bool atStartOfClip = qAbs(window.start - owner.startSeconds) <= tolerance;
            const bool atEndOfClip = qAbs(window.end - owner.endSeconds()) <= tolerance;
            const bool overlap = window.fromClip >= 0 && window.toClip >= 0 && atStartOfClip;
            if (overlap) continue;
            const bool left = !atStartOfClip && qAbs(pos.x() - xForTime(window.start)) <= kGrab;
            const bool right = !atEndOfClip && qAbs(pos.x() - xForTime(window.end)) <= kGrab;
            if (left || right) {
                *layerIndex = lane.layerIndex;
                *clipIndex = window.ownerClip;
                *effectIndex = window.effectIndex;
                return true;
            }
        }
    }
    return false;
}

bool TimelineCanvas::dragTransitionTo(double seconds)
{
    if (!m_comp || m_transitionLayer < 0 || m_transitionLayer >= m_comp->layers().size()) {
        return false;
    }
    composition::Layer& layer = m_comp->layerRef(m_transitionLayer);
    if (m_transitionClip < 0 || m_transitionClip >= layer.clips.size()) return false;
    composition::Clip& clip = layer.clips[m_transitionClip];
    if (m_transitionEffect < 0 || m_transitionEffect >= clip.effects.size()) return false;
    composition::Effect& effect = clip.effects[m_transitionEffect];
    const double tolerance = 0.5 / qMax(1.0, frameRate());
    double length = effect.transitionSeconds;
    if (effect.transitionEdge == composition::TransitionEdge::Out) {
        length = clip.endSeconds() - seconds;
    } else if (composition::transitionPredecessor(layer, m_transitionClip, tolerance) < 0) {
        length = seconds - clip.startSeconds;
    } else {
        length = 2.0 * qAbs(seconds - clip.startSeconds); // centred on the cut
    }
    // Whole frames, at least one, and never longer than the clip.
    const double fps = qMax(1.0, frameRate());
    length = qBound(1.0 / fps, qRound(length * fps) / fps, qMax(1.0 / fps, clip.durationSeconds));
    if (qFuzzyCompare(length, effect.transitionSeconds)) return false;
    effect.transitionSeconds = length;
    return true;
}

void TimelineCanvas::setTool(EditorTool tool)
{
    if (m_tool == tool) {
        return;
    }
    m_tool = tool;
    // A tool change ends any drag in progress: the gesture was started under
    // the old meaning and finishing it under the new one would be a surprise.
    m_dragClipLayer = -1;
    m_dragClipIndex = -1;
    m_dragEdge = ClipEdge::None;
    setCursor(Qt::ArrowCursor);
}

double TimelineCanvas::timeAtX(int widgetX) const
{
    return qMax(0.0, (widgetX + m_hOffset) / pixelsPerSecond());
}

int TimelineCanvas::xForTime(double seconds) const
{
    return static_cast<int>(seconds * pixelsPerSecond()) - m_hOffset;
}

// Clips are laid out at their own startSeconds, which is what the renderer
// composites them at. They used to be drawn packed end to end regardless, so
// the timeline disagreed with the picture as soon as a clip did not begin
// where the previous one ended - and every tool that moves a clip in time
// would have moved nothing visible.
bool TimelineCanvas::clipAt(const QPoint& pos, int* layerIndex, int* clipIndex,
                            ClipEdge* edge) const
{
    if (edge) {
        *edge = ClipEdge::None;
    }
    if (!m_comp) {
        return false;
    }
    const int kEdgeGrab = 6;   // width of the trim zone at each end, in pixels
    for (const TimelineLane& lane : m_lanes) {
        if (lane.isParameterRow() || lane.isTransformRow()) continue;
        if (lane.layerIndex < 0 || lane.layerIndex >= m_comp->layers().size()) {
            continue;
        }
        if (pos.y() < lane.y || pos.y() >= lane.y + lane.height) {
            continue;
        }
        const composition::Layer& layer = m_comp->layers().at(lane.layerIndex);
        for (int c = 0; c < layer.clips.size(); ++c) {
            const composition::Clip& clip = layer.clips.at(c);
            const int left = xForTime(clip.startSeconds);
            const int right = xForTime(clip.endSeconds());
            if (pos.x() < left || pos.x() >= right) {
                continue;
            }
            if (layerIndex) {
                *layerIndex = lane.layerIndex;
            }
            if (clipIndex) {
                *clipIndex = c;
            }
            if (edge) {
                // A clip narrower than two grab zones would be all edge and
                // could never be moved, so the zones shrink with it.
                const int grab = qMin(kEdgeGrab, qMax(1, (right - left) / 3));
                if (pos.x() < left + grab) {
                    *edge = ClipEdge::Left;
                } else if (pos.x() >= right - grab) {
                    *edge = ClipEdge::Right;
                }
            }
            return true;
        }
    }
    return false;
}

double TimelineCanvas::snapTime(double seconds, int ignoreLayer, int ignoreClip) const
{
    if (!m_snap || !m_comp) {
        return seconds;
    }
    // Snapping is a distance in pixels, not in time: it has to feel the same
    // however far the timeline is zoomed in.
    const double tolerance = 8.0 / qMax(1.0, pixelsPerSecond());

    double best = seconds;
    double bestDistance = tolerance;
    const auto consider = [&](double candidate) {
        const double distance = qAbs(candidate - seconds);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
        }
    };

    consider(m_playhead);
    consider(0.0);
    const QVector<composition::Layer>& layers = m_comp->layers();
    for (int li = 0; li < layers.size(); ++li) {
        const composition::Layer& layer = layers.at(li);
        for (int ci = 0; ci < layer.clips.size(); ++ci) {
            if (li == ignoreLayer && ci == ignoreClip) {
                continue;   // a clip does not snap to itself
            }
            consider(layer.clips.at(ci).startSeconds);
            consider(layer.clips.at(ci).endSeconds());
        }
    }
    return best;
}

void TimelineCanvas::beginClipDrag(int layerIndex, int clipIndex, ClipEdge edge, double pressTime)
{
    m_dragClipLayer = layerIndex;
    m_dragClipIndex = clipIndex;
    m_dragEdge = edge;
    m_dragStartTime = pressTime;
    m_dragFollowStarts.clear();

    const composition::Layer& layer = m_comp->layers().at(layerIndex);
    const composition::Clip& clip = layer.clips.at(clipIndex);
    m_dragOriginalStart = clip.startSeconds;
    m_dragOriginalDuration = clip.durationSeconds;
    m_dragOriginalSource = clip.sourceStartSeconds;
    m_dragOriginalSpeed = clip.speed;

    // Every tool works from the geometry as it was when the button went down,
    // so a long drag cannot accumulate rounding and dragging back to the start
    // restores exactly what was there.
    if (clipIndex > 0) {
        const composition::Clip& previous = layer.clips.at(clipIndex - 1);
        m_dragPrevStart = previous.startSeconds;
        m_dragPrevDuration = previous.durationSeconds;
    }
    if (clipIndex + 1 < layer.clips.size()) {
        const composition::Clip& next = layer.clips.at(clipIndex + 1);
        m_dragNextStart = next.startSeconds;
        m_dragNextDuration = next.durationSeconds;
        m_dragNextSource = next.sourceStartSeconds;
    }
    // Ripple moves everything after the clip, so their original starts are
    // needed too - shifting them one mouse-move at a time would compound.
    for (int i = clipIndex + 1; i < layer.clips.size(); ++i) {
        m_dragFollowStarts.append(layer.clips.at(i).startSeconds);
    }
}

bool TimelineCanvas::applyToolDrag(double deltaSeconds)
{
    if (!m_comp || m_dragClipLayer < 0 || m_dragClipLayer >= m_comp->layers().size()) {
        return false;
    }
    composition::Layer& layer = m_comp->layerRef(m_dragClipLayer);
    if (m_dragClipIndex < 0 || m_dragClipIndex >= layer.clips.size()) {
        return false;
    }
    composition::Clip& clip = layer.clips[m_dragClipIndex];
    const bool hasPrevious = m_dragClipIndex > 0;
    const bool hasNext = m_dragClipIndex + 1 < layer.clips.size();
    // The shortest a clip may become. A zero-length clip cannot be grabbed
    // again, so no tool is allowed to produce one.
    const double kMinimum = 1.0 / qMax(1.0, frameRate());
    const double originalEnd = m_dragOriginalStart + m_dragOriginalDuration;

    switch (m_tool) {
    case EditorTool::Select: {
        if (m_dragEdge == ClipEdge::Left) {
            // Trim the head: the start moves and the length follows, so the
            // tail stays put. The material stays put on the timeline too, which
            // is why the source in-point travels with the start.
            double start = snapTime(m_dragOriginalStart + deltaSeconds, m_dragClipLayer,
                                    m_dragClipIndex);
            start = qBound(0.0, start, originalEnd - kMinimum);
            clip.sourceStartSeconds =
                qMax(0.0, m_dragOriginalSource + (start - m_dragOriginalStart));
            clip.startSeconds = start;
            clip.durationSeconds = originalEnd - start;
        } else if (m_dragEdge == ClipEdge::Right) {
            double end = snapTime(originalEnd + deltaSeconds, m_dragClipLayer, m_dragClipIndex);
            end = qMax(m_dragOriginalStart + kMinimum, end);
            clip.durationSeconds = end - m_dragOriginalStart;
        } else {
            const double start = snapTime(m_dragOriginalStart + deltaSeconds, m_dragClipLayer,
                                          m_dragClipIndex);
            clip.startSeconds = qMax(0.0, start);
        }
        return true;
    }

    case EditorTool::Slip: {
        // Slip edit: the clip keeps its place and its length; only the material
        // under it moves. Dragging right reveals later material, so the source
        // in-point follows the pointer.
        clip.sourceStartSeconds = qMax(0.0, m_dragOriginalSource + deltaSeconds);
        return true;
    }

    case EditorTool::Slide: {
        // Slide edit: the clip moves in time and its neighbours give way - the
        // previous one lengthens or shortens and the next one does the
        // opposite, so the run keeps the span it had.
        double shift = deltaSeconds;
        if (hasPrevious) {
            shift = qMax(shift, (m_dragPrevStart + kMinimum) - m_dragOriginalStart);
        } else {
            shift = qMax(shift, -m_dragOriginalStart);
        }
        if (hasNext) {
            const double nextEnd = m_dragNextStart + m_dragNextDuration;
            shift = qMin(shift, (nextEnd - kMinimum) - originalEnd);
        }
        clip.startSeconds = m_dragOriginalStart + shift;
        if (hasPrevious) {
            layer.clips[m_dragClipIndex - 1].durationSeconds = clip.startSeconds - m_dragPrevStart;
        }
        if (hasNext) {
            composition::Clip& next = layer.clips[m_dragClipIndex + 1];
            const double nextEnd = m_dragNextStart + m_dragNextDuration;
            next.sourceStartSeconds =
                qMax(0.0, m_dragNextSource + (clip.endSeconds() - m_dragNextStart));
            next.startSeconds = clip.endSeconds();
            next.durationSeconds = nextEnd - next.startSeconds;
        }
        return true;
    }

    case EditorTool::Ripple: {
        // Ripple edit: changing this clip's length pushes everything after it
        // on the same layer by the same amount, so no gap opens and none closes.
        double end = snapTime(originalEnd + deltaSeconds, m_dragClipLayer, m_dragClipIndex);
        end = qMax(m_dragOriginalStart + kMinimum, end);
        clip.durationSeconds = end - m_dragOriginalStart;
        const double shift = clip.durationSeconds - m_dragOriginalDuration;
        // Measured from the starts captured at the press, not from where the
        // clips are now: shifting them again on every mouse-move would compound.
        for (int i = m_dragClipIndex + 1; i < layer.clips.size(); ++i) {
            const int captured = i - (m_dragClipIndex + 1);
            if (captured >= m_dragFollowStarts.size()) {
                break;
            }
            layer.clips[i].startSeconds = qMax(0.0, m_dragFollowStarts.at(captured) + shift);
        }
        return true;
    }

    case EditorTool::Roll: {
        // Roll edit: the joint between two clips moves and both change length,
        // so the pair still covers exactly the span it did.
        if (m_dragEdge == ClipEdge::Left && hasPrevious) {
            composition::Clip& previous = layer.clips[m_dragClipIndex - 1];
            double joint = snapTime(m_dragOriginalStart + deltaSeconds, m_dragClipLayer,
                                    m_dragClipIndex);
            joint = qBound(m_dragPrevStart + kMinimum, joint, originalEnd - kMinimum);
            previous.durationSeconds = joint - m_dragPrevStart;
            clip.sourceStartSeconds =
                qMax(0.0, m_dragOriginalSource + (joint - m_dragOriginalStart));
            clip.startSeconds = joint;
            clip.durationSeconds = originalEnd - joint;
            return true;
        }
        if (hasNext) {
            composition::Clip& next = layer.clips[m_dragClipIndex + 1];
            const double nextEnd = m_dragNextStart + m_dragNextDuration;
            double joint = snapTime(originalEnd + deltaSeconds, m_dragClipLayer, m_dragClipIndex);
            joint = qBound(m_dragOriginalStart + kMinimum, joint, nextEnd - kMinimum);
            clip.durationSeconds = joint - m_dragOriginalStart;
            // The next clip's head is trimmed by the same amount the joint
            // moved, so its material stays where it was on the timeline.
            next.sourceStartSeconds = qMax(0.0, m_dragNextSource + (joint - m_dragNextStart));
            next.startSeconds = joint;
            next.durationSeconds = nextEnd - joint;
            return true;
        }
        return false;
    }

    case EditorTool::Stretch: {
        // Rate Stretch: the clip's length and its speed change together, so the
        // same material plays over a different span. Dragging the right edge
        // out makes it longer and therefore slower.
        const double end =
            qMax(m_dragOriginalStart + kMinimum, originalEnd + deltaSeconds);
        clip.durationSeconds = end - m_dragOriginalStart;
        if (clip.durationSeconds > 0.0 && m_dragOriginalDuration > 0.0) {
            clip.speed = m_dragOriginalSpeed * (m_dragOriginalDuration / clip.durationSeconds);
        }
        return true;
    }

    case EditorTool::Hand:
    case EditorTool::Slice:
    case EditorTool::TrackSelect:
        break;
    }
    return false;
}

// Slice: the clip under the pointer becomes two, cut at the time clicked. The
// tail keeps reading the material that follows the cut, which is what makes a
// slice invisible until one half is moved.
bool TimelineCanvas::sliceClipAt(const QPoint& pos)
{
    int layerIndex = -1;
    int clipIndex = -1;
    if (!m_comp || !clipAt(pos, &layerIndex, &clipIndex, nullptr)) {
        return false;
    }
    composition::Layer& layer = m_comp->layerRef(layerIndex);
    if (layer.locked) return false;
    composition::Clip& clip = layer.clips[clipIndex];
    const double cut = snapTime(timeAtX(pos.x()), -1, -1);
    const double kMinimum = 1.0 / qMax(1.0, frameRate());
    if (cut <= clip.startSeconds + kMinimum || cut >= clip.endSeconds() - kMinimum) {
        return false;   // nothing to cut off
    }

    composition::Clip tail = clip;               // effects and settings travel with it
    tail.startSeconds = cut;
    tail.durationSeconds = clip.endSeconds() - cut;
    tail.sourceStartSeconds = clip.sourceStartSeconds + (cut - clip.startSeconds) * clip.speed;
    clip.durationSeconds = cut - clip.startSeconds;
    layer.clips.insert(clipIndex + 1, tail);
    return true;
}

void TimelineCanvas::updateToolCursor(const QPoint& pos)
{
    // The pointer says what the tool will do before the button goes down,
    // which is the only hint the strip's icons do not already give.
    int transitionLayer = -1, transitionClip = -1, transitionEffect = -1;
    if (keyFrameAt(pos, nullptr, nullptr)
        || transitionHandleAt(pos, &transitionLayer, &transitionClip, &transitionEffect)) {
        setCursor(Qt::SizeHorCursor);
        return;
    }
    ClipEdge edge = ClipEdge::None;
    const bool overClip = clipAt(pos, nullptr, nullptr, &edge);

    switch (m_tool) {
    case EditorTool::Hand:
        setCursor(Qt::OpenHandCursor);
        return;
    case EditorTool::Slice:
        setCursor(overClip ? Qt::SplitHCursor : Qt::ArrowCursor);
        return;
    case EditorTool::Slip:
    case EditorTool::Slide:
    case EditorTool::Ripple:
    case EditorTool::Stretch:
        setCursor(overClip ? Qt::SizeHorCursor : Qt::ArrowCursor);
        return;
    case EditorTool::Roll:
        setCursor(overClip && edge != ClipEdge::None ? Qt::SplitHCursor : Qt::ArrowCursor);
        return;
    case EditorTool::TrackSelect:
        setCursor(Qt::PointingHandCursor);
        return;
    case EditorTool::Select:
        break;
    }
    setCursor(overClip && edge != ClipEdge::None ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void TimelineCanvas::pickClipAt(const QPoint& pos)
{
    m_selLayer = -1;
    m_selClip = -1;
    if (!m_comp) {
        update();
        return;
    }
    int layerIndex = -1;
    int clipIndex = -1;
    if (clipAt(pos, &layerIndex, &clipIndex, nullptr)) {
        m_selLayer = layerIndex;
        m_selClip = clipIndex;
        emit clipSelected(layerIndex, clipIndex);
    }
    update();
}

// Track Select Forwards: everything from the click onwards on this layer. The
// model carries a single selected clip, so the reference's multi-selection
// becomes "select the first of them and put the playhead at the click", which
// is the part of the gesture this port can honour.
void TimelineCanvas::selectFromHere(const QPoint& pos)
{
    if (!m_comp) {
        return;
    }
    const double at = timeAtX(pos.x());
    for (const TimelineLane& lane : m_lanes) {
        if (lane.layerIndex < 0 || lane.layerIndex >= m_comp->layers().size()) {
            continue;
        }
        if (pos.y() < lane.y || pos.y() >= lane.y + lane.height) {
            continue;
        }
        const composition::Layer& layer = m_comp->layers().at(lane.layerIndex);
        for (int c = 0; c < layer.clips.size(); ++c) {
            if (layer.clips.at(c).endSeconds() > at) {
                m_selLayer = lane.layerIndex;
                m_selClip = c;
                emit clipSelected(lane.layerIndex, c);
                m_playhead = at;
                emit timeScrubbed(m_playhead);
                update();
                return;
            }
        }
    }
    update();
}

void TimelineCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_hScroll) {
        layoutScrollbar();
        syncScrollbar();
    }
}

void TimelineCanvas::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        QWidget::wheelEvent(event);
        return;
    }

    // Ctrl+wheel zooms around the time under the cursor; Shift+wheel pans
    // horizontally. A plain wheel is left to QMainWindow (timeline zooming is
    // normally Ctrl+wheel in the reference).
    if (event->modifiers() & Qt::ControlModifier) {
        const int cursorX = event->position().toPoint().x();
        const int anchorContentX = cursorX + m_hOffset;
        const double factor = (delta > 0) ? 1.25 : 0.8;
        setZoom(m_zoom * factor, anchorContentX);
        event->accept();
        return;
    }
    if (event->modifiers() & Qt::ShiftModifier) {
        scrollTo(m_hScroll ? m_hScroll->value() - delta : m_hOffset - delta);
        event->accept();
        return;
    }
    emit verticalScrollRequested(-delta);
    event->accept();
}

// ---------------------------------------------------------------------------
// TimelineWidget (container with header bar)
// ---------------------------------------------------------------------------

TimelineWidget::TimelineWidget(QWidget* parent)
    : QDockWidget(parent)
{
    Ui::TimelineWidget form;
    form.setupUi(this);
    m_toolStrip = form.timelineToolStrip;
    m_header = form.header;
    m_headerSpacer = form.headerSpacer;
    m_timecode = form.timelineTimecode;
    m_newLayerButton = form.timelineNewLayer;
    m_search = form.timelineSearch;
    m_tree = form.timelineLayerTree;
    m_canvas = form.widgetTimelinePage;
    m_valueGraphView = form.valueGraphPage;
    m_timelineStack = form.stackedWidgetTimelines;
    m_splitter = form.splitter;
    m_zoomSlider = form.timelineZoomSlider;
    m_vScroll = form.timelineVScroll;
    m_compositionTabs = form.timelineCompositionTabs;
    m_panelPages = form.timelinePanelPages;
    m_startPage = form.timelineStartPage;
    m_startPageLayout = form.startPageLayout;
    m_startTabButton = form.timelineStartTab;
    m_panelMenuButton = form.timelinePanelMenu;
    m_toggleKeyButton = form.timelineToggleKeyframe;
    m_valueGraphButton = form.timelineValueGraph;
    m_graphAutoZoomButton = form.timelineGraphAutoZoom;
    m_snapButton = form.timelineSnap;

    setStyleSheet(QStringLiteral(R"(
        QWidget#TimelinePanelWidget { background:#222222; color:#cccccc; }
        QWidget#timelineEditorPage QWidget { background:transparent; }
        QWidget#timelineEditorPage QTreeWidget { background:#222222; border:0; outline:0; }
        QWidget#timelineEditorPage QTreeWidget::item { height:23px; padding:0; border-bottom:1px solid #242424; }
        QWidget#timelineEditorPage QTreeWidget::item:selected { background:#566873; }
        QWidget#timelineEditorPage QTreeWidget::branch { background:#222222; }
        QWidget#timelineEditorPage QTreeWidget::branch:has-children:closed { image:url(:/icons/caret-right.svg); }
        QWidget#timelineEditorPage QTreeWidget::branch:has-children:open { image:url(:/icons/caret-down.svg); }
        QWidget#timelineEditorPage QToolButton { padding:0; border:0; border-radius:0; background:transparent; }
        QWidget#timelineEditorPage QToolButton:hover { background:#454545; }
        QWidget#timelineEditorPage QToolButton:checked { background:#009fdb; }
        QWidget#timelineEditorPage QLineEdit, QWidget#timelineEditorPage QComboBox,
        QWidget#timelineEditorPage QAbstractSpinBox { background:transparent; border:0; border-radius:0; padding:0 3px; min-height:18px; color:#d1d1d1; }
        QWidget#timelineEditorPage QLineEdit:focus, QWidget#timelineEditorPage QAbstractSpinBox:focus { background:#111111; }
        QLineEdit#timelineTimecode { font-weight:bold; font-size:14px; }
        QWidget#timelineFooter { background:#1b1b1b; border-top:1px solid #101010; }
        QToolButton#timelineStartTab { padding:0 10px; border:0; border-right:1px solid #333333; color:#8f8f8f; background:#1b1b1b; }
        QToolButton#timelineStartTab:hover { color:#dfdfdf; background:#222222; }
        QToolButton#timelineStartTab:checked { color:#ffffff; background:#222222; border-bottom:1px solid #12b0ff; }
        QToolButton#timelinePanelMenu { padding:4px; border:0; background:#1b1b1b; }
        QToolButton#timelinePanelMenu:hover { background:#454545; }
        QTabBar#timelineCompositionTabs { background:#1b1b1b; }
        QTabBar#timelineCompositionTabs::tab { height:23px; min-width:72px; padding:0 20px 0 10px; margin:0; color:#a9a9a9; background:#222222; border:0; border-right:1px solid #333333; }
        QTabBar#timelineCompositionTabs::tab:hover { color:#e0e0e0; background:#303030; }
        QTabBar#timelineCompositionTabs::tab:selected { color:#ffffff; background:#222222; border-bottom:1px solid #12b0ff; }
        QTabBar#timelineCompositionTabs[startPageActive="true"]::tab:selected { color:#a9a9a9; background:#1b1b1b; border-bottom:0; }
        QTabBar#timelineCompositionTabs::close-button { subcontrol-position:right; width:12px; height:12px; image:url(:/icons/tab-close.svg); }
        QTabBar#timelineCompositionTabs::close-button:hover { background:#454545; }
        QSlider#timelineZoomSlider::groove:horizontal { height:3px; background:#111111; border:1px solid #363636; }
        QSlider#timelineZoomSlider::handle:horizontal { width:8px; margin:-4px 0; background:#d1d1d1; border-radius:4px; }
    )"));

    // The footer below is the panel's own navigation. Suppress the native
    // dock title/tab chrome which otherwise creates the duplicate row seen in
    // the broken layout.
    auto* dockTitle = new QWidget(this);
    dockTitle->setFixedHeight(0);
    setTitleBarWidget(dockTitle);

    const QIcon listIcon(QStringLiteral(":/icons/list.svg"));
    m_panelMenuButton->setIcon(listIcon);
    m_startTabButton->setChecked(false);
    connect(m_startTabButton, &QToolButton::clicked,
            this, &TimelineWidget::showStartPage);

    auto* panelMenu = new QMenu(m_panelMenuButton);
    QAction* startAction = panelMenu->addAction(tr("Start"));
    connect(startAction, &QAction::triggered, this, &TimelineWidget::showStartPage);
    QAction* propertiesAction = panelMenu->addAction(tr("Composite Shot Properties..."));
    connect(propertiesAction, &QAction::triggered,
            this, &TimelineWidget::editCompositionProperties);
    QAction* closeAction = panelMenu->addAction(tr("Close Active Tab"));
    connect(closeAction, &QAction::triggered, this, [this] {
        if (m_compositionTabs && m_compositionTabs->currentIndex() >= 0)
            emit compositionTabCloseRequested(m_compositionTabs->currentIndex());
    });
    m_panelMenuButton->setMenu(panelMenu);

    form.timelineClock->setIcon(QIcon(QStringLiteral(":/icons/clock.svg")));
    connect(form.timelineClock, &QToolButton::clicked, this, [this] {
        m_framesDisplay = !m_framesDisplay;
        if (m_comp) m_comp->editorSequence().timelineTimeFormat = m_framesDisplay ? 1001 : 1000;
        updateTimecodeLabel(playhead());
    });
    connect(m_timecode, &QLineEdit::editingFinished, this, [this] {
        if (!m_comp) return;
        const QString input = m_timecode->text().trimmed();
        const double fps = double(m_comp->fpsNumerator()) / qMax(1, m_comp->fpsDenominator());
        bool ok = false; int frame = 0;
        const auto parts = input.split(QRegularExpression(QStringLiteral("[:;]")));
        if (parts.size() == 4) {
            int values[4] = {}; ok = true;
            for (int i = 0; i < 4; ++i) {
                bool valid = false; values[i] = parts[i].toInt(&valid); ok &= valid && values[i] >= 0;
            }
            ok &= values[1] < 60 && values[2] < 60 && values[3] < qRound(fps);
            frame = ((values[0] * 3600 + values[1] * 60 + values[2]) * qRound(fps)) + values[3];
        } else if (input.endsWith(QLatin1Char('s'))) {
            frame = qRound(input.chopped(1).toDouble(&ok) * fps);
        } else frame = input.toInt(&ok);
        if (ok && frame >= 0)
            seekToFrame(qMin(frame, qMax(0, qRound(m_comp->durationSeconds() * fps) - 1)));
        updateTimecodeLabel(playhead());
    });

    m_newLayerButton->setIcon(QIcon(QStringLiteral(":/icons/add.svg")));
    auto* layerMenu = new QMenu(m_newLayerButton);
    for (composition::LayerKind kind : {composition::LayerKind::Point, composition::LayerKind::Text,
             composition::LayerKind::Grade, composition::LayerKind::Light,
             composition::LayerKind::Camera, composition::LayerKind::Plane}) {
        QAction* action = layerMenu->addAction(tr("New %1 Layer").arg(
            QCoreApplication::translate("LayerKind", composition::layerKindName(kind))));
        connect(action, &QAction::triggered, this, [this, kind] { emit newLayerRequested(kind); });
    }
    m_newLayerButton->setMenu(layerMenu);
    form.toolButtonMakeCompositeShot->setIcon(QIcon(QStringLiteral(":/icons/make-composite.svg")));
    connect(form.toolButtonMakeCompositeShot, &QToolButton::clicked,
            this, &TimelineWidget::makeCompositeShotRequested);

    form.timelinePreviousKeyframe->setIcon(QIcon(QStringLiteral(":/icons/key-frame-previous.svg")));
    m_toggleKeyButton->setIcon(QIcon(QStringLiteral(":/icons/key-frame-off.svg")));
    form.timelineNextKeyframe->setIcon(QIcon(QStringLiteral(":/icons/key-frame-next.svg")));
    connect(form.timelinePreviousKeyframe, &QToolButton::clicked,
            this, &TimelineWidget::goToPreviousKeyFrame);
    connect(m_toggleKeyButton, &QToolButton::clicked,
            this, &TimelineWidget::toggleSelectedKeyFrame);
    connect(form.timelineNextKeyframe, &QToolButton::clicked,
            this, &TimelineWidget::goToNextKeyFrame);
    const struct { QToolButton* button; composition::TemporalType type; const char* icon; } keyTypes[] = {
        {form.timelineKeyFrameTypekey_frame, composition::TemporalType::Linear, "key-frame"},
        {form.timelineKeyFrameTypekey_frame_hold, composition::TemporalType::Hold, "key-frame-hold"},
        {form.timelineKeyFrameTypekey_frame_ease_in, composition::TemporalType::EaseIn, "key-frame-ease-in"},
        {form.timelineKeyFrameTypekey_frame_easy_ease, composition::TemporalType::EasyEase, "key-frame-easy-ease"},
        {form.timelineKeyFrameTypekey_frame_ease_out, composition::TemporalType::EaseOut, "key-frame-ease-out"},
        {form.timelineKeyFrameTypekey_frame_manual_bezier, composition::TemporalType::ManualBezier, "key-frame-manual-bezier"},
    };
    for (const auto& entry : keyTypes) {
        // uic sanitizes '-' in C++ member names and also uses the sanitized
        // value as objectName. Restore the reference runtime identifiers.
        entry.button->setObjectName(
            QStringLiteral("timelineKeyFrameType%1").arg(QLatin1String(entry.icon)));
        entry.button->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(entry.icon))));
        entry.button->setProperty("temporalType", int(entry.type));
        m_keyTypeButtons.append(entry.button);
        connect(entry.button, &QToolButton::clicked, this,
                [this, type = entry.type] { setSelectedKeyFrameType(type); });
    }
    m_valueGraphButton->setIcon(QIcon(QStringLiteral(":/icons/animation-graph.svg")));
    m_graphAutoZoomButton->setIcon(QIcon(QStringLiteral(":/icons/zoom-fit.svg")));
    form.timelineRenderCache->setIcon(QIcon(QStringLiteral(":/icons/render-timeline.svg")));
    form.timelineExport->setIcon(QIcon(QStringLiteral(":/icons/export-video.svg")));
    connect(form.timelineRenderCache, &QToolButton::clicked, this, &TimelineWidget::preRenderRequested);
    connect(form.timelineExport, &QToolButton::clicked, this, &TimelineWidget::exportRequested);
    connect(m_valueGraphButton, &QToolButton::toggled, this, [this](bool on) {
        m_timelineStack->setCurrentIndex(on ? 1 : 0);
        if (on) syncValueGraphViewport();
        m_graphAutoZoomButton->setEnabled(on);
        if (m_comp) m_comp->editorSequence().timelineValueGraph = on;
        emit valueGraphToggled(on);
    });
    connect(m_graphAutoZoomButton, &QToolButton::toggled, this, [this](bool on) {
        m_valueGraphView->setAutoZoom(on);
        if (m_comp) m_comp->editorSequence().timelineGraphAutoZoom = on;
    });

    struct ToolDef { EditorTool tool; QToolButton* button; const char* icon; QKeySequence shortcut; };
    const ToolDef visibleTools[] = {
        {EditorTool::Select, form.toolButtonPointer, "pointer", QKeySequence(Qt::Key_V)},
        {EditorTool::Hand, form.toolButtonHand, "hand", QKeySequence(Qt::Key_H)},
        {EditorTool::Slice, form.toolButtonSlice, "timeline-slice", QKeySequence(Qt::Key_C)},
        {EditorTool::Stretch, form.toolButtonStretch, "timeline-stretch", QKeySequence(Qt::Key_S)},
    };
    m_toolButtons.resize(9);
    for (const auto& def : visibleTools) {
        def.button->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(def.icon))));
        m_toolButtons[int(def.tool)] = def.button;
        connect(def.button, &QToolButton::clicked, this, [this, tool = def.tool] { setTool(tool); });
        auto* shortcut = new QShortcut(def.shortcut, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, tool = def.tool] { setTool(tool); });
    }
    const struct { EditorTool tool; const char* icon; const char* text; QKeySequence shortcut; } alternateTools[] = {
        {EditorTool::Slip, "slip", QT_TR_NOOP("Slip edit (Y)"), QKeySequence(Qt::Key_Y)},
        {EditorTool::Slide, "slide", QT_TR_NOOP("Slide edit (Shift+U)"), QKeySequence(Qt::SHIFT | Qt::Key_U)},
        {EditorTool::Ripple, "ripple", QT_TR_NOOP("Ripple edit (R)"), QKeySequence(Qt::Key_R)},
        {EditorTool::Roll, "roll", QT_TR_NOOP("Roll edit (E)"), QKeySequence(Qt::Key_E)},
        {EditorTool::TrackSelect, "track-select-forward", QT_TR_NOOP("Track select forwards (A)"), QKeySequence(Qt::Key_A)},
    };
    auto* alternatives = new QMenu(form.toolButtonStretch);
    for (const auto& def : alternateTools) {
        auto* hidden = new QToolButton(m_toolStrip);
        const QIcon icon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(def.icon)));
        hidden->setIcon(icon);
        hidden->setToolTip(tr(def.text)); hidden->hide();
        m_toolButtons[int(def.tool)] = hidden;
        QAction* action = alternatives->addAction(icon, tr(def.text));
        connect(action, &QAction::triggered, this, [this, tool = def.tool] { setTool(tool); });
        auto* shortcut = new QShortcut(def.shortcut, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, tool = def.tool] { setTool(tool); });
    }
    form.toolButtonStretch->setMenu(alternatives);
    m_snapButton->setIcon(QIcon(QStringLiteral(":/icons/snap.svg")));
    m_snapButton->setChecked(m_snap);
    connect(m_snapButton, &QToolButton::clicked, this, &TimelineWidget::setSnapEnabled);
    auto* snapShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_S), this);
    snapShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(snapShortcut, &QShortcut::activated, this, [this] { setSnapEnabled(!m_snap); });
    form.timelineSettings->setIcon(QIcon(QStringLiteral(":/icons/settings.svg")));
    connect(form.timelineSettings, &QToolButton::clicked, this, &TimelineWidget::editCompositionProperties);
    updateToolButtons();

    m_tree->setItemDelegate(new TimelineRowDelegate(m_tree));
    m_tree->setTopInset(0);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_tree->setColumnWidth(0, 218);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* row, int col) {
        if (m_rebuilding || col != 0 || !row->data(0, Qt::UserRole + 2).isValid()
            || row->data(0, Qt::UserRole + 3).isValid() || !m_comp) return;
        const int layer = row->data(0, Qt::UserRole).toInt();
        const int clip = row->data(0, Qt::UserRole + 1).toInt();
        const int effect = row->data(0, Qt::UserRole + 2).toInt();
        if (m_comp->layers()[layer].locked) { scheduleRefresh(); return; }
        const bool enabled = row->checkState(0) == Qt::Checked;
        editLayer(layer, tr("Enable effect"), [=](composition::Layer& value) {
            value.clips[clip].effects[effect].enabled = enabled;
        });
    });

    connect(m_canvas, &TimelineCanvas::editStarted, this, &TimelineWidget::beginModelEdit);
    connect(m_canvas, &TimelineCanvas::keySelected, this, [this](const TimelineLane& lane, int frame) {
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
            auto* row = *it;
            if (row->data(0, Qt::UserRole).toInt() != lane.layerIndex) continue;
            const bool match = lane.isTransformRow()
                ? row->data(0, Qt::UserRole + 4).isValid() && row->data(0, Qt::UserRole + 4).toInt() == lane.transformProp
                : row->data(0, Qt::UserRole + 3).isValid() && row->data(0, Qt::UserRole + 3).toInt() == lane.parameterIndex
                  && row->data(0, Qt::UserRole + 2).toInt() == lane.effectIndex
                  && row->data(0, Qt::UserRole + 1).toInt() == lane.clipIndex;
            if (match) { m_tree->setCurrentItem(row); seekToFrame(frame); break; }
        }
    });
    connect(m_canvas, &TimelineCanvas::timeScrubbed, this,
            [this](double time) { setPlayheadPosition(time); emit timeScrubbed(time); });
    connect(m_canvas, &TimelineCanvas::clipSelected, this, &TimelineWidget::clipSelected);
    connect(m_canvas, &TimelineCanvas::shotPreRenderRequested, this,
            &TimelineWidget::shotPreRenderRequested);
    connect(m_canvas, &TimelineCanvas::mediaDropped, this, &TimelineWidget::mediaDropped);
    connect(m_canvas, &TimelineCanvas::compositeShotDropped, this, &TimelineWidget::compositeShotDropped);
    connect(m_canvas, &TimelineCanvas::effectDropped, this,
            [this](int layerIndex, int clipIndex, const QString& pluginId, double seconds) {
        if (!m_pluginManager) return;
        const plugin::EffectSpec spec = m_pluginManager->spec(plugin::PluginId(pluginId));
        if (!spec.id.isValid()) return;
        editLayer(layerIndex, tr("Add %1").arg(spec.displayName),
                  [spec, clipIndex, seconds](composition::Layer& layer) {
            if (clipIndex < 0 || clipIndex >= layer.clips.size()) return;
            addEffectToClip(layer.clips[clipIndex], spec, seconds);
        }, true);
    });
    connect(m_canvas, &TimelineCanvas::keyFrameDragged, this, &TimelineWidget::keyFramesChanged);
    connect(m_canvas, &TimelineCanvas::keyFramesEdited, this,
            [this] { finishModelEdit(tr("Move keyframe"), true); });
    connect(m_canvas, &TimelineCanvas::clipsEdited, this,
            [this] { finishModelEdit(tr("Edit clips"), true); });
    m_canvas->setTool(m_tool); m_canvas->setSnapEnabled(m_snap);

    form.filterIcon->setPixmap(QIcon(QStringLiteral(":/icons/filter.svg")).pixmap(13, 13));
    connect(m_search, &QLineEdit::textChanged, this,
            [this](const QString& text) { applySearch(); emit searchFilterChanged(text); });
    form.timelineZoomOut->setIcon(QIcon(QStringLiteral(":/icons/zoom-out.svg")));
    form.timelineZoomIn->setIcon(QIcon(QStringLiteral(":/icons/zoom-in.svg")));
    connect(form.timelineZoomOut, &QToolButton::clicked, this, &TimelineWidget::zoomOut);
    connect(form.timelineZoomIn, &QToolButton::clicked, this, &TimelineWidget::zoomIn);
    connect(m_zoomSlider, &QSlider::valueChanged, this,
            [this](int value) { setZoom(0.01 * std::pow(2000., value / 1000.)); });

    m_canvas->installEventFilter(this);
    m_valueGraphView->installEventFilter(this);
    qApp->installEventFilter(this);
    connect(m_valueGraphView, &TimelineValueGraphView::timeScrubbed, this,
            [this](double time) { setPlayheadPosition(time); emit timeScrubbed(time); });
    connect(m_valueGraphView, &TimelineValueGraphView::keyFramesDragged,
            this, &TimelineWidget::keyFramesChanged);
    connect(m_valueGraphView, &TimelineValueGraphView::keyFramesEdited, this,
            [this] { finishModelEdit(tr("Edit value graph"), true); });
    connect(m_valueGraphView, &TimelineValueGraphView::scrollRequested,
            m_canvas, &TimelineCanvas::setScrollOffset);
    connect(m_valueGraphView, &TimelineValueGraphView::zoomRequested, m_canvas,
            [this](double factor, int anchor) { m_canvas->setZoom(m_canvas->zoom() * factor, anchor); });
    connect(m_canvas, &TimelineCanvas::viewportChanged, this, &TimelineWidget::syncValueGraphViewport);

    m_splitter->setStretchFactor(0, 0); m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({440, 900});
    QScrollBar* treeBar = m_tree->verticalScrollBar();
    connect(m_canvas, &TimelineCanvas::verticalScrollRequested, treeBar,
            [treeBar](int delta) { treeBar->setValue(treeBar->value() + delta / 40 * treeBar->singleStep()); });
    const auto mirrorRange = [this, treeBar] {
        m_vScroll->setRange(treeBar->minimum(), treeBar->maximum());
        m_vScroll->setPageStep(treeBar->pageStep());
        m_vScroll->setSingleStep(treeBar->singleStep());
        m_vScroll->setEnabled(treeBar->maximum() > treeBar->minimum());
    };
    mirrorRange();
    connect(treeBar, &QScrollBar::rangeChanged, this, [mirrorRange](int, int) { mirrorRange(); });
    connect(treeBar, &QScrollBar::valueChanged, m_vScroll, &QScrollBar::setValue);
    connect(m_vScroll, &QScrollBar::valueChanged, treeBar, &QScrollBar::setValue);
    connect(m_compositionTabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index < 0) return;
        showTimelinePage();
        emit compositionTabActivated(index);
    });
    connect(m_compositionTabs, &QTabBar::tabBarClicked, this, [this](int index) {
        // QTabBar keeps one current tab while Start is visible. Clicking that
        // same tab therefore has no currentChanged signal, so handle this one
        // case explicitly.
        if (index >= 0 && isStartPageVisible()) {
            showTimelinePage();
            emit compositionTabActivated(index);
        }
    });
    connect(m_compositionTabs, &QTabBar::tabCloseRequested, this, &TimelineWidget::compositionTabCloseRequested);
    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) { syncHeaderToTree(); });
    connect(m_tree, &TimelineTree::decorationClicked, this, [this](QTreeWidgetItem* item) { toggleKeyFrame(item); });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this] { syncLanes(); });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this] { syncLanes(); });
    connect(m_tree, &TimelineTree::layoutShifted, this, [this] { syncLanes(); });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        QTreeWidgetItem* item = m_tree->currentItem();
        if (!item || !item->data(0, Qt::UserRole).isValid()) return;
        const int layer = item->data(0, Qt::UserRole).toInt();
        const QVariant clip = item->data(0, Qt::UserRole + 1);
        QVector<int> selectedLayers;
        for (QTreeWidgetItem* selected : m_tree->selectedItems()) {
            const QVariant value = selected->data(0, Qt::UserRole);
            if (value.isValid() && !selectedLayers.contains(value.toInt())) selectedLayers.append(value.toInt());
        }
        std::sort(selectedLayers.begin(), selectedLayers.end());
        emit layersSelected(selectedLayers);
        emit clipSelected(layer, clip.isValid() ? clip.toInt() : -1);
        m_canvas->setSelection(layer, clip.isValid() ? clip.toInt()
            : (m_comp && m_comp->layers()[layer].clips.size() == 1 ? 0 : -1));
        syncLanes(); updateKeyButtons();
    });
    updateTimecodeLabel(0);
    updateKeyButtons();
    QTimer::singleShot(0, this, &TimelineWidget::syncHeaderToTree);
}



void TimelineWidget::setComposition(std::shared_ptr<composition::Composition> comp)
{
    m_editPending = false; m_editBefore.clear();
    const auto restored = comp ? comp->editorSequence() : composition::EditorSequence{};
    m_comp = comp;
    m_canvas->setComposition(comp);
    m_valueGraphView->setComposition(comp);

    // Restore the Value Graph / Auto Zoom toggles from the sequence's timeline
    // state (read by VegfxSerializer::loadFromFile from <TimelineValueGraph> /
    // <TimelineGraphAutoZoom>). setChecked below fires toggled, whose handler
    // writes the same value straight back into editorSequence(), so this is a
    // round-trip no-op and does not mark anything dirty.
    if (m_comp && m_valueGraphButton && m_graphAutoZoomButton) {
        const composition::EditorSequence& seq = m_comp->editorSequence();
        m_valueGraphButton->setChecked(seq.timelineValueGraph);
        m_graphAutoZoomButton->setChecked(seq.timelineGraphAutoZoom);
    }

    if (comp) {
        if (restored.timelineZoom > 0) m_canvas->setZoom(restored.timelineZoom);
        else QTimer::singleShot(0, m_canvas, &TimelineCanvas::zoomToFit);
        m_framesDisplay = restored.timelineTimeFormat == 1001;
        setSnapEnabled(restored.timelineSnapMode != 1001);
    }
    rebuildTree();
    updateTimecodeLabel(playhead());
}

void TimelineWidget::setStartPage(QWidget* page)
{
    if (!page || !m_startPage || !m_startPageLayout) return;

    while (QLayoutItem* item = m_startPageLayout->takeAt(0)) {
        if (QWidget* oldPage = item->widget()) oldPage->setParent(nullptr);
        delete item;
    }
    page->setParent(m_startPage);
    m_startPageLayout->addWidget(page);
}

bool TimelineWidget::isStartPageVisible() const
{
    return m_panelPages && m_panelPages->currentWidget() == m_startPage;
}

void TimelineWidget::showStartPage()
{
    if (!m_panelPages || !m_startPage) return;
    m_panelPages->setCurrentWidget(m_startPage);
    if (m_startTabButton) m_startTabButton->setChecked(true);
    if (m_compositionTabs) {
        m_compositionTabs->setProperty("startPageActive", true);
        m_compositionTabs->style()->unpolish(m_compositionTabs);
        m_compositionTabs->style()->polish(m_compositionTabs);
    }
}

void TimelineWidget::showTimelinePage()
{
    if (!m_panelPages) return;
    m_panelPages->setCurrentIndex(0);
    if (m_startTabButton) m_startTabButton->setChecked(false);
    if (m_compositionTabs) {
        m_compositionTabs->setProperty("startPageActive", false);
        m_compositionTabs->style()->unpolish(m_compositionTabs);
        m_compositionTabs->style()->polish(m_compositionTabs);
    }
    if (m_compositionTabs && m_compositionTabs->currentIndex() < 0
        && m_compositionTabs->count() > 0) {
        const QSignalBlocker blocker(m_compositionTabs);
        m_compositionTabs->setCurrentIndex(0);
    }
}

void TimelineWidget::setCompositionTabs(const QStringList& names, int currentIndex)
{
    if (!m_compositionTabs) return;
    const QSignalBlocker blocker(m_compositionTabs);
    while (m_compositionTabs->count() > 0) m_compositionTabs->removeTab(0);
    for (const QString& name : names)
        m_compositionTabs->addTab(name.isEmpty() ? tr("Untitled") : name);
    // The reference shows an X even for the only document; closing that root
    // document returns to Start rather than destroying the project model.
    m_compositionTabs->setTabsClosable(!names.isEmpty());
    if (currentIndex >= 0 && currentIndex < names.size()) {
        m_compositionTabs->setCurrentIndex(currentIndex);
    }
}

void TimelineWidget::selectLayer(int layerIndex)
{
    if (!m_tree || layerIndex < 0 || layerIndex >= m_tree->topLevelItemCount()) {
        if (m_tree) m_tree->setCurrentItem(nullptr);
        if (m_canvas) m_canvas->setSelection(-1, -1);
        return;
    }
    m_tree->setCurrentItem(m_tree->topLevelItem(layerIndex));
    if (m_canvas) m_canvas->setSelection(layerIndex, -1);
}

void TimelineWidget::setMediaManager(std::shared_ptr<media::MediaManager> media)
{
    m_media = std::move(media);
    if (!m_waveforms) m_waveforms = new media::WaveformCache(this);
    if (m_canvas) m_canvas->setWaveformSource(m_media, m_waveforms);
    rebuildTree();
}

void TimelineWidget::setPluginManager(plugin::PluginManager* pluginManager)
{
    m_pluginManager = pluginManager;
    rebuildTree();
}

// Vertical tool palette down the left edge of the Editor, as the reference
// draws it: Select / Hand / Slice / Slip, with a settings button pinned to the
// bottom. Shortcuts follow the reference command table (V, H, C, Y).
QWidget* TimelineWidget::buildToolStrip(QWidget* parent)
{
    auto* strip = new QWidget(parent);
    strip->setObjectName(QStringLiteral("timelineToolStrip"));
    strip->setFixedWidth(23);
    strip->setStyleSheet(QStringLiteral("QWidget#timelineToolStrip { background:#222222; } QToolButton:checked { background:#12b0ff; }"));

    auto* layout = new QVBoxLayout(strip);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->setSpacing(2);

    struct ToolDef
    {
        EditorTool tool;
        const char* icon;
        const char* label;
        const char* objectName;   // reference button name
        QKeySequence shortcut;
    };
    // Order, names and shortcuts are the reference's own, from its command
    // table (category 3, "Editor sequence timeline"): Select 2005 V, Hand 2006
    // H, Slice 2008 C, Slip 2009 Y, Slide 2010 Shift+U, Ripple 2011 R, Roll
    // 2012 E, Rate Stretch 2014 S, Track Select Forwards 2015 A.
    const ToolDef defs[] = {
        {EditorTool::Select, "pointer", QT_TR_NOOP("Select (V)"),
         "toolButtonPointer", QKeySequence(Qt::Key_V)},
        {EditorTool::Hand, "hand", QT_TR_NOOP("Hand / pan (H)"),
         "toolButtonHand", QKeySequence(Qt::Key_H)},
        {EditorTool::Slice, "timeline-slice", QT_TR_NOOP("Slice (C)"),
         "toolButtonSlice", QKeySequence(Qt::Key_C)},
        {EditorTool::Slip, "slip", QT_TR_NOOP("Slip edit (Y)"),
         "toolButtonSlip", QKeySequence(Qt::Key_Y)},
        {EditorTool::Slide, "slide", QT_TR_NOOP("Slide edit (Shift+U)"),
         "toolButtonSlide", QKeySequence(Qt::SHIFT | Qt::Key_U)},
        {EditorTool::Ripple, "ripple", QT_TR_NOOP("Ripple edit (R)"),
         "toolButtonRipple", QKeySequence(Qt::Key_R)},
        {EditorTool::Roll, "roll", QT_TR_NOOP("Roll edit (E)"),
         "toolButtonRoll", QKeySequence(Qt::Key_E)},
        {EditorTool::Stretch, "timeline-stretch", QT_TR_NOOP("Rate stretch (S)"),
         "toolButtonStretch", QKeySequence(Qt::Key_S)},
        {EditorTool::TrackSelect, "track-select-forward",
         QT_TR_NOOP("Track select forwards (A)"), "toolButtonTrackSelect",
         QKeySequence(Qt::Key_A)},
    };

    for (const ToolDef& def : defs) {
        auto* button = new QToolButton(strip);
        const QIcon icon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(def.icon)));
        if (!icon.isNull() && !icon.pixmap(QSize(18, 18)).isNull()) {
            button->setIcon(icon);
            button->setIconSize(QSize(18, 18));
        } else {
            button->setText(QString(QLatin1String(def.label)).left(1));
        }
        button->setToolTip(tr(def.label));
        button->setObjectName(QString::fromLatin1(def.objectName));
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setFixedSize(18, 18);
        connect(button, &QToolButton::clicked, this,
                [this, t = def.tool] { setTool(t); });
        auto* shortcut = new QShortcut(def.shortcut, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, t = def.tool] { setTool(t); });
        m_toolButtons.append(button);
        if (def.tool == EditorTool::Select || def.tool == EditorTool::Hand || def.tool == EditorTool::Slice || def.tool == EditorTool::Stretch)
            layout->addWidget(button);
        else button->hide();
    }
    // CompositionPanelWidget exposes Pointer, Hand, Slice and Stretch.
    // Sequence-only alternatives remain reachable from the Stretch menu.
    auto* alternatives = new QMenu(strip);
    for (int i = 3; i < m_toolButtons.size(); ++i) {
        auto* action = alternatives->addAction(m_toolButtons[i]->icon(), m_toolButtons[i]->toolTip());
        connect(action, &QAction::triggered, this, [this, i] { setTool(static_cast<EditorTool>(i)); });
    }
    auto* stretch = m_toolButtons[int(EditorTool::Stretch)];
    stretch->setMenu(alternatives); stretch->setPopupMode(QToolButton::DelayedPopup);

    // Snap is a toggle rather than one of the exclusive tools, so it is built
    // here but placed at the bottom of the strip further down.
    m_snapButton = new QToolButton(strip);
    const QIcon magnet(QStringLiteral(":/icons/snap.svg"));
    if (!magnet.isNull() && !magnet.pixmap(QSize(18, 18)).isNull()) {
        m_snapButton->setIcon(magnet);
        m_snapButton->setIconSize(QSize(18, 18));
    } else {
        m_snapButton->setText(QStringLiteral("S"));
    }
    m_snapButton->setObjectName("timelineSnap");
    m_snapButton->setCheckable(true);
    m_snapButton->setChecked(m_snap);
    m_snapButton->setAutoRaise(true);
    m_snapButton->setToolTip(tr("Snap (Shift+S)"));
    m_snapButton->setFixedSize(18, 18);
    connect(m_snapButton, &QToolButton::clicked, this,
            [this](bool on) { setSnapEnabled(on); });
    auto* snapShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_S), this);
    snapShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(snapShortcut, &QShortcut::activated, this,
            [this] { setSnapEnabled(!m_snap); });
    layout->addStretch(1);

    auto* settings = new QToolButton(strip);
    const QIcon gear(QStringLiteral(":/icons/settings.svg"));
    if (!gear.isNull() && !gear.pixmap(QSize(16, 16)).isNull()) {
        settings->setIcon(gear);
        settings->setIconSize(QSize(16, 16));
    } else {
        settings->setText(QStringLiteral("..."));
    }
    settings->setObjectName("timelineOptions");
    settings->setToolTip(tr("Display properties for this composite shot."));
    settings->setAutoRaise(true);
    settings->setFixedSize(18, 18);
    connect(settings, &QToolButton::clicked, this, &TimelineWidget::editCompositionProperties);
    layout->addWidget(settings);

    // Snap sits at the very foot of the strip, below the settings button.
    layout->addWidget(m_snapButton);

    updateToolButtons();
    return strip;
}

void TimelineWidget::setTool(EditorTool tool)
{
    if (m_tool == tool) {
        updateToolButtons();
        return;
    }
    m_tool = tool;
    updateToolButtons();
    // The canvas is what acts on the tool. Without this the strip only lit its
    // buttons up and every tool behaved like Select.
    if (m_canvas) {
        m_canvas->setTool(tool);
    }
    emit toolChanged(m_tool);
}

void TimelineWidget::setSnapEnabled(bool enabled)
{
    if (m_snap == enabled) {
        return;
    }
    m_snap = enabled;
    if (m_comp) m_comp->editorSequence().timelineSnapMode = enabled ? 1000 : 1001;
    if (m_snapButton) {
        m_snapButton->setChecked(enabled);
    }
    // Same story as the tool: the flag was read by nobody, so the magnet
    // toggled and nothing ever snapped.
    if (m_canvas) {
        m_canvas->setSnapEnabled(enabled);
    }
    emit snapChanged(enabled);
}

void TimelineWidget::updateToolButtons()
{
    for (int i = 0; i < m_toolButtons.size(); ++i) {
        if (m_toolButtons.at(i))
            m_toolButtons.at(i)->setChecked(i == static_cast<int>(m_tool));
    }
}

// A press inside the icon area of column 0 counts as hitting the round
// keyframe toggle, not as selecting the row.
void TimelineTree::mousePressEvent(QMouseEvent* event)
{
    QTreeWidgetItem* item = itemAt(event->pos());
    if (item && !item->icon(0).isNull()) {
        const QRect row = visualItemRect(item);
        const int iconRight = row.left() + 18;
        if (event->pos().x() >= row.left() && event->pos().x() < iconRight) {
            emit decorationClicked(item);
            return;
        }
    }
    QTreeWidget::mousePressEvent(event);
}

// Reference layer tree: each layer expands into Tracks / Masks / Effects /
// Transform / Behaviors, and each effect into its parameters.
int TimelineWidget::currentFrame() const
{
    if (!m_comp) {
        return 0;
    }
    const int den = m_comp->fpsDenominator() > 0 ? m_comp->fpsDenominator() : 1;
    const double fps = static_cast<double>(m_comp->fpsNumerator()) / den;
    return static_cast<int>(qRound(playhead() * fps));
}

void TimelineWidget::seekToFrame(int frame)
{
    if (!m_comp) {
        return;
    }
    const int den = m_comp->fpsDenominator() > 0 ? m_comp->fpsDenominator() : 1;
    const double fps = static_cast<double>(m_comp->fpsNumerator()) / den;
    if (fps <= 0.0) {
        return;
    }
    const double seconds = frame / fps;
    setPlayheadPosition(seconds);
    emit timeScrubbed(seconds);
}

QVector<int> TimelineWidget::keyFrameLocations() const
{
    QSet<int> keys;
    if (!m_comp) return {};
    auto* row = m_tree->currentItem();
    const bool property = row && (row->data(0, Qt::UserRole + 3).isValid() || row->data(0, Qt::UserRole + 4).isValid());
    if (property) {
        for (auto* curve : const_cast<TimelineWidget*>(this)->selectedCurves())
            for (int frame : curve->locations()) keys.insert(frame);
    } else {
        for (const auto& layer : m_comp->layers()) {
            for (auto p : composition::transformPropertiesFor(layer.dimension))
                for (int axis = 0; axis < composition::axisCount(p, layer.dimension); ++axis)
                    if (auto* curve = layer.transform.curve(p, axis))
                        for (int frame : curve->locations()) keys.insert(frame);
            for (const auto& clip : layer.clips)
                for (const auto& effect : clip.effects)
                    for (const auto& curve : effect.animation)
                        for (int frame : curve.locations()) keys.insert(frame);
        }
    }
    QVector<int> frames(keys.begin(), keys.end()); std::sort(frames.begin(), frames.end()); return frames;
}

composition::KeyFrameList* TimelineWidget::selectedCurve()
{
    QTreeWidgetItem* item = m_tree ? m_tree->currentItem() : nullptr;
    if (!item || !m_comp) {
        return nullptr;
    }
    const QVariant paramData = item->data(0, Qt::UserRole + 3);
    if (!paramData.isValid()) {
        return nullptr; // not a parameter row
    }
    const int layerIndex = item->data(0, Qt::UserRole).toInt();
    const int clipIndex = item->data(0, Qt::UserRole + 1).toInt();
    const int effectIndex = item->data(0, Qt::UserRole + 2).toInt();
    const int paramIndex = paramData.toInt();

    if (layerIndex < 0 || layerIndex >= m_comp->layers().size()) {
        return nullptr;
    }
    composition::Layer& layer = m_comp->layerRef(layerIndex);
    if (clipIndex < 0 || clipIndex >= layer.clips.size()) {
        return nullptr;
    }
    composition::Clip& clip = layer.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size()) {
        return nullptr;
    }
    composition::Effect& effect = clip.effects[effectIndex];
    const auto it = effect.animation.find(paramIndex);
    return it == effect.animation.end() ? nullptr : &(*it);
}

void TimelineWidget::setSelectedKeyFrameType(composition::TemporalType type)
{
    beginModelEdit();
    for (auto* curve : selectedCurves()) {
        if (auto* key = curve->keyAt(currentFrame())) key->temporal = type;
    }
    finishModelEdit(tr("Set keyframe interpolation"));
    updateKeyButtons();
}

// Sizes the header spacer so the keyframe group begins at the track area's
// left edge - the reference lines that group up with its ruler rather than
// letting it follow the New Layer button.
void TimelineWidget::syncHeaderToTree()
{
    // count() matters as much as the pointers: a resize can arrive between the
    // splitter being created and its children being added, and widget(0) is
    // null in that window.
    if (!m_headerSpacer || !m_header || !m_splitter || !m_newLayerButton || !m_toolStrip
        || m_splitter->count() < 1 || !m_splitter->widget(0)) {
        return;
    }
    const int rulerLeft = m_canvas->mapTo(widget(), QPoint()).x();
    const int usedLeft = m_newLayerButton->mapTo(widget(), QPoint(m_newLayerButton->width(), 0)).x();
    // There are two layout gaps between the New Layer button and keyBar:
    // New Layer -> spacer and spacer -> keyBar.  Compute an absolute width;
    // adding the current spacer width here makes every subsequent resize move
    // the keyframe controls farther to the right.
    const int width = qMax(0, rulerLeft - usedLeft - 2 * m_header->spacing());
    if (m_headerSpacer->sizeHint().width() == width) {
        return;
    }
    m_headerSpacer->changeSize(width, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    m_header->invalidate();
    // Apply the new fixed spacer in this event turn.  Waiting for a later
    // polish/layout pass leaves the keyframe strip briefly at the end of the
    // New Layer control after a theme or DPI change, which is visible as a
    // sideways jump and can persist until the next resize.
    m_header->activate();
}

void TimelineWidget::resizeEvent(QResizeEvent* event)
{
    QDockWidget::resizeEvent(event);
    QTimer::singleShot(0, this, &TimelineWidget::syncHeaderToTree);
}

void TimelineWidget::openNewLayerMenu()
{
    if (m_newLayerButton) {
        m_newLayerButton->showMenu();
    }
}

// Hands the value graph the track canvas's time axis. The canvas owns zoom and
// horizontal scroll for both pages, so this runs whenever either moves - and
// once more when the graph becomes visible, because a hidden page gets no
// resize and would otherwise paint the axis it was last given.
void TimelineWidget::syncValueGraphViewport()
{
    if (!m_canvas || !m_valueGraphView) {
        return;
    }
    m_valueGraphView->setViewport(m_canvas->pixelsPerSecond(), m_canvas->hScrollOffset(),
                                  m_canvas->contentWidth());
    if (m_zoomSlider) { const QSignalBlocker block(m_zoomSlider); m_zoomSlider->setValue(qRound(std::log(m_canvas->zoom() / 0.01) / std::log(2000.) * 1000.)); }
    if (m_comp) m_comp->editorSequence().timelineZoom = m_canvas->zoom();
}

void TimelineWidget::refreshKeyFrames()
{
    rebuildTree();
    update();
}

void TimelineWidget::goToPreviousKeyFrame()
{
    const QVector<int> frames = keyFrameLocations();
    const int now = currentFrame();
    int best = -1;
    for (const int f : frames) {
        if (f < now && f > best) {
            best = f;
        }
    }
    if (best >= 0) {
        seekToFrame(best);
    }
}

void TimelineWidget::goToNextKeyFrame()
{
    const QVector<int> frames = keyFrameLocations();
    const int now = currentFrame();
    int best = -1;
    for (const int f : frames) {
        if (f > now && (best < 0 || f < best)) {
            best = f;
        }
    }
    if (best >= 0) {
        seekToFrame(best);
    }
}

void TimelineWidget::toggleSelectedKeyFrame()
{
    toggleKeyFrame(m_tree ? m_tree->currentItem() : nullptr);
}

void TimelineWidget::toggleKeyFrame(QTreeWidgetItem* row)
{
    if (!row || !m_comp || !row->data(0, Qt::UserRole).isValid()) return;
    const int l = row->data(0, Qt::UserRole).toInt();
    if (l < 0 || l >= m_comp->layers().size() || m_comp->layers()[l].locked) return;
    m_tree->setCurrentItem(row);
    const int frame = currentFrame();
    if (row->data(0, Qt::UserRole + 4).isValid()) {
        const auto p = composition::TransformProperty(row->data(0, Qt::UserRole + 4).toInt());
        editLayer(l, tr("Toggle transform keyframe"), [p, frame](composition::Layer& layer) {
            bool remove = false;
            for (int axis = 0; axis < composition::axisCount(p, layer.dimension); ++axis)
                if (auto* curve = layer.transform.curve(p, axis)) remove |= curve->contains(frame);
            for (int axis = 0; axis < composition::axisCount(p, layer.dimension); ++axis) {
                auto* curve = layer.transform.curve(p, axis); if (!curve) continue;
                const double v = p == composition::TransformProperty::Opacity ? layer.transform.opacityAt(frame, layer.opacity) * 100.
                    : layer.transform.valueAt(p, axis, frame);
                if (remove) { curve->removeAt(frame); if (curve->isEmpty()) composition::setLayerTransformValue(layer, p, axis, v); }
                else { if (curve->isEmpty()) curve->setDefaultValue(v); curve->set(frame, v); }
            }
        });
        return;
    }
    if (!row->data(0, Qt::UserRole + 3).isValid()) return;
    const int c = row->data(0, Qt::UserRole + 1).toInt(), e = row->data(0, Qt::UserRole + 2).toInt(), p = row->data(0, Qt::UserRole + 3).toInt();
    editLayer(l, tr("Toggle parameter keyframe"), [=](composition::Layer& layer) {
        auto& effect = layer.clips[c].effects[e];
        const auto spec = m_pluginManager ? m_pluginManager->spec(effect.pluginId) : plugin::EffectSpec{};
        const auto param = spec.parameters.value(p);
        QVariant value = effect.parameterAt(p, frame);
        if (!value.isValid()) value = param.defaultValue;
        if (param.type == "int") value = value.toInt();
        else if (param.type == "double" || param.type == "float") value = value.toDouble();
        auto& curve = effect.animation[p];
        if (curve.contains(frame)) {
            curve.removeAt(frame);
            if (curve.isEmpty()) {
                if (effect.parameterValues.size() <= p) effect.parameterValues.resize(p + 1);
                effect.parameterValues[p] = value.toString(); effect.animation.remove(p);
            }
        } else {
            if (curve.isEmpty()) { curve.setDefaultValue(value); curve.setCanInterpolate(param.type == "int" || param.type == "double" || param.type == "float"); }
            curve.set(frame, value);
        }
    });
}

// Maps the tree's visible rows onto track-area lanes.
void TimelineWidget::syncLanes()
{
    if (!m_tree || !m_canvas) {
        return;
    }
    QVector<TimelineLane> lanes;
    // Which lane the tree's current row became: the value graph edits that
    // property, and lanes come out in the same order as the visible rows.
    int selectedLane = -1;
    const QTreeWidgetItem* current = m_tree->currentItem();
    QTreeWidgetItemIterator it(m_tree, QTreeWidgetItemIterator::NotHidden);
    for (; *it; ++it) {
        QTreeWidgetItem* item = *it;
        const QRect r = m_tree->visualItemRect(item);
        if (r.isEmpty()) {
            continue;   // collapsed away or scrolled out
        }
        TimelineLane lane;
        // visualItemRect is viewport-relative; the canvas draws below its
        // ruler, and the tree's viewport already starts at that same offset.
        lane.y = r.y() + kRulerHeight;
        lane.height = r.height();
        const bool isLayerRow = item->parent() == nullptr;
        lane.layerIndex = isLayerRow ? item->data(0, Qt::UserRole).toInt() : -1;
        const QVariant transformProp = item->data(0, Qt::UserRole + 4);
        if (!isLayerRow && transformProp.isValid()) {
            lane.layerIndex = item->data(0, Qt::UserRole).toInt();
            lane.transformProp = transformProp.toInt();
        }
        const QVariant param = item->data(0, Qt::UserRole + 3);
        if (!isLayerRow && param.isValid()) {
            lane.layerIndex = item->data(0, Qt::UserRole).toInt();
            lane.clipIndex = item->data(0, Qt::UserRole + 1).toInt();
            lane.effectIndex = item->data(0, Qt::UserRole + 2).toInt();
            lane.parameterIndex = param.toInt();
        }
        if (item == current) {
            selectedLane = lanes.size();
        }
        lanes.append(lane);
    }
    m_canvas->setLanes(lanes);
    m_valueGraphView->setLanes(lanes);
    m_valueGraphView->setSelectedLane(selectedLane);
}

void TimelineWidget::setPlayheadPosition(double timeSeconds)
{
    m_canvas->setPlayheadPosition(timeSeconds);
    m_valueGraphView->setPlayheadPosition(timeSeconds);
    if (!m_timecode->hasFocus()) updateTimecodeLabel(timeSeconds);
    refreshValues();
}

// HH:MM:SS:FF at the composition's frame rate, matching the viewer overlay.
void TimelineWidget::updateTimecodeLabel(double timeSeconds)
{
    if (!m_timecode) {
        return;
    }
    if (timeSeconds < 0.0) {
        timeSeconds = 0.0;
    }
    int fps = 30;
    if (m_comp && m_comp->fpsDenominator() > 0 && m_comp->fpsNumerator() > 0) {
        fps = qMax(1, qRound(static_cast<double>(m_comp->fpsNumerator())
                             / m_comp->fpsDenominator()));
    }
    const double actualFps = m_comp ? double(m_comp->fpsNumerator()) / qMax(1, m_comp->fpsDenominator()) : fps;
    const int totalFrames = qRound(timeSeconds * actualFps);
    const int ff = totalFrames % fps;
    const int totalSeconds = totalFrames / fps;
    if (m_framesDisplay) { m_timecode->setText(QString::number(currentFrame())); return; }
    m_timecode->setText(tr("%1:%2:%3:%4")
                            .arg(totalSeconds / 3600, 2, 10, QLatin1Char('0'))
                            .arg((totalSeconds % 3600) / 60, 2, 10, QLatin1Char('0'))
                            .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
                            .arg(ff, 2, 10, QLatin1Char('0')));
}

double TimelineWidget::playhead() const
{
    return m_canvas ? m_canvas->playhead() : 0.0;
}

double TimelineWidget::pixelsPerSecond() const
{
    return m_canvas ? m_canvas->pixelsPerSecond() : kPixelsPerSecond;
}

double TimelineWidget::zoom() const
{
    return m_canvas ? m_canvas->zoom() : 1.0;
}

void TimelineWidget::setZoom(double factor)
{
    if (m_canvas) {
        m_canvas->setZoom(factor);
    }
}

void TimelineWidget::zoomIn()
{
    if (m_canvas) {
        m_canvas->setZoom(m_canvas->zoom() * 1.5);
    }
}

void TimelineWidget::zoomOut()
{
    if (m_canvas) {
        m_canvas->setZoom(m_canvas->zoom() / 1.5);
    }
}

} // namespace ui
} // namespace openvegas
