#include "core/Log.h"

namespace openvegas {
namespace core {

Log& Log::instance()
{
    static Log s_instance;
    return s_instance;
}

void Log::setFile(const QString& filePath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_file = std::make_unique<QFile>(filePath);
    if (m_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(m_file.get());
        stream << "\n--- OpenVegasEffects log start ---\n";
        stream.flush();
    }
}

void Log::write(LogLevel level, const QString& message)
{
    if (level < m_threshold) {
        return;
    }

    static const char* kLevelNames[] = {"DEBUG", "INFO", "WARN", "ERROR"};

    QString line = QStringLiteral("[%1] %2")
                       .arg(QString::fromLatin1(kLevelNames[static_cast<int>(level)]), message);

    qDebug().noquote() << line;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file && m_file->isOpen()) {
        QTextStream stream(m_file.get());
        stream << line << '\n';
        stream.flush();
    }
}

} // namespace core
} // namespace openvegas