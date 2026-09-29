#!/usr/bin/env python3
"""Offline import of DiscordChatExporter JSON channels into one OmaChat server."""

import argparse
import hashlib
import json
import mimetypes
import shutil
import sqlite3
import sys
import time
from datetime import datetime
from pathlib import Path
from urllib.parse import unquote, urlsplit


def require_id(value, label):
    value = str(value or "")
    if not value.isdecimal() or int(value) <= 0:
        raise ValueError(f"{label} must be a Discord decimal ID")
    return value


def timestamp(value, label):
    if not isinstance(value, str):
        raise ValueError(f"{label} has no timestamp")
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if parsed.tzinfo is None:
        raise ValueError(f"{label} timestamp has no timezone")
    return int(parsed.timestamp() * 1000)


def asset_path(export_file, url):
    if not url or not isinstance(url, str):
        return None
    parsed = urlsplit(url)
    if parsed.scheme or parsed.netloc or url.startswith(("/", "\\")):
        return None
    root = export_file.parent.resolve()
    candidate = (root / unquote(parsed.path)).resolve()
    if candidate.is_file() and candidate.is_relative_to(root):
        return candidate
    return None


def load_exports(paths):
    exports = []
    guild_id = None
    seen = set()
    for path in paths:
        with path.open(encoding="utf-8-sig") as stream:
            data = json.load(stream)
        if not isinstance(data, dict) or not isinstance(data.get("messages"), list):
            raise ValueError(f"{path}: expected a DiscordChatExporter JSON channel")
        guild = require_id(data.get("guild", {}).get("id"), "guild.id")
        channel = data.get("channel", {})
        channel_id = require_id(channel.get("id"), "channel.id")
        if guild_id is not None and guild_id != guild:
            raise ValueError("all exports must belong to the same Discord guild")
        if channel_id in seen:
            raise ValueError(f"channel {channel_id} appears in more than one export")
        guild_id = guild
        seen.add(channel_id)
        for message in data["messages"]:
            require_id(message.get("id"), "message.id")
            require_id(message.get("author", {}).get("id"), "author.id")
            timestamp(message.get("timestamp"), "message")
        exports.append((path, channel, data["messages"]))
    return exports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--db", required=True, type=Path, help="OmaChat SQLite database")
    parser.add_argument("--files", required=True, type=Path, help="OmaChat files directory")
    parser.add_argument("--server-id", required=True, type=int, help="existing OmaChat server ID")
    parser.add_argument("--apply", action="store_true", help="write changes (default: dry run)")
    parser.add_argument("exports", nargs="+", type=Path, help="DiscordChatExporter JSON channel files")
    args = parser.parse_args()
    exports = load_exports(args.exports)
    if not args.db.is_file():
        raise ValueError("database does not exist")
    db = sqlite3.connect(f"file:{args.db.resolve()}?mode={'rw' if args.apply else 'ro'}", uri=True)
    db.execute("PRAGMA foreign_keys=ON")
    version = db.execute("PRAGMA user_version").fetchone()[0]
    if version != 6:
        raise ValueError(f"database schema is {version}; start the updated server once to migrate it to 6")
    if not db.execute("SELECT 1 FROM servers WHERE id=?", (args.server_id,)).fetchone():
        raise ValueError("target OmaChat server does not exist")
    count = sum(len(messages) for _, _, messages in exports)
    print(f"{len(exports)} channel export(s), {count} message(s) -> OmaChat server {args.server_id}")
    if not args.apply:
        print("Dry run only. Stop omachat-server and repeat with --apply to import.")
        return

    copied = []
    now = int(time.time() * 1000)
    # OmaChat's epoch is 2025-01-01, and node 1023 is reserved for this
    # offline import. Advancing past existing ids also avoids a collision if
    # the server itself happens to use that node.
    next_id = max((max(now - 1735689600000, 0) << 22) | (1023 << 12), *(
        db.execute(f"SELECT COALESCE(MAX(id),0) FROM {table}").fetchone()[0]
        for table in ("users", "servers", "roles", "channels", "messages", "attachments")
    )) + 1

    def new_id():
        nonlocal next_id
        result = next_id
        next_id += 1
        if result >= 2**63:
            raise ValueError("OmaChat ID space exhausted")
        return result

    def mapped(kind, discord_id):
        row = db.execute("SELECT local_id FROM discord_import_map WHERE server_id=? AND kind=? AND discord_id=?",
                         (args.server_id, kind, discord_id)).fetchone()
        return row[0] if row else None

    def remember(kind, discord_id, local_id):
        db.execute("INSERT INTO discord_import_map(server_id,kind,discord_id,local_id) VALUES(?,?,?,?)",
                   (args.server_id, kind, discord_id, local_id))

    def author_id(author):
        did = require_id(author.get("id"), "author.id")
        existing = mapped("user", did)
        if existing:
            db.execute("INSERT OR IGNORE INTO members(server_id,user_id,joined_at) VALUES(?,?,?)",
                       (args.server_id, existing, now))
            return existing
        linked = db.execute("SELECT user_id FROM oauth_identities WHERE provider='discord' AND provider_user_id=?",
                            (did,)).fetchone()
        if linked:
            local = linked[0]
        else:
            archived = db.execute("SELECT id FROM users u WHERE username=? AND password_hash='' "
                                  "AND NOT EXISTS (SELECT 1 FROM oauth_identities o WHERE o.user_id=u.id)",
                                  (f"discord_{did}",)).fetchone()
            if archived:
                local = archived[0]
            else:
                local = new_id()
                name = str(author.get("nickname") or author.get("name") or f"Discord {did}")[:100]
                db.execute("INSERT INTO users(id,username,display_name,avatar_url,password_hash,created_at) "
                           "VALUES(?,?,?,?,?,?)", (local, f"discord_{did}", name, "", "", now))
        remember("user", did, local)
        db.execute("INSERT OR IGNORE INTO members(server_id,user_id,joined_at) VALUES(?,?,?)",
                   (args.server_id, local, now))
        return local

    try:
        db.execute("BEGIN IMMEDIATE")
        channel_ids = {}
        for _, channel, _ in exports:
            category_id = channel.get("categoryId")
            if category_id and str(category_id).isdecimal() and not mapped("category", str(category_id)):
                local = new_id()
                db.execute("INSERT INTO channels(id,server_id,name,type,parent_id,position,topic,created_at) "
                           "VALUES(?,?,?,?,?,?,?,?)", (local, args.server_id,
                           str(channel.get("category") or "Imported channels")[:64], 2, 0, local % 1000000, "", now))
                remember("category", str(category_id), local)
        for _, channel, _ in exports:
            did = str(channel["id"])
            local = mapped("channel", did)
            if not local:
                local = new_id()
                parent = mapped("category", str(channel.get("categoryId"))) or 0
                name = str(channel.get("name") or f"discord-{did}").strip().lower().replace(" ", "-")[:64]
                db.execute("INSERT INTO channels(id,server_id,name,type,parent_id,position,topic,created_at) "
                           "VALUES(?,?,?,?,?,?,?,?)", (local, args.server_id, name, 0, parent,
                           local % 1000000, str(channel.get("topic") or ""), now))
                remember("channel", did, local)
            channel_ids[did] = local

        imported = 0
        for export_file, channel, messages in exports:
            cid = channel_ids[str(channel["id"])]
            for message in sorted(messages, key=lambda m: (timestamp(m["timestamp"], "message"), int(m["id"]))):
                did = str(message["id"])
                if mapped("message", did):
                    continue
                mid = new_id()
                uid = author_id(message["author"])
                content = str(message.get("content") or "")
                attachments = []
                for attachment in message.get("attachments", []):
                    url = attachment.get("url")
                    source = asset_path(export_file, url)
                    if source:
                        aid = new_id()
                        target = args.files / str(aid)
                        if target.exists():
                            raise ValueError(f"attachment target already exists: {target}")
                        args.files.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(source, target)
                        copied.append(target)
                        digest = hashlib.sha256(target.read_bytes()).digest()
                        filename = Path(str(attachment.get("fileName") or source.name)).name[:255]
                        attachments.append((aid, cid, uid, mid, filename,
                                            mimetypes.guess_type(filename)[0] or "application/octet-stream",
                                            target.stat().st_size, digest,
                                            timestamp(message["timestamp"], "message")))
                    elif url:
                        content += f"\n{url}"
                for embed in message.get("embeds", []):
                    if isinstance(embed, dict):
                        for key in ("url", "description"):
                            value = embed.get(key)
                            if isinstance(value, str) and value and value not in content:
                                content += f"\n{value}"
                for sticker in message.get("stickers", []):
                    if isinstance(sticker, dict) and sticker.get("sourceUrl"):
                        content += f"\n{sticker['sourceUrl']}"
                edited = timestamp(message["timestampEdited"], "edit") if message.get("timestampEdited") else 0
                mentions = []
                for mention in message.get("mentions", []):
                    if isinstance(mention, dict) and mention.get("id"):
                        mentions.append(str(author_id(mention)))
                db.execute("INSERT INTO messages(id,channel_id,author_id,content,edited_at,created_at) "
                           "VALUES(?,?,?,?,?,?)", (mid, cid, uid, content, edited,
                           timestamp(message["timestamp"], "message")))
                if mentions:
                    db.execute("UPDATE messages SET mentions=? WHERE id=?", (" ".join(dict.fromkeys(mentions)), mid))
                db.executemany("INSERT INTO attachments(id,channel_id,uploader_id,message_id,filename,mime_type,"
                               "size,sha256,created_at) VALUES(?,?,?,?,?,?,?,?,?)", attachments)
                for reaction in message.get("reactions", []):
                    emoji = reaction.get("emoji") or {}
                    symbol = emoji.get("code") or emoji.get("name")
                    if not isinstance(symbol, str) or not symbol:
                        continue
                    for user in reaction.get("users", []):
                        if isinstance(user, dict) and user.get("id"):
                            db.execute("INSERT OR IGNORE INTO reactions(message_id,user_id,emoji) VALUES(?,?,?)",
                                       (mid, author_id(user), symbol))
                remember("message", did, mid)
                imported += 1
        # A second pass resolves replies even when their source messages were
        # in another file or appeared later in the export.
        for _, _, messages in exports:
            for message in messages:
                ref = message.get("reference") or {}
                reply = mapped("message", str(ref.get("messageId"))) if ref.get("messageId") else None
                if reply:
                    db.execute("UPDATE messages SET reply_to=? WHERE id=?", (reply, mapped("message", str(message["id"]))))
        db.commit()
        print(f"Imported {imported} new message(s). Restart omachat-server to load the channels and authors.")
    except Exception:
        db.rollback()
        for path in copied:
            path.unlink(missing_ok=True)
        raise
    finally:
        db.close()


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, sqlite3.Error, json.JSONDecodeError) as error:
        print(f"import-discord: {error}", file=sys.stderr)
        sys.exit(1)
