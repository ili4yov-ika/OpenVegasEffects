#include "ui/ViewScaleButton.h"

#include <QAction>
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>
#include <QtGlobal>

namespace openvegas {
namespace ui {

namespace {

// "Zoom to Fit" action label (reference viewer command #40).
const char kFitText[] = QT_TRANSLATE_NOOP("openvegas::ui::ViewScaleButton", "Fit");
const char kZoomInText[] = "+";
const char kZoomOutText[] = "-";

// Reference viewer zoom ladder: fit, then fixed percentages.
const int kZoomSteps[] = {12, 25, 50, 75, 100, 150, 200};

} // namespace

ViewScaleButton::ViewScaleButton(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ViewScaleButton"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_outButton = makeToolButton(QString::fromLatin1(kZoomOutText), &ViewScaleButton::zoomOut);
    m_labelButton = makeToolButton(tr(kFitText), &ViewScaleButton::zoomToFit);
    m_inButton = makeToolButton(QString::fromLatin1(kZoomInText), &ViewScaleButton::zoomIn);

    auto* popup = new QMenu(m_labelButton);
    connect(popup, &QMenu::triggered, this, [this](QAction* action) {
        const int percent = action->data().toInt(); // -1 => fit
        setZoomPercent(percent);
    });
    auto addFit = popup->addAction(tr("Fit"));
    addFit->setData(-1);
    popup->addSeparator();
    for (int step : kZoomSteps) {
        QAction* action = popup->addAction(tr("%1%").arg(step));
        action->setData(step);
    }
    m_labelButton->setMenu(popup);
    m_labelButton->setPopupMode(QToolButton::InstantPopup);
    // Named so the theme can place its menu indicator beside the label rather
    // than in the corner underneath it.
    m_labelButton->setObjectName(QStringLiteral("viewScaleLabel"));
    m_labelButton->setMinimumWidth(72);

    layout->addWidget(m_outButton);
    layout->addWidget(m_labelButton, 1);
    layout->addWidget(m_inButton);

    setZoomPercent(-1);
}

QToolButton* ViewScaleButton::makeToolButton(const QString& text, void (ViewScaleButton::*slot)())
{
    auto* button = new QToolButton(this);
    button->setText(text);
    button->setAutoRaise(true);
    button->setMinimumWidth(24);
    connect(button, &QToolButton::clicked, this, [this, slot] { (this->*slot)(); });
    return button;
}

void ViewScaleButton::setZoomPercent(int percent)
{
    if (m_zoomPercent == percent)
        return;
    m_zoomPercent = percent;
    updateButtonText();
    emit zoomPercentChanged(percent);
}

void ViewScaleButton::zoomIn()
{
    setZoomPercent(isFit() ? 50 : m_zoomPercent + 25);
}

void ViewScaleButton::zoomOut()
{
    if (isFit())
        return;
    setZoomPercent(qMax(12, m_zoomPercent - 25));
}

void ViewScaleButton::updateButtonText()
{
    m_labelButton->setText(isFit() ? tr(kFitText) : QStringLiteral("%1%").arg(m_zoomPercent));
}

} // namespace ui
} // namespace openvegas
