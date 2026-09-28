#include "omachat/core/Paths.hpp"

#include <QDir>
#include <QFile>

#include <sys/stat.h>
#include <unistd.h>

namespace omachat::paths {
namespace {

QString home()
{
    return QDir::homePath();
}

QString fromEnv(const char* name, const QString& fallback)
{
    const QString value = qEnvironmentVariable(name);
    // The XDG spec says relative paths are invalid and must be ignored.
    if (!value.isEmpty() && QDir::isAbsolutePath(value))
        return value;
    return fallback;
}

} // namespace

QString configDir()
{
    return fromEnv("XDG_CONFIG_HOME", home() + QStringLiteral("/.config")) + QStringLiteral("/omachat");
}

QString dataDir()
{
    return fromEnv("XDG_DATA_HOME", home() + QStringLiteral("/.local/share")) + QStringLiteral("/omachat");
}

QString cacheDir()
{
    return fromEnv("XDG_CACHE_HOME", home() + QStringLiteral("/.cache")) + QStringLiteral("/omachat");
}

QString runtimeDir()
{
    const QString runtime = fromEnv("XDG_RUNTIME_DIR", QString());
    if (!runtime.isEmpty())
        return runtime + QStringLiteral("/omachat");
    return QStringLiteral("/tmp/omachat-%1").arg(::getuid());
}

QString configFile()
{
    return configDir() + QStringLiteral("/config.toml");
}

QString databaseFile()
{
    return dataDir() + QStringLiteral("/omachat.db");
}

QString socketPath()
{
    const QString overridePath = qEnvironmentVariable("OMACHAT_SOCKET");
    if (!overridePath.isEmpty())
        return overridePath;
    return runtimeDir() + QStringLiteral("/omachat.sock");
}

bool ensurePrivateDir(const QString& path)
{
    QDir dir(path);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;
    struct stat st{};
    const QByteArray native = QFile::encodeName(path);
    if (::stat(native.constData(), &st) != 0 || !S_ISDIR(st.st_mode))
        return false;
    // Never touch permissions of directories we do not own (e.g. /tmp when
    // OMACHAT_SOCKET points there); the socket itself is 0600 regardless.
    if (st.st_uid != ::getuid())
        return true;
    return (st.st_mode & 0777) == 0700 || ::chmod(native.constData(), 0700) == 0;
}

} // namespace omachat::paths
