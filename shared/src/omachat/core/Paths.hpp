#pragma once

#include <QString>

namespace omachat::paths {

// XDG base directories with OmaChat's application subdirectory appended.
// Each honours the corresponding XDG_* variable and falls back to the
// specification defaults. None of these functions create directories.
QString configDir(); // $XDG_CONFIG_HOME/omachat
QString dataDir(); // $XDG_DATA_HOME/omachat
QString cacheDir(); // $XDG_CACHE_HOME/omachat
QString runtimeDir(); // $XDG_RUNTIME_DIR/omachat (falls back to /tmp/omachat-$UID)

QString configFile(); // configDir()/config.toml
QString databaseFile(); // dataDir()/omachat.db
QString socketPath(); // runtimeDir()/omachat.sock (override: OMACHAT_SOCKET)

// Creates `path` (and parents) if needed and restricts it to mode 0700 when
// owned by the current user. Directories owned by others are left untouched.
bool ensurePrivateDir(const QString& path);

} // namespace omachat::paths
