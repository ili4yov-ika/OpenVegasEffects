#pragma once

#include <QString>

namespace openvegas {
namespace core {

enum class ResultStatus
{
    Success    = 0,
    InvalidArgument,
    MissingResource,
    PermissionDenied,
    OperationFailed,
    Unsupported,
    Aborted,
};

class Result
{
public:
    Result() = default;

    static Result ok() { return Result(ResultStatus::Success, {}); }

    static Result fail(ResultStatus status, const QString& message)
    {
        return Result(status, message);
    }

    bool isSuccess() const { return m_status == ResultStatus::Success; }
    bool isFailure() const { return m_status != ResultStatus::Success; }

    ResultStatus status() const { return m_status; }
    const QString& message() const { return m_message; }

private:
    Result(ResultStatus status, const QString& message)
        : m_status(status)
        , m_message(message)
    {
    }

    ResultStatus m_status = ResultStatus::Success;
    QString m_message;
};

} // namespace core
} // namespace openvegas