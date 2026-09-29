# Import Discord channels

## In the client

On an updated OmaChat server, open **Add Server → Create a server → Import
Discord**. Enter the new server name, choose the JSON channel exports, and
select **Create and import**. The dialog checks the files before creating the
server, then shows batch progress. The client transfers the history and local
assets over its existing encrypted connection; no access to the server's
filesystem is needed. The account creating the server owns it. Imported
authors remain separate archive profiles; an export cannot claim authorship
on a live OmaChat account.

Select exports from one Discord guild. Each JSON file may be up to 128 MB;
the selection may total 256 MB and 100 files. Local attachments must be beside
their export, no larger than 4 MB each, and no more than 10 per message. If a
local asset is missing or exceeds those limits, the client reports the issue
before it creates the server. Remote attachment URLs remain links in the
message. A failed transfer may leave a partially imported server; imported
Discord message IDs are tracked so the remaining history can be retried.

## Offline operator tool

OmaChat can also import **DiscordChatExporter JSON** channel exports into an existing
OmaChat server. This is an offline operator tool: it edits the server's SQLite
database and attachment directory, so stop `omachat-server` before applying an
import and start it afterward. Back up both the database and files directory.

Export each Discord text channel as JSON. Enable **Download assets** if you
want attachments copied into OmaChat. Keep each JSON file beside its downloaded
assets. You can pass several channel files in one command; they must come from
the same Discord guild. The importer creates categories and text channels,
stores original message dates and edits, and links replies across the supplied
files. It creates archival, passwordless OmaChat profiles for authors who have
not already linked their Discord account to an OmaChat account. Repeating the
same import skips messages already mapped from their Discord IDs.

First start the updated server once to migrate its database to schema 6, then
stop it. Find the OmaChat server ID with `omachatctl server list`. Run a dry run:

```sh
python3 scripts/import-discord.py \
  --db /var/lib/omachat/omachat.db \
  --files /var/lib/omachat/files \
  --server-id 123456789 \
  exports/channel-one.json exports/channel-two.json
```

Repeat with `--apply` to write the import, then restart `omachat-server`.
Use your configured database and files paths if they differ from the examples.
For Docker, run the script where both volume paths and export files are
available, while the container is stopped.

Both paths import text channel history, authors, dates, edits, reply
links, mentions, reaction users when present, and downloaded attachments.
Remote attachment URLs are retained in message text when no local asset is
available; the importer does not fetch them. Embed descriptions and links and
sticker URLs are appended to text. Discord voice channels, roles, permission
overrides, pins, polls, and exact reaction counts without user lists have no
matching import representation yet. Thread exports become text channels.
Archived profiles do not grant their owners access to OmaChat; only already
linked Discord identities map to live OmaChat accounts.

Use exports produced by [DiscordChatExporter](https://github.com/Tyrrrz/DiscordChatExporter),
which provides JSON output and an option to download assets. Keep any account
token used for export out of the OmaChat import command and logs.
