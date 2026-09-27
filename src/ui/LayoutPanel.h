#pragma once

#include <QDockWidget>
#include <QRectF>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QToolButton;

namespace openvegas {
namespace ui {

// Mirrors the reference "Layout" panel (type 2055, objectName "LayoutPanel",
// RTTI ".?AVLayoutPanel@ui@biff@@").
//
// The panel stacks the reference's two reusable widgets, in this order:
//
//   biff::ui::common::TransformWidget  (setupUi FUN_140315400,
//                                       retranslateUi FUN_1403164a0)
//     a row of four buttons - Mirror Vertical, Mirror Horizontal, Rotate 90
//     Degrees Counter Clockwise, Rotate 90 Degrees Clockwise - over a group box
//     holding widgetDirection (which corner or edge X and Y are measured to),
//     spinBoxX / spinBoxY / spinBoxWidth / spinBoxHeight and the
//     toolButtonScaleLinked chain that keeps width and height in proportion.
//
//   biff::ui::common::AlignmentWidget  (setupUi FUN_140329cd0,
//                                       retranslateUi FUN_14032b3a0)
//     a group box titled "Alignment" with comboBoxAlignTo (Selection /
//     Timeline), six align buttons and six distribute buttons.
//
// Both are built here rather than as classes of their own: what is recoverable
// from the reference is the widget tree and the names, and those are reproduced
// exactly - the objectNames below are the reference's own.
//
// What this panel used to be - viewer layout presets plus a workspace picker -
// was a misreading. The toolButtonLayout1/2/2_1/2_2/4 buttons and their
// "layout-*" ids belong to the reference's *Scopes* panel (builder FUN_1402915c0
// is ScopesPanelWidget, whose tooltips are "One Scope" ... "Four Scopes"), and
// the workspace commands live in the Window > Workspaces menu (FUN_14039e2e0)
// and the WorkspaceDialog, not here.
class LayoutPanel : public QDockWidget
{
    Q_OBJECT

public:
    // Reference biff::ui::common::DirectionWidget::Direction (RTTI string
    // 1412dd1b0, property "direction"): which point of the bounding box the X
    // and Y readouts are measured to.
    enum class Direction
    {
        TopLeft,
        Top,
        TopRight,
        Left,
        Center,
        Right,
        BottomLeft,
        Bottom,
        BottomRight,
    };
    Q_ENUM(Direction)

    // Items of comboBoxAlignTo, in the reference's own order.
    enum class AlignTo
    {
        Selection,
        Timeline,
    };
    Q_ENUM(AlignTo)

    explicit LayoutPanel(QWidget* parent = nullptr);

    // Bounding box of the selection in canvas pixels, origin top-left - the
    // space the renderer composites in, and the one the reference's X/Y/Width/
    // Height are in (a full-frame layer of a 1920x1080 project reads
    // 960 / 540 / 1920 / 1080 with the centre direction selected).
    void setSelectionBounds(const QRectF& bounds);
    void setSelectionBounds(const QVector<QRectF>& bounds);
    void clearSelection();
    // The composition frame, in the same canvas pixels - what "Align to:
    // Timeline" aligns against.
    void setFrameRect(const QRectF& frame);
    bool hasSelection() const { return m_hasSelection; }
    QRectF selectionBounds() const { return m_bounds; }

    Direction direction() const { return m_direction; }
    void setDirection(Direction direction);
    AlignTo alignTo() const;

signals:
    // The user typed a new X, Y, Width or Height, or pressed an align button:
    // the selection's new bounding box, in canvas pixels.
    void boundsEdited(QRectF bounds);
    void selectionBoundsEdited(const QVector<QRectF>& bounds);
    // Mirror Horizontal / Mirror Vertical - the axis to flip about.
    void mirrorRequested(Qt::Orientation orientation);
    // +90 for Rotate 90 Degrees Clockwise, -90 for counter clockwise.
    void rotateRequested(int degrees);

private:
    // Pushes m_bounds into the four spin boxes, honouring the direction.
    void refreshFields();
    // Reads the four spin boxes back into a bounding box.
    QRectF boundsFromFields() const;
    void emitBounds(const QRectF& bounds);
    void applyAlignment(Qt::Alignment alignment);
    void applyDistribution(const QString& mode);
    void updateEnabled();

    QDoubleSpinBox* m_x = nullptr;
    QDoubleSpinBox* m_y = nullptr;
    QDoubleSpinBox* m_width = nullptr;
    QDoubleSpinBox* m_height = nullptr;
    QToolButton* m_scaleLinked = nullptr;
    QComboBox* m_alignTo = nullptr;
    QWidget* m_alignRow = nullptr;
    QWidget* m_distributeRow = nullptr;
    QList<QToolButton*> m_directionButtons;

    Direction m_direction = Direction::Center;
    QRectF m_bounds;
    QVector<QRectF> m_selectionBounds;
    QRectF m_frame{0.0, 0.0, 1920.0, 1080.0};
    bool m_hasSelection = false;
    // Guards refreshFields() against the valueChanged it causes.
    bool m_updating = false;
    // Aspect kept by toolButtonScaleLinked, captured when the link is switched
    // on so that a chain of edits does not drift.
    double m_linkedAspect = 1.0;
};

} // namespace ui
} // namespace openvegas
