#include "ui/LearnSidebar.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace openvegas {
namespace ui {

LearnSidebar::LearnSidebar(QWidget* parent)
    : QDockWidget(parent)
{
    setWindowTitle(tr("Learn"));
    setObjectName(QStringLiteral("LearnSidebar"));

    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("learnWebViewSpace"));
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    auto* title = new QLabel(tr("Learn"), root);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 1.5);
    title->setFont(titleFont);
    layout->addWidget(title);

    // The reference's own invitation on its learning strip (1412d7f70).
    auto* intro = new QLabel(
        tr("Go online to explore amazing tutorials and unlock your creative potential."), root);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    layout->addSpacing(8);

    const auto link = [root, layout](const QString& objectName, const QString& text) {
        auto* button = new QPushButton(text, root);
        button->setObjectName(objectName);
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { color:#12b0ff; text-align:left; border:0; padding:2px 0; }"
            "QPushButton:hover { text-decoration:underline; }"));
        layout->addWidget(button);
        return button;
    };
    connect(link(QStringLiteral("learnBrowseTutorials"), tr("Browse tutorials")),
            &QPushButton::clicked, this, &LearnSidebar::tutorialsRequested);
    connect(link(QStringLiteral("learnCreateCompositeShot"), tr("Create a composite shot")),
            &QPushButton::clicked, this,
            [this] { emit commandRequested(QStringLiteral("Create New Composite Shot")); });
    connect(link(QStringLiteral("learnImportMedia"), tr("Import your first clip")),
            &QPushButton::clicked, this,
            [this] { emit commandRequested(QStringLiteral("Import Media")); });
    layout->addStretch();
    setWidget(root);
}

} // namespace ui
} // namespace openvegas
