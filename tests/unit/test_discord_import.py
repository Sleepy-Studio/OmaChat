"""Exercise the offline importer with an export and a minimal schema 6 DB."""

import json
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "import-discord.py"


class DiscordImportTest(unittest.TestCase):
    def test_dates_replies_attachments_and_repeat_import(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            db_path = root / "chat.db"
            files = root / "files"
            export = root / "channel.json"
            (root / "assets").mkdir()
            (root / "assets" / "photo.png").write_bytes(b"image bytes")
            with sqlite3.connect(db_path) as db:
                db.executescript("""
                    PRAGMA user_version=6;
                    CREATE TABLE servers(id INTEGER PRIMARY KEY);
                    CREATE TABLE users(id INTEGER PRIMARY KEY,username TEXT UNIQUE,display_name TEXT,
                        avatar_url TEXT,password_hash TEXT,created_at INTEGER);
                    CREATE TABLE oauth_identities(provider TEXT,provider_user_id TEXT,user_id INTEGER);
                    CREATE TABLE members(server_id INTEGER,user_id INTEGER,joined_at INTEGER,
                        PRIMARY KEY(server_id,user_id));
                    CREATE TABLE roles(id INTEGER PRIMARY KEY);
                    CREATE TABLE channels(id INTEGER PRIMARY KEY,server_id INTEGER,name TEXT,type INTEGER,
                        parent_id INTEGER,position INTEGER,topic TEXT,created_at INTEGER);
                    CREATE TABLE messages(id INTEGER PRIMARY KEY,channel_id INTEGER,author_id INTEGER,
                        content TEXT,reply_to INTEGER DEFAULT 0,edited_at INTEGER DEFAULT 0,
                        mentions TEXT DEFAULT '',created_at INTEGER);
                    CREATE TABLE attachments(id INTEGER PRIMARY KEY,channel_id INTEGER,uploader_id INTEGER,
                        message_id INTEGER,filename TEXT,mime_type TEXT,size INTEGER,sha256 BLOB,created_at INTEGER);
                    CREATE TABLE reactions(message_id INTEGER,user_id INTEGER,emoji TEXT,
                        PRIMARY KEY(message_id,user_id,emoji));
                    CREATE TABLE discord_import_map(server_id INTEGER,kind TEXT,discord_id TEXT,local_id INTEGER,
                        PRIMARY KEY(server_id,kind,discord_id));
                    INSERT INTO servers(id) VALUES(42);
                """)
            author = {"id": "111", "name": "Alice", "nickname": "Alice"}
            export.write_text(json.dumps({
                "guild": {"id": "7", "name": "Old place"},
                "channel": {"id": "9", "name": "general", "type": "GuildTextChat",
                            "categoryId": "8", "category": "Discussion", "topic": "Old topic"},
                "messages": [
                    {"id": "12", "timestamp": "2020-01-02T03:04:06+00:00", "author": author,
                     "content": "reply", "reference": {"messageId": "11"}, "attachments": []},
                    {"id": "11", "timestamp": "2020-01-02T03:04:05+00:00", "author": author,
                     "content": "hello", "attachments": [{"url": "assets/photo.png",
                     "fileName": "photo.png"}]},
                ],
            }), encoding="utf-8")
            cmd = [sys.executable, str(SCRIPT), "--db", str(db_path), "--files", str(files),
                   "--server-id", "42", str(export)]
            dry = subprocess.run(cmd, capture_output=True, text=True, check=True)
            self.assertIn("Dry run", dry.stdout)
            with sqlite3.connect(db_path) as db:
                self.assertEqual(db.execute("SELECT COUNT(*) FROM messages").fetchone()[0], 0)
            for _ in range(2):
                subprocess.run(cmd[:2] + ["--apply"] + cmd[2:], capture_output=True, text=True, check=True)
            with sqlite3.connect(db_path) as db:
                rows = db.execute("SELECT id,content,reply_to,created_at FROM messages ORDER BY created_at").fetchall()
                self.assertEqual(len(rows), 2)
                self.assertEqual(rows[1][2], rows[0][0])
                self.assertEqual(rows[0][3], 1577934245000)
                self.assertEqual(db.execute("SELECT COUNT(*) FROM channels").fetchone()[0], 2)
                attachment = db.execute("SELECT id,filename FROM attachments").fetchone()
                self.assertEqual(attachment[1], "photo.png")
                self.assertEqual((files / str(attachment[0])).read_bytes(), b"image bytes")


if __name__ == "__main__":
    unittest.main()
