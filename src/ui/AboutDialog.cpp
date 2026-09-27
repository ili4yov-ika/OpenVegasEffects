#include "ui/AboutDialog.h"

#include "ui_AboutDialog.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QPixmap>
#include <QSysInfo>
#include <QTextDocument>

namespace openvegas {
namespace ui {

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
    , m_ui(new Ui::AboutDialog)
{
    m_ui->setupUi(this);
    setObjectName(QStringLiteral("aboutDialog"));
    setWindowTitle(tr("About OpenVegas Effects"));

    const QPixmap logo(QStringLiteral(":/icons/logo.png"));
    if (!logo.isNull()) {
        m_ui->labelLogo->setPixmap(logo);
    } else {
        m_ui->labelLogo->hide();
    }

    m_ui->labelVersion->setText(
        tr("Version %1").arg(QCoreApplication::applicationVersion()));
    m_ui->labelBuild->setText(tr("Qt %1 - %2")
                                  .arg(QLibraryInfo::version().toString(),
                                       QSysInfo::prettyProductName()));

    // Default link colour is a dark maroon, unreadable on the dark theme; use
    // the reference accent instead.
    m_ui->textDetails->document()->setDefaultStyleSheet(
        QStringLiteral("a { color: #12b0ff; }"));
    m_ui->textDetails->setHtml(
        tr("<p>Original code released under the GNU GPL v3. The reference "
           "binaries studied during development remain the property of their "
           "respective owners.</p>"
           "<p>Project page: "
           "<a href=\"https://github.com/ili4yov-ika/OpenVegasEffects\">"
           "github.com/ili4yov-ika/OpenVegasEffects</a></p>"));
}

AboutDialog::~AboutDialog()
{
    delete m_ui;
}

} // namespace ui
} // namespace openvegas
