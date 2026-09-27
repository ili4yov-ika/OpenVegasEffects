#include "ui/Theme.h"
#include "ui/ViewerPanel.h"

#include "ui/ViewerTransportBar.h"
#include "ui/ViewerWidget.h"
#include "ui_Viewer.h"

namespace openvegas::ui {

ViewerPanel::ViewerPanel(QWidget* parent)
    : QWidget(parent)
{
    Ui::ViewerPanel form;
    form.setupUi(this);
    m_viewer = form.graphicsViewViewer;
    m_transportBar = form.ViewerPlaybackWidget;
    m_transportBar->addTrailingWidget(m_viewer->createViewOptionsBar(m_transportBar));
}

} // namespace openvegas::ui
