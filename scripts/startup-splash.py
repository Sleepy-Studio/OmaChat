#!/usr/bin/env python3
"""Small, optional GTK splash for launch work that takes longer than a blink."""

from __future__ import annotations

import os
import re
import sys
import time
import tomllib
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        return 2
    status_file = Path(sys.argv[1])
    colors = {
        "background": "#141b2e",
        "foreground": "#e8ecf5",
        "accent": "#7aa2f7",
        "selection": "#24365e",
    }
    theme_path = Path(os.environ.get(
        "OMACHAT_THEME_FILE", str(Path.home() / ".config/omachat/theme.toml")
    ))
    try:
        theme = tomllib.loads(theme_path.read_text(encoding="utf-8"))
        for key in colors:
            value = theme.get(key)
            if isinstance(value, str) and re.fullmatch(r"#[0-9a-fA-F]{6}", value):
                colors[key] = value
    except (OSError, ValueError, tomllib.TOMLDecodeError):
        pass
    # Avoid flashing a window for launches where the update check is quick.
    time.sleep(0.7)
    try:
        import gi

        gi.require_version("Gtk", "4.0")
        from gi.repository import GLib, Gtk
    except Exception:
        return 0

    class Splash(Gtk.Application):
        def __init__(self) -> None:
            super().__init__(application_id="org.omarchy.OmaChat.Startup")
            self.window = None

        def do_activate(self) -> None:
            window = Gtk.Window(application=self)
            window.set_title("omachat startup")
            window.set_default_size(340, 156)
            window.set_resizable(False)
            window.set_decorated(False)
            window.set_modal(True)
            window.set_hide_on_close(True)

            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
            box.set_halign(Gtk.Align.CENTER)
            box.set_valign(Gtk.Align.CENTER)
            box.set_margin_top(22)
            box.set_margin_bottom(22)
            box.set_margin_start(28)
            box.set_margin_end(28)

            spinner = Gtk.Spinner()
            spinner.set_spinning(True)
            spinner.set_halign(Gtk.Align.CENTER)
            spinner.add_css_class("accent")
            label = Gtk.Label(label="Starting omachat…")
            label.add_css_class("title-3")
            box.append(spinner)
            box.append(label)
            window.set_child(box)

            css = Gtk.CssProvider()
            css.load_from_data(f"""
                window {{ background: {colors['background']}; color: {colors['foreground']}; border: 1px solid {colors['selection']}; border-radius: 14px; }}
                spinner {{ color: {colors['accent']}; min-width: 28px; min-height: 28px; }}
                label {{ color: {colors['foreground']}; font-size: 15px; font-weight: 600; }}
            """.encode())
            Gtk.StyleContext.add_provider_for_display(
                window.get_display(), css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
            )
            self.window = window
            window.present()
            GLib.timeout_add(180, self.refresh)

        def refresh(self) -> bool:
            try:
                message = status_file.read_text(encoding="utf-8").strip()
            except OSError:
                message = ""
            if not message:
                if self.window is not None:
                    self.window.close()
                self.quit()
                return GLib.SOURCE_REMOVE
            if self.window is not None and isinstance(self.window.get_child(), Gtk.Box):
                box = self.window.get_child()
                child = box.get_last_child()
                if isinstance(child, Gtk.Label) and child.get_text() != message:
                    child.set_text(message)
            return GLib.SOURCE_CONTINUE

    # No error output or dependency on GTK: a missing display/toolkit must never
    # prevent the source launcher from working.
    try:
        app = Splash()
        return app.run([])
    except Exception:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
