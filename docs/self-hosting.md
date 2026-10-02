# Self-hosting omachat-server

The server is a single binary with an SQLite database. No reverse proxy or
external service is required.

## Docker (fastest)

```bash
docker run -d --name omachat-server --restart unless-stopped \
    -p 6473:6473/tcp -p 6474:6474/udp \
    -v omachat-data:/var/lib/omachat \
    -e OMACHAT_HOSTNAME=chat.example.org \
    ghcr.io/sleepy-studio/omachat-server:latest
docker logs omachat-server   # prints the self-signed certificate fingerprint
```

Or with the `docker-compose.yml` in the repo: `docker compose up -d`. On
first run the container generates `/var/lib/omachat/server.toml` from
`OMACHAT_*` environment variables and a self-signed certificate for
`OMACHAT_HOSTNAME` — set that to whatever your clients will actually type
as the server address. Both persist in the `omachat-data` volume, so
restarting the container does not regenerate them; edit
`server.toml` inside the volume directly for anything the environment
variables don't cover (see the full key list in step 3 below), then
restart the container.

Environment variables: `OMACHAT_NAME`, `OMACHAT_HOSTNAME`, `OMACHAT_PORT`
(6473), `OMACHAT_MEDIA_PORT` (6474), `OMACHAT_REGISTRATION_OPEN` (true),
`OMACHAT_NODE_ID` (1), `OMACHAT_MAX_UPLOAD_MB` (50), `OMACHAT_LOG_LEVEL`
(info), and the OAuth pairs below. To use a real certificate instead of the
generated self-signed one, mount it at `/var/lib/omachat/cert.pem` and
`/var/lib/omachat/key.pem` before first start.

**Sign in with Discord/GitHub/Google**: set
`OMACHAT_OAUTH_DISCORD_CLIENT_ID`/`_SECRET`,
`OMACHAT_OAUTH_GITHUB_CLIENT_ID`/`_SECRET`, and/or
`OMACHAT_OAUTH_GOOGLE_CLIENT_ID`/`_SECRET`. A provider is only enabled once
both its id and secret are set; unlike the rest of `server.toml`, these are
appended to an *existing* config too on container start (so turning one on
later, or on a volume from before this existed, doesn't need a manual edit).
See [Optional: sign in with Discord, GitHub or Google](#optional-sign-in-with-discord-github-or-google)
below for how to register each provider's app.

Skip to [Firewall](#4-firewall) below — TLS and config are already done.

## Coolify

Create a Git-based **Docker Compose** application from this repository. Set
**Base Directory** to `/` and **Docker Compose Location** to
`/docker-compose.coolify.yml`. The Compose file pulls the published
`ghcr.io/sleepy-studio/omachat-server` image; Coolify does not build it.
Its `OMACHAT_IMAGE_TAG` variable defaults to `latest` and can be set to a
published version tag for a controlled upgrade.

Coolify automatically generates `SERVICE_FQDN_OMACHAT_SERVER` for this Compose
service and passes that hostname to OmaChat as `OMACHAT_HOSTNAME`. To use your
own domain, first configure the server's Wildcard Domain in Coolify and point
wildcard DNS at the server; Coolify cannot create DNS records from Compose.
Without a wildcard domain, Coolify can generate an `sslip.io` hostname from
the server IP. Check that the resulting hostname resolves to the server before
handing it to clients. A generated Coolify web URL is not an OmaChat endpoint:
clients connect to the hostname on TCP port 6473 (or your chosen port), while
media uses UDP port 6474 (or your chosen port). The Coolify HTTP proxy route
associated with the generated domain is unused and cannot terminate OmaChat's
TLS or carry its UDP traffic. Do not append `:6473` to Coolify's **Domains for
omachat-server** field: a port there selects an internal HTTP proxy target,
not the public client port. Coolify may warn that it is an unrecognized
internal port; that warning does not describe the Compose `ports:` mappings.
In the OmaChat client, enter only the hostname in **Server** and `6473` in
**Port**.

Review the environment defaults for server name, TCP and UDP ports,
registration, node ID, upload limit, and log level. Open the selected TCP and
UDP ports on the server firewall. The Compose health check probes the TLS
listener and Coolify displays the named data volume under Persistent Storage.

The image must be public in GHCR for an unauthenticated pull, or the Coolify
server must have registry credentials. The image was initially published as a
private org package; check its visibility before the first deployment. The
first boot writes `server.toml` and a certificate into the persistent volume.
Later changes to `OMACHAT_*` variables do not rewrite that config or
certificate: edit the stored files (or deliberately regenerate them) when
changing server settings or the certificate identity. Keep the volume when
redeploying or upgrading.

## Native (systemd), the alternative to Docker

### 1. Install

On Arch, `packaging/arch/PKGBUILD` (or `scripts/install.sh`, or the AUR
package once published — see the README) installs the server alongside the
client. Building from source instead: `cmake --install build`. Either way
you get:

- `/usr/bin/omachat-server`
- `/usr/lib/systemd/system/omachat-server.service` (runs as user `omachat`)
- `/usr/lib/sysusers.d/omachat-server.conf`
- `/usr/share/doc/omachat/server.toml.example`

```bash
sudo systemd-sysusers                      # creates the omachat user
sudo install -d -o omachat -g omachat -m 750 /var/lib/omachat
sudo install -Dm644 /usr/share/doc/omachat/server.toml.example /etc/omachat/server.toml
```

### 2. TLS certificate

Any of these work:

- **Public CA** (e.g. Let's Encrypt): point `tls.certificate` at the full
  chain and `tls.private_key` at the key. Clients connect without prompts.
- **Private CA**: clients that trust your CA connect without prompts.
- **Self-signed**:

  ```bash
  sudo -u omachat omachat-server generate-cert \
      --cert /var/lib/omachat/cert.pem --key /var/lib/omachat/key.pem \
      --name chat.example.org --name 203.0.113.10
  ```

  It prints the SHA-256 fingerprint. Give that to your users: OmaChat shows
  the server's fingerprint on first connection and asks them to trust it.
  The server log also prints it at startup (`tls identity loaded`).

### 3. Configure `/etc/omachat/server.toml`

```toml
[server]
name = "My Community"
bind = "0.0.0.0"
port = 6473
registration_open = true   # turn off after everyone has registered
node_id = 1

[media]
udp_port = 6474

[database]
type = "sqlite"
path = "/var/lib/omachat/omachat.db"

[tls]
certificate = "/var/lib/omachat/cert.pem"
private_key = "/var/lib/omachat/key.pem"
```

Relative paths are resolved against the config file's directory. Other keys:
`media.bind`, `media.voice_bitrate`, `auth.access_token_minutes`,
`auth.refresh_token_days`, `limits.max_connections_per_ip`, `log.level`.

### Optional: sign in with Discord, GitHub or Google

Add a `[oauth.discord]`, `[oauth.github]` and/or `[oauth.google]` section
with `client_id` and `client_secret` from that provider's developer console.
OmaChat only advertises providers you configure; leave a section out to keep
it off. See `server.toml.example` for the exact keys.

When registering the app on each provider's side, set its redirect/callback
URI to exactly:

```
http://127.0.0.1:34579/callback
```

This is a fixed port on the *end user's own machine* (not your server) —
the client runs a short-lived local listener there during sign-in and it
never leaves 127.0.0.1. It has to be an exact, predictable value because
Discord and GitHub validate the redirect URI with an exact string match,
port included; only Google has documented support for any loopback port,
but using the same fixed one everywhere keeps setup simple. Register the app
as an "installed app" / "desktop app" / "public client" type where the
provider distinguishes one (no client secret exposed to end users, PKCE
required). Signing in this way creates an account automatically the first
time, with no password set.

## 4. Firewall

Open **TCP 6473** and **UDP 6474** (or your configured ports). Voice needs
the UDP port reachable; clients behind NAT keep their mapping alive
automatically.

### 5. Run

```bash
sudo systemctl enable --now omachat-server
journalctl -u omachat-server -f
```

Without systemd: `omachat-server --config /path/to/server.toml`
(SIGINT/SIGTERM shut down cleanly).

## 6. First use

1. In OmaChat choose **Register**, enter your server's host and port.
2. Trust the certificate fingerprint if it is self-signed.
3. Create a server (the `+` in the server rail) and copy an invite
   (`/invite`, or the people icon). Invites look like
   `omachat://invite/Z3fkn_JvwEh8` and default to 7 days, unlimited uses.

From a terminal: `omachatctl account register chat.example.org:6473 you`,
`omachatctl server create "My Community"`,
`omachatctl invite create "My Community" --max-uses 10`.

### Instance operator console

Register your own account first. Then assign it as operator with environment
variables and restart the service:

```yaml
OMACHAT_OPERATOR_USER_ID: "123456789"
OMACHAT_OPERATOR_REMOTE_RESTART: "true"
```

The id is your numeric server user id — find it with
`omachatctl status --json | jq .user.id`. Ids are used instead of usernames
because they are stable. In Coolify these are plain environment variables on
the service; with `docker run` pass `-e OMACHAT_OPERATOR_USER_ID=...`; with
systemd add `Environment=...` to a unit override. The variables are honored
on every start and take precedence over `server.toml`, so no persisted-file
editing is needed. The TOML equivalent (`[operator]` section in
`/var/lib/omachat/server.toml`, see `server.toml.example`) still works and
is used when the variables are unset or empty.

`user_id` must be an existing account's id. If it is not, startup fails rather
than silently assigning the privilege to a later registrant. Leave
`remote_restart` off unless systemd, Docker, or another supervisor is
configured to restart a process that exits with a nonzero status. The packaged
systemd unit and Compose files support this.

Log in with that account in the normal client. The account menu then shows
**Instance console**. It reports uptime, connected sessions, users, messages,
communities, recent process logs and operator actions. It can open or close
registration, suspend or restore accounts, moderate members in any community,
delete a community, and request a supervised restart. Suspensions revoke all
sessions and block password, OAuth, refresh, and resume login. The operator
cannot suspend or delete their own account through the client; remove the
assignment from the config first if the identity must change. The console
does not expose a host shell, files, secrets, or direct message contents.

## Backups

Server state is the SQLite database (WAL mode) plus the attachment files.
Back up the database with
`sqlite3 /var/lib/omachat/omachat.db ".backup /backup/omachat.db"` while
running, then copy `/var/lib/omachat/files/` (files are write-once, so
`rsync` is enough), plus your TLS key. Take the database copy first: a file
without a database row is swept on the next start, while a row whose file
is missing only makes that one attachment fail to download.

## Upgrades

The schema version is stored in the database (`PRAGMA user_version`) and
migrated forward on start. A server refuses to open a database written by a
newer version.

## Mobile-compatible server changes

The Android feature branch adds migration 14 and capability-negotiated durable
message retries, per-account read state and explicit voice ownership. Back up the
SQLite database before upgrade. Deduplication records persist until account deletion;
deleted message tombstones prevent retry resurrection. A persistent public instance
UUID helps phones detect a replaced database at the same address. Old clients remain
compatible with additive wire fields. See [mobile protocol changes](mobile-protocol-changes.md).

No push gateway, FCM registration, or OAuth mobile redirect support has been
implemented yet. No Firebase project credentials are required for the current
foreground-only text client. The hosted server has not been redeployed by this work.
