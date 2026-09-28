# Self-hosting omachat-server

The server is a single binary with an SQLite database. No reverse proxy,
container or external service is required.

## 1. Install

On Arch, build the package (`packaging/arch/PKGBUILD`) or install from a
source build (`cmake --install build`). The package ships:

- `/usr/bin/omachat-server`
- `/usr/lib/systemd/system/omachat-server.service` (runs as user `omachat`)
- `/usr/lib/sysusers.d/omachat-server.conf`
- `/usr/share/doc/omachat/server.toml.example`

```bash
sudo systemd-sysusers                      # creates the omachat user
sudo install -d -o omachat -g omachat -m 750 /var/lib/omachat
sudo install -Dm644 /usr/share/doc/omachat/server.toml.example /etc/omachat/server.toml
```

## 2. TLS certificate

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

## 3. Configure `/etc/omachat/server.toml`

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

## 4. Firewall

Open **TCP 6473** and **UDP 6474** (or your configured ports). Voice needs
the UDP port reachable; clients behind NAT keep their mapping alive
automatically.

## 5. Run

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
