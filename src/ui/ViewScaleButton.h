#pragma once

#include <QString>
#include <QWidget>

class QToolButton;

namespace openvegas {
namespace ui {

// Viewer magnification control, ported from the reference
// `biff::ui::common::ViewScaleButton`: a "fit" + fixed-percent zoom picker
// with zoom-out/in buttons. Zoom factors follow the reference viewer scale
// ladder (fit, then 25%..200%).
class ViewScaleButton : public QWidget
{
    Q_OBJECT

public:
    explicit ViewScaleButton(QWidget* parent = nullptr);

    // -1 => fit (auto); otherwise integer percent (12, 25, 50, 75, ...).
    int zoomPercent() const { return m_zoomPercent; }
    bool isFit() const { return m_zoomPercent < 0; }

public slots:
    void setZoomPercent(int percent); // -1 for fit
    void zoomIn();
    void zoomOut();
    void zoomToFit() { setZoomPercent(-1); }

signals:
    void zoomPercentChanged(int percent); // -1 for fit

private:
    void updateButtonText();
    QToolButton* makeToolButton(const QString& text, void (ViewScaleButton::*slot)());

    QToolButton* m_outButton = nullptr;
    QToolButton* m_labelButton = nullptr;
    QToolButton* m_inButton = nullptr;
    int m_zoomPercent = -1;
};

} // namespace ui
} // namespace openvegas
