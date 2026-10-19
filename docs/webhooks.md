# Incoming webhooks

P0-09 delivers inbound text into existing community text channels. No external
service is connected automatically. DMs, encrypted conversations, attachments,
rich embeds and a distinct feed channel type are outside this slice.

The listener is disabled by default. Operators opt in using server TOML:

```toml
[webhooks]
enabled = true
bind = "127.0.0.1"
port = 6475
```

Container/systemd operators can override these values with
`OMACHAT_WEBHOOKS_ENABLED`, `OMACHAT_WEBHOOKS_BIND`, and `OMACHAT_WEBHOOKS_PORT`.
Unset or empty environment values preserve TOML. Enabled accepts the ordinary
true/false, yes/no, on/off and 1/0 spellings; port must be 1–65535 and bind must
be a numeric IPv4/IPv6 address. Invalid values fail startup. No public port is
opened by changing the example configuration.

This HTTP listener is intended behind an HTTPS reverse proxy. Keep it on loopback
unless the deployment provides a trusted private transport. Preserve the complete
path, disable access logging of `/hooks/` paths (they contain credentials), reject
oversized bodies at the proxy too, and expose only the ingress route. Do not put
credentials in screenshots, source control or issue reports.

Clients discover `incoming.webhooks.v1` when ingress is enabled. Over the normal
TLS authenticated protocol, `create_webhook(channel_id,name)` returns
`incoming_webhook(id,channel_id,name,token)`. The 256-bit random base64url token is
returned only once; only its SHA-256 digest is stored. `list_webhooks(channel_id)`
returns metadata without tokens. `revoke_webhook(channel_id,webhook_id)` permanently
deletes that credential, is idempotent and cannot affect another channel. Create,
list and revoke require current ViewChannel + ManageChannel on a community text
channel; create also requires SendMessages. Maximum eight webhooks per channel;
names are 1–64 printable characters. To rotate, revoke then create a new credential.
Deleting a channel or creator cascades its credentials. Existing delivered messages
retain provenance independently of the credential.

Send one HTTP/1.1 request:

```http
POST /hooks/<token> HTTP/1.1
Host: your-server
Content-Type: application/json
Content-Length: <body byte length>

{"content":"Build failed: inspect the CI logs."}
```

Only `content` is accepted, as a JSON string; extra keys reject the request.
The message passes ordinary text validation and the combined attribution prefix
plus text must fit the normal message length. Sender names cannot be overridden.
Each delivery rechecks the creator's current ViewChannel, SendMessages and
ManageChannel permissions. Revoked, removed, or inaccessible credentials cannot
post. Integration text never creates mention reasons. Ordinary unread and eligible All-message notifications still apply, including to the credential creator. Integration messages cannot be edited into mentions.

Delivered messages persist `integration_id` and `integration_name` and include a
literal `[Integration: name]` prefix for older clients. The underlying author ID
is the creator for existing ownership and database compatibility; clients should
prefer the integration label when integration_id is nonzero. Normal moderation,
history, search and live delivery apply. No credential is included in messages.

Bounds: 32 active connections; five-second absolute request timeout; 8 KiB headers;
16 KiB body; 24 KiB total buffered request; one request per connection, always
closed afterward. Duplicate headers, transfer encoding, Expect, malformed lengths,
extra bytes/pipelining and non-JSON media types are rejected. Process-wide token
bucket: burst 60/refill 10 per second, including invalid credentials. Per integration:
burst 5/refill one per two seconds. At most 4096 integration rate buckets are kept;
new identities fail closed with 503 when full. Rate state is process-local and
resets on restart; deployments can add a persistent edge rate limit.

HTTP status: 201 accepted; 400 invalid request/payload; 401 unknown/revoked token;
403 current creator permissions do not authorize delivery; 413 body too large;
415 media type unsupported; 429 rate limited; 431 headers too large; 500 storage
failure; 503 capacity reached. No automatic retries or deduplication are provided;
a lost successful response leaves delivery uncertain. Check channel history before
retrying. Responses carry `Cache-Control: no-store`.

Storage schema 17 adds credentials and immutable message attribution. Schema 16
in the same continuation adds mention classification. Back up before upgrading;
older servers reject the newer schema. This document describes local source
behavior; it does not claim hosted deployment or an external-provider test.

For the Git-based Coolify Compose deployment, the three environment references
ship disabled by default. To opt in, set `OMACHAT_WEBHOOKS_ENABLED=true` and
`OMACHAT_WEBHOOKS_BIND=0.0.0.0` on the private Compose network. Keep port 6475
unpublished; set the service's managed HTTPS domain to
`https://your-host:6475` (6475 selects the internal destination, public TLS stays
on 443). Preserve `/hooks/` rather than stripping its prefix. Verify proxy/CDN
access logging and tracing do not retain credential paths before using real
tokens. The deployment connector in this continuation can deploy/read but its
credential rejects configuration REST calls with 403; no public route is claimed.

Before upgrading any existing schema 1–16 file database, server startup now creates
a unique 0600 sibling backup using SQLite VACUUM INTO, opens it independently
read-only to verify integrity and the original schema version, and synchronizes
both file and directory. Backup failure aborts migration before schema changes;
a verified backup is retained even if a later migration fails. Fresh, current
and memory databases skip this gate. Keep these backups through upgrade acceptance;
there is no automatic backup deletion.
