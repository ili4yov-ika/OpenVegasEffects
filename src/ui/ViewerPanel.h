#pragma once

#include <QWidget>

namespace openvegas::ui {

class ViewerTransportBar;
class ViewerWidget;

class ViewerPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit ViewerPanel(QWidget* parent = nullptr);

    ViewerWidget* viewer() const { return m_viewer; }
    ViewerTransportBar* transportBar() const { return m_transportBar; }

private:
    ViewerWidget* m_viewer = nullptr;
    ViewerTransportBar* m_transportBar = nullptr;
};

} // namespace openvegas::ui
