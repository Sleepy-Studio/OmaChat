#pragma once

#include "storage/Records.hpp"
#include <QRegularExpression>
#include <set>

namespace omachat::server {

inline std::vector<std::pair<unsigned, Id>> broadMentionTokens(const QString& content)
{
    static const QRegularExpression expression(QStringLiteral(R"((?<![\w@])@\{(everyone|role:([1-9][0-9]*))\})"));
    std::set<std::pair<unsigned, Id>> targets;
    auto matches = expression.globalMatch(content);
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (match.captured(1) == QLatin1String("everyone")) {
            targets.insert({4, 0});
        } else {
            bool valid = false;
            const auto id = match.captured(2).toULongLong(&valid);
            if (valid && id)
                targets.insert({2, id});
        }
    }
    return {targets.begin(), targets.end()};
}

} // namespace omachat::server
