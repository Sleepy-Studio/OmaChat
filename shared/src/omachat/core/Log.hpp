#pragma once

#include <QString>

#include <concepts>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace omachat::log {

enum class Level { Trace = 0, Debug, Info, Warning, Error, Critical, Off };

// A structured field. Values are rendered verbatim; callers must never pass
// passwords, tokens, keys or authentication headers.
struct Field {
    std::string_view key;
    QString value;

    Field(std::string_view k, QString v)
        : key(k)
        , value(std::move(v))
    {
    }
    Field(std::string_view k, const char* v)
        : key(k)
        , value(QString::fromUtf8(v))
    {
    }
    Field(std::string_view k, std::string_view v)
        : key(k)
        , value(QString::fromUtf8(v.data(), static_cast<qsizetype>(v.size())))
    {
    }
    template <std::integral T>
        requires(!std::same_as<T, bool>)
    Field(std::string_view k, T v)
        : key(k)
        , value(QString::number(static_cast<std::conditional_t<std::is_signed_v<T>, qint64, quint64>>(v)))
    {
    }
    Field(std::string_view k, bool v)
        : key(k)
        , value(v ? QStringLiteral("true") : QStringLiteral("false"))
    {
    }
    Field(std::string_view k, double v)
        : key(k)
        , value(QString::number(v, 'g', 6))
    {
    }
};

void setLevel(Level level);
Level level();
std::optional<Level> parseLevel(std::string_view name);

// Installs a Qt message handler so qWarning()/qDebug() from Qt itself flow
// through the same structured sink. `component` names the process.
void initialize(std::string_view component, Level level);

void write(Level level, std::string_view category, std::string_view message, std::initializer_list<Field> fields = {});

inline bool enabled(Level l)
{
    return l >= level();
}

} // namespace omachat::log

#define OMA_LOG(lvl, cat, msg, ...)                                                                                    \
    do {                                                                                                               \
        if (::omachat::log::enabled(lvl))                                                                              \
            ::omachat::log::write(lvl, cat, msg, {__VA_ARGS__});                                                       \
    } while (false)

#define OMA_TRACE(cat, msg, ...) OMA_LOG(::omachat::log::Level::Trace, cat, msg, __VA_ARGS__)
#define OMA_DEBUG(cat, msg, ...) OMA_LOG(::omachat::log::Level::Debug, cat, msg, __VA_ARGS__)
#define OMA_INFO(cat, msg, ...) OMA_LOG(::omachat::log::Level::Info, cat, msg, __VA_ARGS__)
#define OMA_WARN(cat, msg, ...) OMA_LOG(::omachat::log::Level::Warning, cat, msg, __VA_ARGS__)
#define OMA_ERROR(cat, msg, ...) OMA_LOG(::omachat::log::Level::Error, cat, msg, __VA_ARGS__)
#define OMA_CRITICAL(cat, msg, ...) OMA_LOG(::omachat::log::Level::Critical, cat, msg, __VA_ARGS__)
