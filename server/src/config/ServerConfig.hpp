#pragma once

#include <QString>

#include <cstdint>

namespace omachat::server {

struct ServerConfig {
    QString instanceName = QStringLiteral("OmaChat");
    QString bind = QStringLiteral("0.0.0.0");
    std::uint16_t port = 6473;
    bool registrationOpen = true;
    std::uint32_t nodeId = 1; // snowflake node (0..1023)

    std::uint16_t mediaPort = 6474;
    QString mediaBind; // empty = same as bind
    int voiceBitrate = 40000;

    QString databasePath = QStringLiteral("/var/lib/omachat/omachat.db");

    QString tlsCertificate; // PEM; required
    QString tlsPrivateKey; // PEM; required

    QString filesPath = QStringLiteral("/var/lib/omachat/files");
    int maxUploadMb = 50;

    int accessTokenMinutes = 15;
    int refreshTokenDays = 30;
    int maxConnectionsPerIp = 16;

    QString logLevel = QStringLiteral("info");

    // Loads TOML. Unknown keys are ignored; invalid values produce an error.
    static bool load(const QString& path, ServerConfig& out, QString* error);
};

} // namespace omachat::server
