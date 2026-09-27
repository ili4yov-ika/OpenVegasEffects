#pragma once

#include <QString>
#include <QtGlobal>

#include <qbuffer.h>
#include <qdebug.h>
#include <qfile.h>
#include <qtextstream.h>

#include <memory>

#include <mutex>

namespace openvegas {
namespace core {

enum class LogLevel
{
    Debug = 0,
    Info,
    Warning,
    Error,
};

class Log
{
public:
    static Log& instance();

    void setFile(const QString& filePath);
    void setLevelThreshold(LogLevel level) { m_threshold = level; }

    void write(LogLevel level, const QString& message);

private:
    Log() = default;

    std::mutex m_mutex;
    std::unique_ptr<QFile> m_file;
    LogLevel m_threshold = LogLevel::Info;
};

#define OV_LOG_DEBUG(msg) ::openvegas::core::Log::instance().write(::openvegas::core::LogLevel::Debug, msg)
#define OV_LOG_INFO(msg) ::openvegas::core::Log::instance().write(::openvegas::core::LogLevel::Info, msg)
#define OV_LOG_WARN(msg) ::openvegas::core::Log::instance().write(::openvegas::core::LogLevel::Warning, msg)
#define OV_LOG_ERROR(msg) ::openvegas::core::Log::instance().write(::openvegas::core::LogLevel::Error, msg)

} // namespace core
} // namespace openvegas