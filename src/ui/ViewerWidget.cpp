#define _USE_MATH_DEFINES
#include "ui/ViewerWidget.h"
#include "ui/Theme.h"

#include <QAction>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QCursor>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QIcon>
#include <QActionGroup>
#include <QMenu>
#include <QPolygonF>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtGlobal>

#include <cmath>

#include "app/Settings.h"
#include "ui/ViewScaleButton.h"

namespace openvegas {
namespace ui {

namespace {
// The reference strip is 24 px wide. Its 22 px square buttons sit one pixel
// from either edge, so checked/hover backgrounds fill the rail and every glyph
// shares the same vertical axis.
constexpr int kToolStripWidth = 24;
constexpr int kToolButtonExtent = 22;
constexpr int kToolIconExtent = 16;
} // namespace

namespace {

const QColor kActiveBorder(0, 122, 204);    // reference accent #007ACC
const QColor kInactiveBorder(51, 51, 51);   // reference $fill-panel-plus1

// Checkerboard cell, the reference's "checkerboardSize" viewer property
// (141324800, read by FUN_140928760).
constexpr qreal kCheckerboardSize = 16.0;

// The reference draws its checkerboard in a fragment shader (1412c0af0) that
// takes one colour and derives the other from it:
//     color = isColoredSquare ? backgroundColor
//                             : mix(backgroundColor, vec4(1,1,1,1), 0.15);
// so the pattern is always the background tinted 15% toward white, not the
// fixed grey pair this port used to draw - which is why its checkerboard came
// out light against a dark reference one.
QColor checkerboardTint(const QColor& background)
{
    const auto mix = [](int channel) { return int(channel + (255 - channel) * 0.15 + 0.5); };
    return QColor(mix(background.red()), mix(background.green()), mix(background.blue()));
}

// Reference PaintCheckerboard (ColorPainting) equivalent: two-tone
// checkerboard tiles drawn with QPainter.
void paintCheckerboard(QPainter& painter, const QRectF& rect, const QColor& background)
{
    const QColor plain = background;
    const QColor lit = checkerboardTint(background);
    const qreal tile = kCheckerboardSize;
    const int cols = int(std::ceil(rect.width() / tile));
    const int rows = int(std::ceil(rect.height() / tile));
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setClipRect(rect);
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            const bool coloured = ((x + y) & 1) == 0;
            painter.fillRect(QRectF(rect.x() + x * tile, rect.y() + y * tile, tile, tile),
                             coloured ? plain : lit);
        }
    }
    painter.restore();
}

// Icon and label for a shape, keyed the way the reference names its mask tools.
// Regular n-gon inscribed in `bounds`, first vertex at the top. Used for the
// polygon and star mask previews so each shape draws as itself rather than as
// its bounding box.
QPolygonF regularPolygon(const QRectF& bounds, int sides, double rotationRadians)
{
    QPolygonF poly;
    const QPointF centre = bounds.center();
    const double rx = bounds.width() / 2.0;
    const double ry = bounds.height() / 2.0;
    for (int i = 0; i < sides; ++i) {
        const double a = rotationRadians - M_PI_2 + (2.0 * M_PI * i) / sides;
        poly << QPointF(centre.x() + rx * std::cos(a), centre.y() + ry * std::sin(a));
    }
    return poly;
}

// Star with `points` tips; `innerRatio` is the inner radius as a fraction of
// the outer one.
QPolygonF starPolygon(const QRectF& bounds, int points, double innerRatio)
{
    QPolygonF poly;
    const QPointF centre = bounds.center();
    const double rx = bounds.width() / 2.0;
    const double ry = bounds.height() / 2.0;
    for (int i = 0; i < points * 2; ++i) {
        const double a = -M_PI_2 + (M_PI * i) / points;
        const double f = (i % 2 == 0) ? 1.0 : innerRatio;
        poly << QPointF(centre.x() + rx * f * std::cos(a), centre.y() + ry * f * std::sin(a));
    }
    return poly;
}

struct ShapeDef
{
    ViewerWidget::ViewerTool tool;
    const char* icon;
    const char* label;
};
const ShapeDef kShapes[] = {
    {ViewerWidget::ViewerTool::Rectangle,   "mask-square",      QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Rectangle Mask Tool")},
    {ViewerWidget::ViewerTool::RoundedRect, "mask-rounded-rect", QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Rounded Rect Mask Tool")},
    {ViewerWidget::ViewerTool::Ellipse,     "mask-circle",      QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Ellipse Mask Tool")},
    {ViewerWidget::ViewerTool::Polygon,     "mask-polygon",     QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Polygon Mask Tool")},
    {ViewerWidget::ViewerTool::Star,        "mask-star",        QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Star Mask Tool")},
};

const ShapeDef* shapeDefFor(ViewerWidget::ViewerTool tool)
{
    for (const ShapeDef& def : kShapes) {
        if (def.tool == tool) {
            return &def;
        }
    }
    return nullptr;
}

// Reference `biff::ui::common::FullScreenPreviewWidget` (RTTI 1414e74f0),
// reached by command 5026 "Toggle Full Screen Preview" (Ctrl+Shift+F). It is a
// window of its own showing nothing but the frame, letter-boxed on the
// background colour; Escape or the same shortcut puts it away. No signals of
// its own, so it needs no moc and can live here beside the widget that owns it.
class FullScreenPreview : public QWidget
{
public:
    explicit FullScreenPreview(QWidget* owner)
        : QWidget(nullptr, Qt::Window)
        , m_owner(owner)
    {
        setObjectName(QStringLiteral("FullScreenPreviewWidget"));
        setWindowTitle(QObject::tr("Full Screen Preview"));
        setFocusPolicy(Qt::StrongFocus);
        connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (!QSettings().value(QStringLiteral("Options/HideFullScreenPreview"), false).toBool()) return;
            if (state != Qt::ApplicationActive) hide();
            else showFullScreen();
        });
    }

    void setFrame(const QImage& frame, const QColor& background)
    {
        m_frame = frame;
        m_background = background;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), m_background);
        if (m_frame.isNull()) {
            return;
        }
        const QSize scaled = m_frame.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - scaled.width()) / 2,
                                  (height() - scaled.height()) / 2),
                           scaled);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target, m_frame);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape
            || (event->key() == Qt::Key_F
                && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier))) {
            close();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void closeEvent(QCloseEvent* event) override
    {
        // Let the viewer put its menu item back in step however the window was
        // dismissed - Escape, the shortcut or the window manager.
        if (m_owner) {
            QMetaObject::invokeMethod(m_owner, "setFullScreenPreview", Qt::QueuedConnection,
                                      Q_ARG(bool, false));
        }
        QWidget::closeEvent(event);
    }

private:
    QWidget* m_owner = nullptr;
    QImage m_frame;
    QColor m_background{0, 0, 0};
};

} // namespace

ViewerWidget::ViewerWidget(QWidget* parent)
    : QWidget(parent)
{
    // Reference name for the viewer canvas (141320d78, xref FUN_1408b2d40).
    setObjectName(QStringLiteral("graphicsViewViewer"));
    setMinimumSize(320, 180);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    // View options are stored settings in the reference, not per-session state:
    // the Options menu and the Preferences Viewer page write the same keys, so
    // the viewer starts from them rather than from hard-coded defaults.
    m_checkerboard = app::Settings::showCheckerboard2D();
    const QColor stored(app::Settings::viewerBackgroundColor());
    if (stored.isValid()) {
        m_backgroundColor = stored;
    }
    m_showMotionPath = app::Settings::showMotionPath();
    m_playbackResolution = downsampleFromLabel(app::Settings::playbackDownsampleMode());
    m_pausedResolution = downsampleFromLabel(app::Settings::pausedDownsampleMode());
    m_playbackQuality = qualityFromLabel(app::Settings::playbackQualityProfile());
    m_pausedQuality = qualityFromLabel(app::Settings::pausedQualityProfile());

    // Command 5026, "Toggle Full Screen Preview" (Ctrl+Shift+F). Built here
    // rather than in the Options menu so there is exactly one of it: the window
    // puts the same action in its Window menu, and a second QAction on the same
    // key would leave both dead.
    m_fullScreenAction = new QAction(tr("Full Screen Preview"), this);
    m_fullScreenAction->setObjectName(QStringLiteral("actionFullScreenPreview"));
    m_fullScreenAction->setCheckable(true);
    m_fullScreenAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    m_fullScreenAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_fullScreenAction, &QAction::triggered, this, &ViewerWidget::setFullScreenPreview);
    addAction(m_fullScreenAction);

    buildToolStrip();
}

void ViewerWidget::setFrame(const QImage& image)
{
    m_frame = image;
    // static_cast, not qobject_cast: FullScreenPreview declares no Q_OBJECT of
    // its own, so a qobject_cast would match its base and accept any QWidget.
    // The pointer is only ever set from this class.
    if (m_fullScreenPreview) {
        static_cast<FullScreenPreview*>(m_fullScreenPreview)->setFrame(m_frame, m_backgroundColor);
    }
    update();
}

int ViewerWidget::viewCount() const
{
    switch (m_layout) {
    case ViewerLayout::Single: return 1;
    case ViewerLayout::Row: return 2;
    case ViewerLayout::TwoOverOne: return 3;
    case ViewerLayout::Four: return 4;
    case ViewerLayout::RowFour: return 4;
    }
    return 1;
}

QPointF ViewerWidget::canvasDeltaForViewDelta(const QPointF& viewDelta) const
{
    const QRect imageRect = imageRectForView(m_activeView);
    if (imageRect.width() <= 0 || imageRect.height() <= 0 || m_projectSize.isEmpty())
        return viewDelta;
    return QPointF(viewDelta.x() * double(m_projectSize.width()) / imageRect.width(),
                   viewDelta.y() * double(m_projectSize.height()) / imageRect.height());
}

void ViewerWidget::setLayout(ViewerLayout layout)
{
    if (m_layout == layout)
        return;
    m_layout = layout;
    if (m_activeView >= viewCount())
        m_activeView = 0;
    emit layoutChanged(m_layout);
    update();
}

void ViewerWidget::setActiveView(int index)
{
    if (index < 0 || index >= viewCount() || index == m_activeView)
        return;
    m_activeView = index;
    emit activeViewChanged(m_activeView);
    update();
}

void ViewerWidget::setCheckerboardEnabled(bool enabled)
{
    if (m_checkerboard == enabled)
        return;
    m_checkerboard = enabled;
    // The toggle now lives in the Options menu, which keeps its own checked
    // state; the signal is what carries the change back to it.
    app::Settings::setShowCheckerboard2D(enabled);
    emit checkerboardChanged(enabled);
    update();
}

void ViewerWidget::setBackgroundColor(const QColor& color)
{
    if (!color.isValid() || color == m_backgroundColor) {
        return;
    }
    m_backgroundColor = color;
    app::Settings::setViewerBackgroundColor(color.name());
    if (m_fullScreenPreview) {
        static_cast<FullScreenPreview*>(m_fullScreenPreview)->setFrame(m_frame, m_backgroundColor);
    }
    update();
}

void ViewerWidget::setColorChannel(ColorChannel channel)
{
    if (m_colorChannel == channel) {
        return;
    }
    m_colorChannel = channel;
    emit colorChannelChanged(channel);
    update();
}

void ViewerWidget::setShowMotionPath(bool on)
{
    if (m_showMotionPath == on) {
        return;
    }
    m_showMotionPath = on;
    app::Settings::setShowMotionPath(on);
    emit showMotionPathChanged(on);
    update();
}

void ViewerWidget::setMotionPath(const QVector<QPointF>& path, const QVector<QPointF>& keys)
{
    m_motionPath = path;
    m_motionPathKeys = keys;
    if (m_showMotionPath) {
        update();
    }
}

QString ViewerWidget::downsampleLabel(Downsample mode)
{
    switch (mode) {
    case Downsample::Antialiased: return QStringLiteral("Antialiased");
    case Downsample::Full:        return QStringLiteral("Full");
    case Downsample::Half:        return QStringLiteral("1/2");
    case Downsample::Quarter:     return QStringLiteral("1/4");
    }
    return QStringLiteral("Full");
}

ViewerWidget::Downsample ViewerWidget::downsampleFromLabel(const QString& label)
{
    if (label == QLatin1String("Antialiased")) return Downsample::Antialiased;
    if (label == QLatin1String("1/2"))         return Downsample::Half;
    if (label == QLatin1String("1/4"))         return Downsample::Quarter;
    return Downsample::Full;
}

QString ViewerWidget::qualityLabel(QualityProfile profile)
{
    switch (profile) {
    case QualityProfile::Final:   return QStringLiteral("Final");
    case QualityProfile::Draft:   return QStringLiteral("Draft");
    case QualityProfile::Quick:   return QStringLiteral("Quick");
    case QualityProfile::Fastest: return QStringLiteral("Fastest");
    }
    return QStringLiteral("Final");
}

ViewerWidget::QualityProfile ViewerWidget::qualityFromLabel(const QString& label)
{
    if (label == QLatin1String("Draft"))   return QualityProfile::Draft;
    if (label == QLatin1String("Quick"))   return QualityProfile::Quick;
    if (label == QLatin1String("Fastest")) return QualityProfile::Fastest;
    return QualityProfile::Final;
}

double ViewerWidget::renderScale(bool playing) const
{
    switch (playing ? m_playbackResolution : m_pausedResolution) {
    // Antialiased renders above the frame size and lets the downscale to the
    // view do the smoothing - the only way a software rasteriser antialiases
    // an already-composited picture.
    case Downsample::Antialiased: return 2.0;
    case Downsample::Full:        return 1.0;
    case Downsample::Half:        return 0.5;
    case Downsample::Quarter:     return 0.25;
    }
    return 1.0;
}

bool ViewerWidget::renderEffects(bool playing) const
{
    // The reference's profiles are lists of switches (profile-shadows,
    // profile-motion-blur, profile-reflections, profile-depth-of-field,
    // profile-2d-effects-rendering). Of those this port has only the last, so a
    // profile here decides exactly one thing: whether the layer effects run.
    // Final and Draft keep them; Quick and Fastest drop them, which is what the
    // reference's own "Effects disabled by quality profile." notice is for.
    switch (playing ? m_playbackQuality : m_pausedQuality) {
    case QualityProfile::Final:
    case QualityProfile::Draft:
        return true;
    case QualityProfile::Quick:
    case QualityProfile::Fastest:
        return false;
    }
    return true;
}

void ViewerWidget::setPlaybackResolution(Downsample mode)
{
    if (m_playbackResolution == mode) {
        return;
    }
    m_playbackResolution = mode;
    app::Settings::setPlaybackDownsampleMode(downsampleLabel(mode));
    updateQualityButtonText();
    emit renderQualityChanged();
}

void ViewerWidget::setPausedResolution(Downsample mode)
{
    if (m_pausedResolution == mode) {
        return;
    }
    m_pausedResolution = mode;
    app::Settings::setPausedDownsampleMode(downsampleLabel(mode));
    updateQualityButtonText();
    emit renderQualityChanged();
}

void ViewerWidget::setPlaybackQuality(QualityProfile profile)
{
    if (m_playbackQuality == profile) {
        return;
    }
    m_playbackQuality = profile;
    app::Settings::setPlaybackQualityProfile(qualityLabel(profile));
    emit renderQualityChanged();
}

void ViewerWidget::setPausedQuality(QualityProfile profile)
{
    if (m_pausedQuality == profile) {
        return;
    }
    m_pausedQuality = profile;
    app::Settings::setPausedQualityProfile(qualityLabel(profile));
    emit renderQualityChanged();
}

void ViewerWidget::setPlaying(bool playing)
{
    if (m_playing == playing) {
        return;
    }
    m_playing = playing;
    updateQualityButtonText();
}

void ViewerWidget::updateQualityButtonText()
{
    if (!m_qualityButton) {
        return;
    }
    // The face shows the resolution actually in force, which is the playback
    // one only while the transport is running - the reference keeps a separate
    // paused setting precisely so the two differ.
    m_qualityButton->setText(
        downsampleLabel(m_playing ? m_playbackResolution : m_pausedResolution));
}

bool ViewerWidget::fullScreenPreviewActive() const
{
    return m_fullScreenPreview != nullptr && m_fullScreenPreview->isVisible();
}

void ViewerWidget::toggleFullScreenPreview()
{
    setFullScreenPreview(!fullScreenPreviewActive());
}

void ViewerWidget::setFullScreenPreview(bool on)
{
    if (!on) {
        if (m_fullScreenPreview) {
            // Cleared first: the window's own closeEvent posts a call back here
            // to keep the menu item in step, and by then there must be nothing
            // left to close or it would recurse.
            QWidget* preview = m_fullScreenPreview;
            m_fullScreenPreview = nullptr;
            preview->close();
            preview->deleteLater();
        }
        return;
    }
    if (m_fullScreenPreview) {
        return;
    }
    auto* preview = new FullScreenPreview(this);
    preview->setAttribute(Qt::WA_DeleteOnClose, false);
    preview->setFrame(m_frame, m_backgroundColor);
    m_fullScreenPreview = preview;
    preview->showFullScreen();
    preview->setFocus();
}

void ViewerWidget::setZoomPercent(int percent)
{
    if (m_zoomPercent == percent)
        return;
    m_zoomPercent = percent;
    emit zoomPercentChanged(percent);
    update();
}

void ViewerWidget::zoomIn()
{
    setZoomPercent((m_zoomPercent < 0) ? 50 : qMin(200, m_zoomPercent + 25));
}

void ViewerWidget::zoomOut()
{
    if (m_zoomPercent < 0)
        return;
    setZoomPercent(qMax(12, m_zoomPercent - 25));
}

int ViewerWidget::zoomPercent() const
{
    return m_zoomPercent;
}

void ViewerWidget::setProjectSize(const QSize& size)
{
    if (size.isEmpty() || size == m_projectSize)
        return;
    m_projectSize = size;
    update();
}

void ViewerWidget::setFrameRate(int numerator, int denominator)
{
    m_fpsNumerator = numerator > 0 ? numerator : 30;
    m_fpsDenominator = denominator > 0 ? denominator : 1;
    update();
}

void ViewerWidget::setTimecode(double seconds)
{
    m_timecode = seconds;
    m_hasTimecode = true;
    update();
}

QString ViewerWidget::formatTimecode(double seconds) const
{
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const double fps = static_cast<double>(m_fpsNumerator) / m_fpsDenominator;
    const int frames = static_cast<int>(seconds * fps + 0.5);
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

void ViewerWidget::setTool(ViewerTool tool)
{
    if (m_tool == tool)
        return;
    m_tool = tool;
    m_handPanning = false;
    m_rubberBanding = false;
    updateToolButtons();
    emit toolChanged(tool);
    update();
}

QRect ViewerWidget::toolStripRect() const
{
    // The playback/options row is a separate widget below ViewerWidget, so the
    // palette occupies the complete canvas height without a stray two-pixel
    // gap at its foot.
    return QRect(0, 0, kToolStripWidth, height());
}

int ViewerWidget::viewAt(const QPoint& widgetPos) const
{
    const int count = viewCount();
    for (int i = 0; i < count; ++i) {
        if (viewRect(i).contains(widgetPos))
            return i;
    }
    return -1;
}

QRect ViewerWidget::imageRectForView(int index) const
{
    const QRect vp = viewRect(index);
    if (vp.isEmpty() || m_frame.isNull())
        return QRect();

    double fitScale = qMin(double(vp.width()) / m_frame.width(),
                           double(vp.height()) / m_frame.height());
    const double scale = (m_zoomPercent < 0)
                             ? fitScale
                             : fitScale * (double(m_zoomPercent) / 100.0);
    const QSize scaled(int(m_frame.width() * scale), int(m_frame.height() * scale));
    return QRect(QPoint(vp.x() + (vp.width() - scaled.width()) / 2,
                        vp.y() + (vp.height() - scaled.height()) / 2),
                 scaled);
}

QPoint ViewerWidget::imagePos(int viewIndex, const QPoint& widgetPos) const
{
    const QRect fit = imageRectForView(viewIndex);
    if (fit.isNull() || fit.width() <= 0 || fit.height() <= 0)
        return QPoint();
    // Composition pixels, not frame pixels: a preview rendered at 1/2 or at 2x
    // has a frame that is not the project's size, and the number wanted here -
    // the one a layer position or a mask point is measured in - is always the
    // project's.
    const QSize space = m_projectSize.isEmpty() ? m_frame.size() : m_projectSize;
    // Active-view pan offset moves the fitted rectangle; map back so a drag
    // under the mouse keeps the grabbed image point underneath.
    const QPoint pos = (viewIndex == m_activeView) ? widgetPos - m_panOffset : widgetPos;
    return QPoint(int((pos.x() - fit.x()) * double(space.width()) / fit.width()),
                  int((pos.y() - fit.y()) * double(space.height()) / fit.height()));
}

void ViewerWidget::updateToolButtons()
{
    // Each button carries the tool it selects, because the strip no longer has
    // one button per enumerator - the five shapes share a single button - so the
    // old "index equals enum value" rule would light up the wrong one.
    for (QToolButton* button : m_toolButtons) {
        const int tool = button->property("viewerTool").toInt();
        const bool isShapeButton = button == m_shapeButton;
        button->setChecked(isShapeButton ? isShapeTool(m_tool) : tool == int(m_tool));
    }
}

void ViewerWidget::buildToolStrip()
{
    m_toolStrip = new QWidget(this);
    m_toolStrip->setObjectName(QStringLiteral("viewer-tool-strip"));
    auto* layout = new QVBoxLayout(m_toolStrip);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    struct ToolDef {
        ViewerTool tool;
        const char* icon;
        const char* tip;
        const char* objectName;   // reference button name
        Qt::Key key;
    };
    // The shapes live behind the shape button's flyout rather than each having
    // a button, which is why only these three are laid out in the loop. The
    // strip's order is the reference's own, by the address of each button name
    // in its builder FUN_140911470: Pointer, Hand, Text, MaskShape, MaskPen,
    // CameraOrbit, VectorPath. Orbit therefore comes after the pen, not before
    // the text tool as it used to.
    const ToolDef defs[] = {
        {ViewerTool::Select,   "pointer", QT_TR_NOOP("Select (V)"),
         "toolButtonPointer",  Qt::Key_V},
        {ViewerTool::Hand,     "hand",   QT_TR_NOOP("Hand / pan (H)"),
         "toolButtonHand",     Qt::Key_H},
        {ViewerTool::Text,     "text-layer",   QT_TR_NOOP("Text (T)"),
         "toolButtonText",     Qt::Key_T},
    };

    const auto styleButton = [](QToolButton* button) {
        button->setAutoRaise(true);
        button->setCheckable(true);
        button->setFixedSize(kToolButtonExtent, kToolButtonExtent);
        button->setIconSize(QSize(kToolIconExtent, kToolIconExtent));
    };

    for (const ToolDef& def : defs) {
        auto* button = new QToolButton(m_toolStrip);
        const QIcon icon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(def.icon)));
        if (!icon.isNull() && !icon.pixmap(QSize(kToolIconExtent, kToolIconExtent)).isNull()) {
            button->setIcon(icon);
        } else {
            button->setText(tr(def.tip).left(1));
        }
        styleButton(button);
        button->setToolTip(tr(def.tip));
        button->setObjectName(QString::fromLatin1(def.objectName));
        button->setProperty("viewerTool", static_cast<int>(def.tool));
        connect(button, &QToolButton::clicked, this, [this, tool = def.tool] { setTool(tool); });
        auto* shortcut = new QShortcut(QKeySequence(def.key), this);
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, tool = def.tool] { setTool(tool); });
        m_toolButtons.append(button);
        layout->addWidget(button, 0, Qt::AlignHCenter);
    }

    // Shape button with its flyout. Clicking it uses the shape last picked, the
    // arrow opens the list - the reference behaves the same way, which is why
    // only one shape button is visible at a time.
    m_shapeButton = new QToolButton(m_toolStrip);
    styleButton(m_shapeButton);
    m_shapeButton->setObjectName(QStringLiteral("toolButtonMaskShape"));
    // DelayedPopup, not MenuButtonPopup: the strip button is compact, and a split
    // button would spend half of that on the arrow and leave the icon cramped.
    // A plain click therefore uses the current shape, press-and-hold opens the
    // flyout, and the style still draws its small corner indicator. Right-click
    // opens it at once, which is how a palette flyout is usually reached.
    m_shapeButton->setPopupMode(QToolButton::DelayedPopup);
    m_shapeButton->setContextMenuPolicy(Qt::CustomContextMenu);
    m_shapeButton->setProperty("viewerTool", static_cast<int>(m_shapeTool));
    auto* shapeMenu = new QMenu(m_shapeButton);
    for (const ShapeDef& def : kShapes) {
        const QIcon icon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(def.icon)));
        QAction* action = shapeMenu->addAction(icon, tr(def.label));
        action->setData(static_cast<int>(def.tool));
        connect(action, &QAction::triggered, this, [this, tool = def.tool] {
            m_shapeTool = tool;
            m_shapeButton->setProperty("viewerTool", static_cast<int>(tool));
            updateShapeButton();
            setTool(tool);
        });
    }
    m_shapeButton->setMenu(shapeMenu);
    connect(m_shapeButton, &QToolButton::customContextMenuRequested, this,
            [this, shapeMenu](const QPoint& pos) {
                shapeMenu->popup(m_shapeButton->mapToGlobal(pos));
            });
    connect(m_shapeButton, &QToolButton::clicked, this, [this] { setTool(m_shapeTool); });
    updateShapeButton();
    m_toolButtons.append(m_shapeButton);
    layout->addWidget(m_shapeButton, 0, Qt::AlignHCenter);

    // Freehand Mask Tool - the pen.
    auto* penButton = new QToolButton(m_toolStrip);
    const QIcon penIcon(QStringLiteral(":/icons/mask-add-point.svg"));
    if (!penIcon.isNull() && !penIcon.pixmap(QSize(kToolIconExtent, kToolIconExtent)).isNull()) {
        penButton->setIcon(penIcon);
    } else {
        penButton->setText(QStringLiteral("F"));
    }
    styleButton(penButton);
    penButton->setToolTip(tr("Freehand Mask Tool (F)"));
    penButton->setObjectName(QStringLiteral("toolButtonMaskPen"));
    penButton->setProperty("viewerTool", static_cast<int>(ViewerTool::Freehand));
    connect(penButton, &QToolButton::clicked, this, [this] { setTool(ViewerTool::Freehand); });
    auto* penShortcut = new QShortcut(QKeySequence(Qt::Key_F), this);
    penShortcut->setContext(Qt::WidgetShortcut);
    connect(penShortcut, &QShortcut::activated, this, [this] { setTool(ViewerTool::Freehand); });
    m_toolButtons.append(penButton);
    layout->addWidget(penButton, 0, Qt::AlignHCenter);

    // Camera Orbit. Named and keyed as the reference has it - toolButtonCameraOrbit
    // (141323bd8) on B - rather than the toolButtonOrbit / O this port invented.
    auto* orbitButton = new QToolButton(m_toolStrip);
    const QIcon orbitIcon(QStringLiteral(":/icons/camera-orbit.svg"));
    if (!orbitIcon.isNull() && !orbitIcon.pixmap(QSize(kToolIconExtent, kToolIconExtent)).isNull()) {
        orbitButton->setIcon(orbitIcon);
    } else {
        orbitButton->setText(QStringLiteral("B"));
    }
    styleButton(orbitButton);
    orbitButton->setToolTip(tr("Orbit Tool (B)"));
    orbitButton->setObjectName(QStringLiteral("toolButtonCameraOrbit"));
    orbitButton->setProperty("viewerTool", static_cast<int>(ViewerTool::Orbit));
    connect(orbitButton, &QToolButton::clicked, this, [this] { setTool(ViewerTool::Orbit); });
    auto* orbitShortcut = new QShortcut(QKeySequence(Qt::Key_B), this);
    orbitShortcut->setContext(Qt::WidgetShortcut);
    connect(orbitShortcut, &QShortcut::activated, this, [this] { setTool(ViewerTool::Orbit); });
    m_toolButtons.append(orbitButton);
    layout->addWidget(orbitButton, 0, Qt::AlignHCenter);

    layout->addStretch(1);

    // toolButtonVectorPath sits alone at the bottom of the reference strip.
    auto* pathButton = new QToolButton(m_toolStrip);
    const QIcon pathIcon(QStringLiteral(":/icons/freehand-path.svg"));
    if (!pathIcon.isNull() && !pathIcon.pixmap(QSize(kToolIconExtent, kToolIconExtent)).isNull()) {
        pathButton->setIcon(pathIcon);
    } else {
        pathButton->setText(QStringLiteral("P"));
    }
    styleButton(pathButton);
    // No shortcut. The reference's shipped key map has this tool as command
    // 5014, "Freehand path tool", with an empty <shortcut value="">; the "P"
    // this port used to bind was invented, and P is already the reference's
    // "Set In & Out Points to content" (command 1102).
    pathButton->setToolTip(tr("Freehand Path Tool"));
    pathButton->setObjectName(QStringLiteral("toolButtonVectorPath"));
    pathButton->setProperty("viewerTool", static_cast<int>(ViewerTool::VectorPath));
    connect(pathButton, &QToolButton::clicked, this,
            [this] { setTool(ViewerTool::VectorPath); });
    m_toolButtons.append(pathButton);
    layout->addWidget(pathButton, 0, Qt::AlignHCenter);

    updateToolButtons();
    // The application-wide QToolButton rule has padding and a 24 px minimum
    // width intended for regular toolbars. Scope the compact Viewer palette so
    // those metrics cannot shift or crop its icons.
    m_toolStrip->setStyleSheet(QStringLiteral(R"(
        QWidget#viewer-tool-strip {
            background: #1b1b1b;
            border-right: 1px solid #111111;
        }
        QWidget#viewer-tool-strip QToolButton {
            background: transparent;
            border: 0;
            border-radius: 0;
            margin: 0;
            padding: 2px;
            min-width: 0;
            min-height: 0;
        }
        QWidget#viewer-tool-strip QToolButton:hover { background: #3a3a3a; }
        QWidget#viewer-tool-strip QToolButton:checked { background: #12b0ff; }
    )"));
    m_toolStrip->setGeometry(toolStripRect());
    m_toolStrip->raise();
}

bool ViewerWidget::isShapeTool(ViewerTool tool)
{
    switch (tool) {
    case ViewerTool::Rectangle:
    case ViewerTool::RoundedRect:
    case ViewerTool::Ellipse:
    case ViewerTool::Polygon:
    case ViewerTool::Star:
        return true;
    default:
        break;
    }
    return false;
}


void ViewerWidget::setShowMouseCoordinates(bool on)
{
    if (m_showMouseCoordinates == on) {
        return;
    }
    m_showMouseCoordinates = on;
    update();
}

void ViewerWidget::updateShapeButton()
{
    if (!m_shapeButton) {
        return;
    }
    const ShapeDef* def = shapeDefFor(m_shapeTool);
    if (!def) {
        return;
    }
    const QIcon icon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(def->icon)));
    if (!icon.isNull() && !icon.pixmap(QSize(kToolIconExtent, kToolIconExtent)).isNull()) {
        m_shapeButton->setIcon(icon);
        m_shapeButton->setIconSize(QSize(kToolIconExtent, kToolIconExtent));
    }
    // The reference gives the two shapes it keys - Rectangle R and Ellipse E -
    // their letter on the button, so the shortcut is discoverable from the
    // strip rather than only from the flyout.
    QString tip = tr(def->label);
    if (m_shapeTool == ViewerTool::Rectangle) {
        tip = tr("%1 (R)").arg(tip);
    } else if (m_shapeTool == ViewerTool::Ellipse) {
        tip = tr("%1 (E)").arg(tip);
    }
    m_shapeButton->setToolTip(tip);
}

QWidget* ViewerWidget::createViewOptionsBar(QWidget* parent)
{
    auto* bar = new QWidget(parent);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    // The reference's "View" button (toolButtonView, built by its
    // ViewerPlaybackWidget): it switches the viewer's layout. This port had the
    // same menu under an invented name, "Options"; the name and label now match
    // the original.
    auto* options = new QToolButton(bar);
    options->setText(tr("View"));
    options->setObjectName(QStringLiteral("toolButtonView"));
    options->setAutoRaise(true);
    options->setPopupMode(QToolButton::InstantPopup);
    options->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto* menu = new QMenu(options);
    auto* layoutGroup = new QActionGroup(menu);
    layoutGroup->setExclusive(true);
    struct LayoutDef
    {
        ViewerLayout layout;
        const char* label;
    };
    const LayoutDef layouts[] = {
        {ViewerLayout::Single,     QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "1 View")},
        {ViewerLayout::Row,        QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "2 Views")},
        {ViewerLayout::TwoOverOne, QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "2 over 1")},
        {ViewerLayout::Four,       QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "4 Views")},
        {ViewerLayout::RowFour,    QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "4 in a Row")},
    };
    for (const LayoutDef& def : layouts) {
        QAction* action = menu->addAction(tr(def.label));
        action->setCheckable(true);
        action->setChecked(m_layout == def.layout);
        action->setData(static_cast<int>(def.layout));
        layoutGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, l = def.layout] { setLayout(l); });
    }
    // Keep the menu honest when the layout is changed from elsewhere (the
    // Layout panel drives the same property).
    connect(this, &ViewerWidget::layoutChanged, menu, [layoutGroup](ViewerLayout current) {
        const auto actions = layoutGroup->actions();
        for (QAction* action : actions) {
            action->setChecked(action->data().toInt() == static_cast<int>(current));
        }
    });

    options->setMenu(menu);
    layout->addWidget(options);

    // The reference's own order along the bottom-right of the viewer, by the
    // order its ViewerPlaybackWidget builder names them: toolButtonView (the
    // layout switcher above), toolButtonOption, toolButtonPlaybackQuality,
    // toolButtonZoom.
    layout->addWidget(createOptionsButton(bar));
    layout->addWidget(createQualityButton(bar));

    m_scaleButton = new ViewScaleButton(bar);
    m_scaleButton->setZoomPercent(m_zoomPercent);
    connect(m_scaleButton, &ViewScaleButton::zoomPercentChanged, this, &ViewerWidget::setZoomPercent);
    connect(this, &ViewerWidget::zoomPercentChanged, m_scaleButton, &ViewScaleButton::setZoomPercent);
    m_scaleButton->setObjectName(QStringLiteral("toolButtonZoom"));
    m_scaleButton->setMaximumWidth(150);
    layout->addWidget(m_scaleButton);

    return bar;
}

QWidget* ViewerWidget::createOptionsButton(QWidget* parent)
{
    // toolButtonOption (1413238f8, text "Options") and the menu its builder
    // FUN_1408d93d0 fills. The reference order is kept even where an entry has
    // no counterpart here yet, so the items that do exist sit where a user of
    // the original would reach for them.
    auto* button = new QToolButton(parent);
    button->setText(tr("Options"));
    button->setObjectName(QStringLiteral("toolButtonOption"));
    button->setAutoRaise(true);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto* menu = new QMenu(button);

    QAction* motionPath = menu->addAction(tr("Show Motion Path"));
    motionPath->setCheckable(true);
    motionPath->setChecked(m_showMotionPath);
    connect(motionPath, &QAction::toggled, this, &ViewerWidget::setShowMotionPath);
    connect(this, &ViewerWidget::showMotionPathChanged, motionPath, &QAction::setChecked);

    QAction* background = menu->addAction(tr("Background Color"));
    connect(background, &QAction::triggered, this, [this] {
        const QColor picked = interfaceColor(m_backgroundColor, this,
                                                     tr("Viewer Background Color"));
        if (picked.isValid()) {
            setBackgroundColor(picked);
        }
    });

    QAction* checker = menu->addAction(tr("Checkerboard Background"));
    checker->setCheckable(true);
    checker->setChecked(m_checkerboard);
    connect(checker, &QAction::toggled, this, &ViewerWidget::setCheckerboardEnabled);
    connect(this, &ViewerWidget::checkerboardChanged, checker, &QAction::setChecked);

    menu->addSeparator();

    QMenu* channels = menu->addMenu(tr("Color Channels"));
    auto* channelGroup = new QActionGroup(channels);
    channelGroup->setExclusive(true);
    struct ChannelDef
    {
        ColorChannel channel;
        const char* label;
    };
    const ChannelDef channelDefs[] = {
        {ColorChannel::Rgb,         QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "RGB")},
        {ColorChannel::RgbStraight, QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "RGB Straight")},
        {ColorChannel::Red,         QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Red")},
        {ColorChannel::Green,       QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Green")},
        {ColorChannel::Blue,        QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Blue")},
        {ColorChannel::Alpha,       QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Alpha")},
    };
    for (const ChannelDef& def : channelDefs) {
        QAction* action = channels->addAction(tr(def.label));
        action->setCheckable(true);
        action->setChecked(m_colorChannel == def.channel);
        action->setData(static_cast<int>(def.channel));
        channelGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, channel = def.channel] { setColorChannel(channel); });
    }
    connect(this, &ViewerWidget::colorChannelChanged, channels, [channelGroup](ColorChannel now) {
        const auto actions = channelGroup->actions();
        for (QAction* action : actions) {
            action->setChecked(action->data().toInt() == static_cast<int>(now));
        }
    });

    menu->addSeparator();

    // The one action from the constructor, not a second one on the same key.
    menu->addAction(m_fullScreenAction);
    // The window can be dismissed from its own keyboard, so the item is put
    // back in step when the menu is about to be shown rather than trusted to
    // stay where the last click left it.
    connect(menu, &QMenu::aboutToShow, m_fullScreenAction,
            [this] { m_fullScreenAction->setChecked(fullScreenPreviewActive()); });

    menu->addSeparator();

    // Commands 40, 41 and 42 in the reference's own shortcut table - the same
    // three actions its Options menu drives, under the menu labels it uses
    // ("Scale to Fit", not "Zoom to Fit", which is the table's name for it).
    QAction* fit = menu->addAction(tr("Scale to Fit"));
    fit->setShortcut(QKeySequence(Qt::Key_QuoteLeft));
    fit->setShortcutContext(Qt::WidgetShortcut);
    connect(fit, &QAction::triggered, this, &ViewerWidget::zoomToFit);
    addAction(fit);

    QAction* zoomIn = menu->addAction(tr("Zoom In"));
    zoomIn->setShortcut(QKeySequence(Qt::Key_Equal));
    zoomIn->setShortcutContext(Qt::WidgetShortcut);
    connect(zoomIn, &QAction::triggered, this, &ViewerWidget::zoomIn);
    addAction(zoomIn);

    QAction* zoomOut = menu->addAction(tr("Zoom Out"));
    zoomOut->setShortcut(QKeySequence(Qt::Key_Minus));
    zoomOut->setShortcutContext(Qt::WidgetShortcut);
    connect(zoomOut, &QAction::triggered, this, &ViewerWidget::zoomOut);
    addAction(zoomOut);

    button->setMenu(menu);
    return button;
}

QWidget* ViewerWidget::createQualityButton(QWidget* parent)
{
    // toolButtonPlaybackQuality (141320cf8). Its menu builder FUN_140a88320
    // makes two exclusive groups - the four quality profiles and the four
    // resolutions - under the headings "Playback Quality"/"Playback Resolution"
    // or "Paused Quality"/"Paused Resolution"; the viewer offers both pairs, so
    // playing and paused can be set apart from each other.
    m_qualityButton = new QToolButton(parent);
    m_qualityButton->setObjectName(QStringLiteral("toolButtonPlaybackQuality"));
    m_qualityButton->setAutoRaise(true);
    m_qualityButton->setPopupMode(QToolButton::InstantPopup);
    m_qualityButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_qualityButton->setToolTip(tr("Preview quality and resolution"));

    auto* menu = new QMenu(m_qualityButton);

    struct QualityDef
    {
        QualityProfile profile;
        const char* label;
    };
    const QualityDef profiles[] = {
        {QualityProfile::Final,   QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Final")},
        {QualityProfile::Draft,   QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Draft")},
        {QualityProfile::Quick,   QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Quick")},
        {QualityProfile::Fastest, QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Fastest")},
    };
    struct ResolutionDef
    {
        Downsample mode;
        const char* label;
    };
    const ResolutionDef resolutions[] = {
        {Downsample::Antialiased, QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Antialiased")},
        {Downsample::Full,        QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "Full")},
        {Downsample::Half,        QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "1/2")},
        {Downsample::Quarter,     QT_TRANSLATE_NOOP("openvegas::ui::ViewerWidget", "1/4")},
    };

    const auto addQualityMenu = [&](const QString& title, bool playback) {
        QMenu* sub = menu->addMenu(title);
        sub->setObjectName(playback ? QStringLiteral("menuPlaybackQuality")
                                    : QStringLiteral("menuPausedQuality"));
        auto* group = new QActionGroup(sub);
        group->setExclusive(true);
        for (const QualityDef& def : profiles) {
            QAction* action = sub->addAction(tr(def.label));
            action->setCheckable(true);
            action->setData(static_cast<int>(def.profile));
            action->setChecked((playback ? m_playbackQuality : m_pausedQuality) == def.profile);
            group->addAction(action);
            connect(action, &QAction::triggered, this, [this, playback, p = def.profile] {
                if (playback) {
                    setPlaybackQuality(p);
                } else {
                    setPausedQuality(p);
                }
            });
        }
    };
    const auto addResolutionMenu = [&](const QString& title, bool playback) {
        QMenu* sub = menu->addMenu(title);
        sub->setObjectName(playback ? QStringLiteral("menuPlaybackResolution")
                                    : QStringLiteral("menuPausedResolution"));
        auto* group = new QActionGroup(sub);
        group->setExclusive(true);
        for (const ResolutionDef& def : resolutions) {
            QAction* action = sub->addAction(tr(def.label));
            action->setCheckable(true);
            action->setData(static_cast<int>(def.mode));
            action->setChecked((playback ? m_playbackResolution : m_pausedResolution) == def.mode);
            group->addAction(action);
            connect(action, &QAction::triggered, this, [this, playback, m = def.mode] {
                if (playback) {
                    setPlaybackResolution(m);
                } else {
                    setPausedResolution(m);
                }
            });
        }
    };

    addQualityMenu(tr("Playback Quality"), true);
    addResolutionMenu(tr("Playback Resolution"), true);
    menu->addSeparator();
    addQualityMenu(tr("Paused Quality"), false);
    addResolutionMenu(tr("Paused Resolution"), false);

    m_qualityButton->setMenu(menu);
    updateQualityButtonText();
    return m_qualityButton;
}

QRect ViewerWidget::viewRect(int index) const
{
    // The control bar occupies the top strip and the tool palette the left
    // one; the reference viewer puts its canvas beside the palette rather than
    // underneath it, so both are reserved here. Painting under the palette is
    // what clipped the timecode overlay.
    const QRect area(kToolStripWidth, 0,
                     qMax(0, width() - kToolStripWidth),
                     height());
    if (area.isEmpty())
        return QRect();

    switch (m_layout) {
    case ViewerLayout::Single:
        return area;
    case ViewerLayout::Row: {
        const int w = area.width() / 2;
        return QRect(area.x() + index * w, area.y(), w, area.height());
    }
    case ViewerLayout::TwoOverOne: {
        const int h = (area.height() + 1) / 2;
        if (index >= 2)
            return QRect(area.x(), area.y() + h, area.width(), area.height() - h);
        const int w = area.width() / 2;
        return QRect(area.x() + index * w, area.y(), w, h);
    }
    case ViewerLayout::Four: {
        const int w = (area.width() + 1) / 2;
        const int h = (area.height() + 1) / 2;
        const int col = index & 1;
        const int row = index >> 1;
        return QRect(area.x() + col * w, area.y() + row * h, w, h);
    }
    case ViewerLayout::RowFour: {
        const int w = area.width() / 4;
        return QRect(area.x() + index * w, area.y(), w, area.height());
    }
    }
    return area;
}

void ViewerWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 17, 17));

    const int count = viewCount();
    const int active = m_activeView;
    for (int i = 0; i < count; ++i) {
        const QRect vp = viewRect(i);
        if (vp.isEmpty())
            continue;

        painter.save();
        painter.setClipRect(vp.adjusted(2, 2, -2, -2));

        if (m_checkerboard) {
            paintCheckerboard(painter, QRectF(vp), m_backgroundColor);
        } else {
            painter.fillRect(vp, m_backgroundColor);
        }

        if (!m_frame.isNull()) {
            QRect target = imageRectForView(i);
            if (!target.isNull()) {
                if (i == m_activeView && !m_panOffset.isNull())
                    target.translate(m_panOffset);
                // Smooth, because a frame rendered above the view size is how
                // the Antialiased preview resolution does its work and a
                // frame rendered below it is how 1/2 and 1/4 do theirs.
                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter.drawImage(target, channelFiltered(m_frame));
                painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            }
        }

        paintMotionPath(painter, i);

        painter.restore();

        // Active view border.
        painter.setPen(QPen(active == i ? kActiveBorder : kInactiveBorder, active == i ? 2 : 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(vp.adjusted(1, 1, -1, -1));

        // Timecode overlay on the active view (HH:MM:SS:FF).
        if (i == active && m_hasTimecode) {
            const QString tc = formatTimecode(m_timecode);
            const QFontMetrics fm = painter.fontMetrics();
            const QRect textRect(vp.topLeft() + QPoint(12, 14),
                                 QSize(fm.horizontalAdvance(tc) + 10, fm.height()));
            const QRect box = textRect.adjusted(-4, -2, 8, 6);
            painter.fillRect(box, QColor(0, 0, 0, 170));
            painter.setPen(QColor(220, 220, 220));
            painter.drawText(textRect, Qt::AlignLeft, tc);
        }

    }

    // Rubber-band preview for the rectangle/ellipse tools.
    if (m_rubberBanding && !m_rubberStart.isNull() && !m_rubberCurrent.isNull()) {
        const QRect r = QRect(m_rubberStart, m_rubberCurrent).normalized();
        painter.setPen(QPen(QColor(18, 176, 255), 1, Qt::DashLine));
        painter.setBrush(QColor(18, 176, 255, 40));
        painter.setRenderHint(QPainter::Antialiasing, true);
        switch (m_tool) {
        case ViewerTool::Ellipse:
            painter.drawEllipse(r);
            break;
        case ViewerTool::RoundedRect: {
            // Corner radius scales with the smaller side, so the rounding stays
            // proportionate however the band is dragged.
            const qreal radius = qMin(r.width(), r.height()) * 0.25;
            painter.drawRoundedRect(r, radius, radius);
            break;
        }
        case ViewerTool::Polygon:
            painter.drawPolygon(regularPolygon(r, 5, 0.0));
            break;
        case ViewerTool::Star:
            painter.drawPolygon(starPolygon(r, 5, 0.42));
            break;
        default:
            painter.drawRect(r);
            break;
        }
        painter.setRenderHint(QPainter::Antialiasing, false);
    }
    if (m_freehandDrawing && m_freehandPoints.size() > 1) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(18, 176, 255), 2));
        painter.setBrush(QColor(18, 176, 255, 30));
        painter.drawPolyline(QPolygon(m_freehandPoints));
        painter.setRenderHint(QPainter::Antialiasing, false);
    }

    // Mouse coordinates, when the reference's Preferences option asks for them.
    // Image space, not widget space: the number a user wants is where the
    // pointer is in the frame, which is what a position or a mask point is
    // measured in.
    if (m_showMouseCoordinates && m_mouseInside) {
        const QString text =
            QStringLiteral("%1, %2").arg(m_mouseImagePos.x()).arg(m_mouseImagePos.y());
        const QFontMetrics metrics(painter.font());
        const QRect box = metrics.boundingRect(text).adjusted(-6, -3, 6, 3);
        const QRect area(kToolStripWidth + 6, height() - box.height() - 6, box.width(),
                         box.height());
        painter.fillRect(area, QColor(0, 0, 0, 150));
        painter.setPen(QColor(230, 230, 235));
        painter.drawText(area, Qt::AlignCenter, text);
    }

}

QImage ViewerWidget::channelFiltered(const QImage& source) const
{
    // RGB is the frame as composited; RGB Straight un-multiplies it, and the
    // four single-channel views show one component as grey. The reference keeps
    // the same six entries in its Color Channels submenu (FUN_1408d4b80).
    if (m_colorChannel == ColorChannel::Rgb || source.isNull()) {
        return source;
    }
    if (m_channelFrameChannel == m_colorChannel && m_channelFrameKey == source.cacheKey()
        && !m_channelFrame.isNull()) {
        return m_channelFrame;
    }

    QImage out = source.convertToFormat(QImage::Format_ARGB32);
    const int height = out.height();
    const int width = out.width();
    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const QRgb pixel = line[x];
            const int a = qAlpha(pixel);
            switch (m_colorChannel) {
            case ColorChannel::RgbStraight: {
                // Straight, not premultiplied: divide the colour back out of
                // the alpha it was composited with, so a half-transparent white
                // reads as white rather than as grey.
                if (a == 0 || a == 255) {
                    line[x] = pixel;
                } else {
                    const auto straight = [a](int c) { return qMin(255, c * 255 / a); };
                    line[x] = qRgba(straight(qRed(pixel)), straight(qGreen(pixel)),
                                    straight(qBlue(pixel)), a);
                }
                break;
            }
            case ColorChannel::Red: {
                const int v = qRed(pixel);
                line[x] = qRgba(v, v, v, 255);
                break;
            }
            case ColorChannel::Green: {
                const int v = qGreen(pixel);
                line[x] = qRgba(v, v, v, 255);
                break;
            }
            case ColorChannel::Blue: {
                const int v = qBlue(pixel);
                line[x] = qRgba(v, v, v, 255);
                break;
            }
            case ColorChannel::Alpha:
                line[x] = qRgba(a, a, a, 255);
                break;
            case ColorChannel::Rgb:
                break;
            }
        }
    }

    m_channelFrame = out;
    m_channelFrameChannel = m_colorChannel;
    m_channelFrameKey = source.cacheKey();
    return m_channelFrame;
}

void ViewerWidget::paintMotionPath(QPainter& painter, int viewIndex) const
{
    if (!m_showMotionPath || m_motionPath.size() < 2) {
        return;
    }
    QRect target = imageRectForView(viewIndex);
    if (target.isNull() || m_projectSize.isEmpty()) {
        return;
    }
    if (viewIndex == m_activeView && !m_panOffset.isNull()) {
        target.translate(m_panOffset);
    }
    const double sx = double(target.width()) / m_projectSize.width();
    const double sy = double(target.height()) / m_projectSize.height();
    const auto toWidget = [&](const QPointF& canvas) {
        return QPointF(target.x() + canvas.x() * sx, target.y() + canvas.y() * sy);
    };

    QPolygonF poly;
    poly.reserve(m_motionPath.size());
    for (const QPointF& point : m_motionPath) {
        poly << toWidget(point);
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Drawn twice: a dark backing line so the path stays visible over a bright
    // frame, then the light one on top.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 0, 0, 160), 3.0));
    painter.drawPolyline(poly);
    painter.setPen(QPen(QColor(240, 240, 245), 1.0));
    painter.drawPolyline(poly);

    // Keyframed points get a square, as the reference marks them.
    painter.setPen(QPen(QColor(20, 20, 20), 1.0));
    painter.setBrush(QColor(240, 200, 60));
    for (const QPointF& key : m_motionPathKeys) {
        const QPointF centre = toWidget(key);
        painter.drawRect(QRectF(centre.x() - 3.0, centre.y() - 3.0, 6.0, 6.0));
    }
    painter.restore();
}

void ViewerWidget::leaveEvent(QEvent* event)
{
    if (m_mouseInside) {
        m_mouseInside = false;
        update();
    }
    QWidget::leaveEvent(event);
}

void ViewerWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_toolStrip) {
        m_toolStrip->setGeometry(toolStripRect());
        m_toolStrip->raise();
    }
    update();
}

void ViewerWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const int index = viewAt(event->pos());
    if (index < 0) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (index != m_activeView)
        setActiveView(index);

    m_dragView = index;
    m_lastMouse = event->pos();

    if (m_tool == ViewerTool::Hand) {
        m_handPanning = true;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    // Orbit turns the selected layer; Select drags it. Both are reported as a
    // running delta from the last mouse position, so the window can push each
    // step onto the model without the viewer holding any of it.
    if (m_tool == ViewerTool::Orbit || m_tool == ViewerTool::Select) {
        m_layerDragging = true;
        setCursor(m_tool == ViewerTool::Orbit ? Qt::SizeAllCursor : Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (isShapeTool(m_tool)) {
        m_rubberStart = event->pos();
        m_rubberCurrent = event->pos();
        m_rubberBanding = true;
        event->accept();
        return;
    }

    if (m_tool == ViewerTool::Freehand || m_tool == ViewerTool::VectorPath) {
        m_freehandDrawing = true;
        m_freehandPoints = {event->pos()};
        event->accept();
        return;
    }

    if (m_tool == ViewerTool::Text) {
        placeText(index, event->pos());
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void ViewerWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && viewAt(event->pos()) >= 0) {
        emit textEditRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ViewerWidget::contextMenuEvent(QContextMenuEvent* event)
{
    if (viewAt(event->pos()) >= 0) {
        emit textContextMenuRequested(event->globalPos());
        event->accept();
        return;
    }
    QWidget::contextMenuEvent(event);
}

void ViewerWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_showMouseCoordinates) {
        const int view = viewAt(event->pos());
        m_mouseInside = view >= 0;
        if (m_mouseInside) {
            m_mouseImagePos = imagePos(view, event->pos());
        }
        update();
    }

    if (m_handPanning && m_dragView == m_activeView) {
        m_panOffset += event->pos() - m_lastMouse;
        m_lastMouse = event->pos();
        update();
        event->accept();
        return;
    }

    if (m_rubberBanding) {
        m_rubberCurrent = event->pos();
        update();
        event->accept();
        return;
    }

    if (m_freehandDrawing) {
        if (m_freehandPoints.isEmpty() || (event->pos() - m_freehandPoints.last()).manhattanLength() >= 2)
            m_freehandPoints.append(event->pos());
        update();
        event->accept();
        return;
    }

    if (m_layerDragging) {
        const QPointF delta = event->pos() - m_lastMouse;
        m_lastMouse = event->pos();
        if (!delta.isNull()) {
            if (m_tool == ViewerTool::Orbit) {
                emit layerOrbited(delta);
            } else {
                emit layerMoved(delta);
            }
        }
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void ViewerWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_handPanning && event->button() == Qt::LeftButton) {
        m_handPanning = false;
        m_dragView = -1;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }

    if (m_layerDragging && event->button() == Qt::LeftButton) {
        m_layerDragging = false;
        m_dragView = -1;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }

    if (m_rubberBanding && event->button() == Qt::LeftButton) {
        m_rubberBanding = false;
        m_rubberCurrent = event->pos();
        const QPoint first = imagePos(m_dragView, m_rubberStart);
        const QPoint last = imagePos(m_dragView, m_rubberCurrent);
        const QRectF bounds = QRectF(first, last).normalized();
        if (bounds.width() >= 2.0 && bounds.height() >= 2.0)
            emit maskCreationRequested(static_cast<int>(m_tool), bounds);
        m_dragView = -1;
        update();
        event->accept();
        return;
    }

    if (m_freehandDrawing && event->button() == Qt::LeftButton) {
        m_freehandDrawing = false;
        if (m_freehandPoints.isEmpty() || m_freehandPoints.last() != event->pos())
            m_freehandPoints.append(event->pos());
        QVector<QPointF> canvas;
        canvas.reserve(m_freehandPoints.size());
        for (const QPoint& point : m_freehandPoints)
            canvas.append(QPointF(imagePos(m_dragView, point)));
        if (canvas.size() >= 3) emit freehandMaskCreationRequested(canvas);
        m_freehandPoints.clear(); m_dragView = -1;
        update(); event->accept(); return;
    }

    QWidget::mouseReleaseEvent(event);
}

void ViewerWidget::placeText(int viewIndex, const QPoint& widgetPos)
{
    const QPoint img = imagePos(viewIndex, widgetPos);
    emit textCreationRequested(QPointF(img));
}

void ViewerWidget::keyPressEvent(QKeyEvent* event)
{
    // Arrow keys nudge the selected layer. The reference registers these in its
    // Viewer category as commands 1107-1110 ("Move position left/right/up/down
    // by 1 pixel") with the Shift variants 1111-1114 moving ten. Handled before
    // the tool letters because they take a modifier into account.
    const bool shifted = event->modifiers().testFlag(Qt::ShiftModifier);
    const double step = shifted ? 10.0 : 1.0;
    switch (event->key()) {
    case Qt::Key_Left:  emit layerNudged(QPointF(-step, 0.0)); return;
    case Qt::Key_Right: emit layerNudged(QPointF(step, 0.0)); return;
    case Qt::Key_Up:    emit layerNudged(QPointF(0.0, step)); return;
    case Qt::Key_Down:  emit layerNudged(QPointF(0.0, -step)); return;
    default: break;
    }
    if (event->modifiers() != Qt::NoModifier) {
        QWidget::keyPressEvent(event);
        return;
    }

    // Tool shortcuts fall back to the key handler when the widget owns focus.
    switch (event->key()) {
    case Qt::Key_V: setTool(ViewerTool::Select); return;
    case Qt::Key_H: setTool(ViewerTool::Hand); return;
    case Qt::Key_T: setTool(ViewerTool::Text); return;
    case Qt::Key_R: m_shapeTool = ViewerTool::Rectangle; updateShapeButton();
                    setTool(ViewerTool::Rectangle); return;
    case Qt::Key_E: m_shapeTool = ViewerTool::Ellipse; updateShapeButton();
                    setTool(ViewerTool::Ellipse); return;
    case Qt::Key_F: setTool(ViewerTool::Freehand); return;
    // B, as the reference keys Camera Orbit; this port used to bind O, which is
    // not one of its shortcuts at all.
    case Qt::Key_B: setTool(ViewerTool::Orbit); return;
    // Zoom, from the reference's General category: Zoom to Fit is 40 on the
    // backquote, Zoom In 41 on "=" and Zoom Out 42 on "-".
    case Qt::Key_QuoteLeft: zoomToFit(); return;
    case Qt::Key_Equal:     zoomIn(); return;
    case Qt::Key_Minus:     zoomOut(); return;
    default: break;
    }
    QWidget::keyPressEvent(event);
}

void ViewerWidget::wheelEvent(QWheelEvent* event)
{
    if (event->angleDelta().y() > 0)
        zoomIn();
    else
        zoomOut();
    event->accept();
}

} // namespace ui
} // namespace openvegas
