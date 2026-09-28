#include "omachat/ipc/IpcMessage.hpp"

#include "omachat/core/Version.hpp"

#include <QJsonDocument>
#include <QJsonParseError>

namespace omachat::ipc {

QByteArray encode(const QJsonObject& message)
{
    QByteArray out = QJsonDocument(message).toJson(QJsonDocument::Compact);
    out.append('\n');
    return out;
}

QJsonObject makeRequest(qint64 id, const QString& method, const QJsonObject& params)
{
    return QJsonObject{{"v", kIpcVersion}, {"id", id}, {"method", method}, {"params", params}};
}

QJsonObject makeResult(qint64 id, const QJsonObject& result)
{
    return QJsonObject{{"v", kIpcVersion}, {"id", id}, {"ok", true}, {"result", result}};
}

QJsonObject makeError(qint64 id, const QString& code, const QString& message)
{
    return QJsonObject{
        {"v", kIpcVersion}, {"id", id}, {"ok", false}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

QJsonObject makeEvent(const QString& name, const QJsonObject& data)
{
    return QJsonObject{{"v", kIpcVersion}, {"event", name}, {"data", data}};
}

LineDecoder::Status LineDecoder::next(QByteArray& line)
{
    const qsizetype newline = m_buffer.indexOf('\n');
    if (newline < 0) {
        return m_buffer.size() > kMaxLineBytes ? Status::Oversized : Status::NeedMore;
    }
    if (newline > kMaxLineBytes)
        return Status::Oversized;
    line = m_buffer.left(newline);
    m_buffer.remove(0, newline + 1);
    return Status::Line;
}

std::optional<QJsonObject> parse(const QByteArray& line)
{
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    return doc.object();
}

} // namespace omachat::ipc
