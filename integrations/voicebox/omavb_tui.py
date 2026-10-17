#!/usr/bin/env python3
"""Small terminal soundboard for speaking text through omachat."""
from __future__ import annotations

import argparse
import curses
import os
import termios
from pathlib import Path

from omavb import generate, monitor_audio, pcm48, send_voice


DEFAULT_PROFILE = "Omatube E2E Test Voice Normalized"


def draw(stdscr: curses.window, text: str, status: str, monitor: bool, profile: str) -> None:
    stdscr.erase()
    height, width = stdscr.getmaxyx()
    title = " omachat Voiceboard "
    stdscr.addnstr(0, 0, title, width - 1, curses.A_BOLD)
    stdscr.addnstr(1, 0, f"Profile: {profile}   Model: Qwen 0.6B   Monitor: {'on' if monitor else 'off'}", width - 1)
    stdscr.addnstr(3, 0, "Type a message. Enter speak | Ctrl-X clear | Ctrl-Q quit", width - 1, curses.A_DIM)
    stdscr.addnstr(5, 0, "Draft:", width - 1, curses.A_BOLD)
    lines = text.splitlines() or [""]
    for row, line in enumerate(lines[: max(1, height - 9)]):
        stdscr.addnstr(6 + row, 0, line, width - 1)
    stdscr.addnstr(height - 2, 0, f"Status: {status}", width - 1, curses.A_DIM)
    stdscr.refresh()


def edit(stdscr: curses.window, initial: str) -> tuple[str, str]:
    text = initial
    status = "ready"
    curses.curs_set(1)
    while True:
        draw(stdscr, text, status, edit.monitor, edit.profile)
        key = stdscr.get_wch()
        if key in ("\x11",):
            return text, "quit"
        if key in ("\x13", "\x14", "\n", "\r", curses.KEY_ENTER):
            return text, "speak"
        if key in ("\x18",):
            text = ""
            continue
        if key in (curses.KEY_BACKSPACE, "\x08", "\x7f"):
            text = text[:-1]
        elif key == "\n":
            text += "\n"
        elif isinstance(key, str) and key.isprintable():
            text += key


def speak(stdscr: curses.window, text: str, args: argparse.Namespace) -> str:
    if not text.strip():
        return "draft is empty"
    try:
        voice_args = argparse.Namespace(
            text=text.split(), profile=args.profile, language=args.language,
            engine="qwen", model_size="0.6B", url=args.url,
        )
        output = generate(voice_args)
        monitor = monitor_audio(output, args.monitor_volume) if args.monitor else None
        try:
            send_voice(pcm48(output), args.socket)
        finally:
            if monitor is not None:
                monitor.wait(timeout=60)
            output.unlink(missing_ok=True)
        return "sent"
    except Exception as exc:  # noqa: BLE001 - show a useful status in the TUI
        return f"error: {exc}"


def run(stdscr: curses.window, args: argparse.Namespace) -> None:
    edit.monitor = args.monitor
    edit.profile = args.profile
    draft = ""
    while True:
        text, action = edit(stdscr, draft)
        if action == "quit":
            return
        status = speak(stdscr, text, args)
        draft = ""
        draw(stdscr, draft, status, args.monitor, args.profile)
        stdscr.get_wch()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default=os.environ.get("VOICEBOX_URL", "http://127.0.0.1:17493"))
    parser.add_argument("--profile", default=os.environ.get("VOICEBOX_PROFILE", DEFAULT_PROFILE))
    parser.add_argument("--language", default=os.environ.get("VOICEBOX_LANGUAGE", "en"))
    parser.add_argument("--socket", default=os.environ.get("OMACHAT_SOCKET", "/tmp/omachat-dev/omachat.sock"))
    parser.add_argument("--no-monitor", dest="monitor", action="store_false")
    parser.add_argument("--monitor-volume", type=int, default=22938)
    parser.set_defaults(monitor=True)
    args = parser.parse_args()
    fd = 0
    original = termios.tcgetattr(fd)
    terminal = termios.tcgetattr(fd)
    terminal[0] &= ~termios.IXON
    termios.tcsetattr(fd, termios.TCSANOW, terminal)
    try:
        curses.wrapper(run, args)
    finally:
        termios.tcsetattr(fd, termios.TCSANOW, original)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
