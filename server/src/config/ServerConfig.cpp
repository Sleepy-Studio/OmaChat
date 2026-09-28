#include "config/ServerConfig.hpp"

#include <QFileInfo>

#include <toml++/toml.hpp>

#include <sstream>

namespace omachat::server {

bool ServerConfig::load(const QString& path, ServerConfig& out, QString* error)
{
    toml::table tbl;
    try {
        tbl = toml::parse_file(path.toStdString());
    } catch (const toml::parse_error& e) {
        std::ostringstream os;
        os << e;
        if (error)
            *error = QString::fromStdString(os.str());
        return false;
    }

    const QString baseDir = QFileInfo(path).absolutePath();
    auto resolve = [&](const QString& p) {
        if (p.isEmpty() || QFileInfo(p).isAbsolute())
            return p;
        return baseDir + u'/' + p;
    };
    auto str = [](auto node, const QString& fallback) {
        if (auto v = node.template value<std::string>())
            return QString::fromStdString(*v);
        return fallback;
    };
    auto integer = [&](auto node, std::int64_t fallback, std::int64_t lo, std::int64_t hi,
                       const char* key) -> std::optional<std::int64_t> {
        if (!node)
            return fallback;
        auto v = node.template value<std::int64_t>();
        if (!v || *v < lo || *v > hi) {
            if (error)
                *error = QStringLiteral("%1 must be an integer in [%2, %3]").arg(key).arg(lo).arg(hi);
            return std::nullopt;
        }
        return *v;
    };

    const toml::table& t = tbl;
    out.instanceName = str(t["server"]["name"], out.instanceName);
    out.bind = str(t["server"]["bind"], out.bind);
    auto port = integer(t["server"]["port"], out.port, 1, 65535, "server.port");
    auto node = integer(t["server"]["node_id"], out.nodeId, 0, 1023, "server.node_id");
    out.registrationOpen = t["server"]["registration_open"].value_or(out.registrationOpen);
    auto mport = integer(t["media"]["udp_port"], out.mediaPort, 1, 65535, "media.udp_port");
    out.mediaBind = str(t["media"]["bind"], out.mediaBind);
    auto bitrate = integer(t["media"]["voice_bitrate"], out.voiceBitrate, 24000, 96000, "media.voice_bitrate");
    out.databasePath = resolve(str(t["database"]["path"], out.databasePath));
    const QString dbType = str(t["database"]["type"], QStringLiteral("sqlite"));
    out.tlsCertificate = resolve(str(t["tls"]["certificate"], out.tlsCertificate));
    out.tlsPrivateKey = resolve(str(t["tls"]["private_key"], out.tlsPrivateKey));
    out.filesPath = resolve(str(t["files"]["path"], out.filesPath));
    auto upload = integer(t["files"]["max_upload_mb"], out.maxUploadMb, 1, 4096, "files.max_upload_mb");
    auto access
        = integer(t["auth"]["access_token_minutes"], out.accessTokenMinutes, 1, 1440, "auth.access_token_minutes");
    auto refresh = integer(t["auth"]["refresh_token_days"], out.refreshTokenDays, 1, 365, "auth.refresh_token_days");
    auto perIp = integer(
        t["limits"]["max_connections_per_ip"], out.maxConnectionsPerIp, 1, 10000, "limits.max_connections_per_ip");
    out.logLevel = str(t["log"]["level"], out.logLevel);

    if (!port || !node || !mport || !bitrate || !upload || !access || !refresh || !perIp)
        return false;
    if (dbType != u"sqlite") {
        if (error)
            *error = QStringLiteral("database.type '%1' is not supported (only 'sqlite')").arg(dbType);
        return false;
    }
    out.port = static_cast<std::uint16_t>(*port);
    out.nodeId = static_cast<std::uint32_t>(*node);
    out.mediaPort = static_cast<std::uint16_t>(*mport);
    out.voiceBitrate = static_cast<int>(*bitrate);
    out.maxUploadMb = static_cast<int>(*upload);
    out.accessTokenMinutes = static_cast<int>(*access);
    out.refreshTokenDays = static_cast<int>(*refresh);
    out.maxConnectionsPerIp = static_cast<int>(*perIp);
    if (out.mediaBind.isEmpty())
        out.mediaBind = out.bind;
    return true;
}

} // namespace omachat::server
