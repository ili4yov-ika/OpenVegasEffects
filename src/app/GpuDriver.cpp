#include "app/GpuDriver.h"

#include "ui/PromptMessage.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QSettings>
#include <QStringList>

namespace openvegas::app {

namespace {

// The reference's table (FUN_1401bffe0), in its order: class, vendor (the
// renderer starts with it), model (the renderer ends with it).
struct GpuEntry
{
    int gpuClass;
    const char* vendor;
    const char* model;
};

constexpr GpuEntry kGpus[] = {
    {7, "Intel", " UHD Graphics 605"},  {7, "Intel", " UHD Graphics 620"},
    {7, "Intel", " UHD Graphics 630"},  {7, "Radeon", "RX Vega M GH Graphics"},
    {7, "Radeon", "RX Vega M GL Graphics"},
    {6, "Intel", " HD Graphics 500"},   {6, "Intel", " HD Graphics 505"},
    {6, "Intel", " HD Graphics 610"},   {6, "Intel", " HD Graphics 615"},
    {6, "Intel", " HD Graphics 620"},   {6, "Intel", " HD Graphics 630"},
    {6, "Intel", " HD Graphics P630"},  {6, "Intel", "Plus Graphics 640"},
    {6, "Intel", "Plus Graphics 650"},
    {5, "Intel", " HD Graphics 510"},   {5, "Intel", " HD Graphics 515"},
    {5, "Intel", " HD Graphics 520"},   {5, "Intel", " HD Graphics 530"},
    {5, "Intel", "Graphics 540"},       {5, "Intel", "Graphics 550"},
    {5, "Intel", "Pro Graphics 580"},
    {4, "Intel", " HD Graphics 5300"},  {4, "Intel", " HD Graphics 5500"},
    {4, "Intel", " HD Graphics 6000"},  {4, "Intel", "Graphics 6100"},
    {4, "Intel", "Pro Graphics 6200"},
    {3, "Intel", " HD Graphics 4200"},  {3, "Intel", " HD Graphics 4400"},
    {3, "Intel", " HD Graphics 4600"},  {3, "Intel", " HD Graphics 5000"},
    {3, "Intel", "Graphics 5100"},      {3, "Intel", "Pro Graphics 520"},
    {2, "Intel", " HD Graphics 4000"},  {2, "Intel", " HD Graphics 2500"},
    {1, "Intel", " HD Graphics 2000"},  {1, "Intel", " HD Graphics 3000"},
    {0, "Intel", " GMA 3150)"},         {0, "Intel", " GMA 600)"},
};

// Intel's download centre, where the reference's warning sends one.
const char kDriverDownloads[] = "https://downloadcenter.intel.com/";

QString text(const char* source)
{
    return QCoreApplication::translate("QObject", source);
}

} // namespace

int gpuClassOf(const QString& renderer)
{
    if (renderer.isEmpty()) return 8;
    for (const GpuEntry& entry : kGpus) {
        if (renderer.startsWith(QLatin1String(entry.vendor), Qt::CaseInsensitive)
            && renderer.endsWith(QLatin1String(entry.model), Qt::CaseInsensitive))
            return entry.gpuClass;
    }
    return 8;
}

GpuDriverInfo gpuDriverInfo(const QString& renderer, const QString& glVersion)
{
    GpuDriverInfo info;
#ifdef Q_OS_WIN
    // The Display device class: one subkey per adapter (0000, 0001, ...).
    QSettings adapters(QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\"
                                      "{4d36e968-e325-11ce-bfc1-08002be10318}"),
                       QSettings::NativeFormat);
    for (const QString& adapter : adapters.childGroups()) {
        bool numbered = false;
        adapter.toInt(&numbered);
        if (!numbered) continue;
        const QString description = adapters.value(adapter + QStringLiteral("/DriverDesc")).toString();
        if (description.isEmpty()) continue;
        if (!renderer.contains(description, Qt::CaseInsensitive)
            && !description.contains(renderer, Qt::CaseInsensitive))
            continue;
        info.version = adapters.value(adapter + QStringLiteral("/DriverVersion")).toString();
        info.date = adapters.value(adapter + QStringLiteral("/DriverDate")).toString();
        info.device = description;
        break;
    }
#endif
    if (info.device.isEmpty()) info.device = renderer;
    if (info.version.isEmpty()) {
        const QStringList words = glVersion.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (!words.isEmpty()) info.version = words.last();
    }
    return info;
}

GpuDriverCheck checkGpuDriver(const QString& renderer, const QString& driverVersion)
{
    GpuDriverCheck check;
    check.gpuClass = gpuClassOf(renderer);
    if (check.gpuClass >= 5 && check.gpuClass <= 7) check.required = 1006326;
    else if (check.gpuClass == 3 || check.gpuClass == 4) check.required = 5057;
    else if (check.gpuClass == 2) check.required = 5059;
    if (check.required == 0) return check;
    const QStringList parts = driverVersion.trimmed().split(QLatin1Char('.'));
    bool ok = false;
    const qint64 last = parts.isEmpty() ? 0 : parts.last().toLongLong(&ok);
    if (!ok) return check;   // nothing to compare: no warning
    check.installed = last;
    if (check.gpuClass >= 5 && parts.size() >= 2) {
        bool previousOk = false;
        const qint64 previous = parts.at(parts.size() - 2).toLongLong(&previousOk);
        if (previousOk) check.installed = previous * 10000 + last;
    }
    return check;
}

QString gpuDriverWarningText(const GpuDriverInfo& info)
{
    QString message = text(QT_TRANSLATE_NOOP("QObject", "Your GPU driver is out of date.<br>"));
    if (!info.device.isEmpty()) message += text(QT_TRANSLATE_NOOP("QObject", "<br>Device: %1")).arg(info.device);
    if (!info.version.isEmpty()) message += text(QT_TRANSLATE_NOOP("QObject", "<br>Driver Version: %1")).arg(info.version);
    if (!info.date.isEmpty()) message += text(QT_TRANSLATE_NOOP("QObject", "<br>Driver Date: %1")).arg(info.date);
    message += text(QT_TRANSLATE_NOOP("QObject", "<br><br>Please ensure that you have the latest GPU driver installed and try "
                    "again:<br><br><a href=\"%1\">%1</a><br>"))
                   .arg(QLatin1String(kDriverDownloads));
    return message;
}

bool warnAboutGpuDriver(QWidget* parent, const QString& renderer, const QString& glVersion)
{
    if (gpuClassOf(renderer) > 7) return true;
    const GpuDriverInfo info = gpuDriverInfo(renderer, glVersion);
    if (!checkGpuDriver(renderer, info.version).outdated()) return true;
    const auto answer = ui::showPrompt(
        parent, QStringLiteral("GPUDriverWarning"), QMessageBox::Warning,
        text(QT_TRANSLATE_NOOP("QObject", "%1 : Outdated GPU Driver")).arg(QCoreApplication::applicationName()),
        gpuDriverWarningText(info), QMessageBox::Ignore | QMessageBox::Abort, QMessageBox::Ignore,
        [](QMessageBox& box) {
            box.setTextFormat(Qt::RichText);
            box.button(QMessageBox::Ignore)->setText(text(QT_TRANSLATE_NOOP("QObject", "Continue")));
            box.button(QMessageBox::Abort)->setText(text(QT_TRANSLATE_NOOP("QObject", "Exit")));
        },
        QMessageBox::Ignore);
    return answer != QMessageBox::Abort;
}

} // namespace openvegas::app
