#!/usr/bin/env bash
# Bootstraps a config and a self-signed certificate on first run so
# `docker run` works with zero manual setup, then execs omachat-server.
# Everything lands in /var/lib/omachat (the mounted volume), so a restart
# reuses what was generated instead of creating a new identity or database.
set -euo pipefail

DATA_DIR=/var/lib/omachat
CONFIG_PATH="$DATA_DIR/server.toml"
CERT_PATH="$DATA_DIR/cert.pem"
KEY_PATH="$DATA_DIR/key.pem"

# All optional; sane defaults make the bare `docker run` case work.
: "${OMACHAT_NAME:=OmaChat}"
: "${OMACHAT_HOSTNAME:=localhost}"   # what clients connect to; goes in the cert
: "${OMACHAT_PORT:=6473}"
: "${OMACHAT_MEDIA_PORT:=6474}"
: "${OMACHAT_REGISTRATION_OPEN:=true}"
: "${OMACHAT_NODE_ID:=1}"
: "${OMACHAT_MAX_UPLOAD_MB:=50}"
: "${OMACHAT_LOG_LEVEL:=info}"
# OAuth sign-in: all optional and unset by default. Each provider needs both
# its client id and secret to do anything; see server.toml.example for where
# to register an app with each one.
: "${OMACHAT_OAUTH_DISCORD_CLIENT_ID:=}"
: "${OMACHAT_OAUTH_DISCORD_CLIENT_SECRET:=}"
: "${OMACHAT_OAUTH_GITHUB_CLIENT_ID:=}"
: "${OMACHAT_OAUTH_GITHUB_CLIENT_SECRET:=}"
: "${OMACHAT_OAUTH_GOOGLE_CLIENT_ID:=}"
: "${OMACHAT_OAUTH_GOOGLE_CLIENT_SECRET:=}"

if [[ ! -f "$CONFIG_PATH" ]]; then
    echo "omachat-entrypoint: writing $CONFIG_PATH (first run)"
    cat >"$CONFIG_PATH" <<EOF
# Generated on first run from OMACHAT_* environment variables. Delete this
# file (or edit it directly) and restart the container to regenerate it;
# an existing file is never overwritten.
[server]
name = "$OMACHAT_NAME"
bind = "0.0.0.0"
port = $OMACHAT_PORT
registration_open = $OMACHAT_REGISTRATION_OPEN
node_id = $OMACHAT_NODE_ID

[media]
udp_port = $OMACHAT_MEDIA_PORT

[database]
type = "sqlite"
path = "$DATA_DIR/omachat.db"

[tls]
certificate = "$CERT_PATH"
private_key = "$KEY_PATH"

[files]
path = "$DATA_DIR/files"
max_upload_mb = $OMACHAT_MAX_UPLOAD_MB

[log]
level = "$OMACHAT_LOG_LEVEL"
EOF
fi

# Appends an [oauth.PROVIDER] section the first time credentials for it are
# supplied, even to a config.toml left over from before this existed or from
# an earlier run without them set — but never touches one already there, so
# hand edits (or rotating just the secret by editing the file directly)
# survive a restart.
add_oauth_section() {
    local provider="$1" client_id="$2" client_secret="$3"
    [[ -z "$client_id" || -z "$client_secret" ]] && return 0
    grep -q "^\[oauth\.$provider\]" "$CONFIG_PATH" && return 0
    echo "omachat-entrypoint: enabling OAuth sign-in for $provider"
    cat >>"$CONFIG_PATH" <<EOF

[oauth.$provider]
client_id = "$client_id"
client_secret = "$client_secret"
EOF
}
add_oauth_section discord "$OMACHAT_OAUTH_DISCORD_CLIENT_ID" "$OMACHAT_OAUTH_DISCORD_CLIENT_SECRET"
add_oauth_section github "$OMACHAT_OAUTH_GITHUB_CLIENT_ID" "$OMACHAT_OAUTH_GITHUB_CLIENT_SECRET"
add_oauth_section google "$OMACHAT_OAUTH_GOOGLE_CLIENT_ID" "$OMACHAT_OAUTH_GOOGLE_CLIENT_SECRET"

if [[ ! -f "$CERT_PATH" || ! -f "$KEY_PATH" ]]; then
    echo "omachat-entrypoint: generating a self-signed certificate for '$OMACHAT_HOSTNAME'"
    echo "omachat-entrypoint: to use a real certificate instead, mount it at $CERT_PATH / $KEY_PATH"
    /usr/bin/omachat-server generate-cert --cert "$CERT_PATH" --key "$KEY_PATH" --name "$OMACHAT_HOSTNAME"
    echo "omachat-entrypoint: give clients the fingerprint printed above to trust this server"
fi

exec /usr/bin/omachat-server --config "$CONFIG_PATH" "$@"
