#!/usr/bin/env python3
"""Keep the project landing's ombrand footer as its sole identity link."""

from __future__ import annotations

import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlparse


class ProposalLinks(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.links: list[dict[str, object]] = []
        self.current: dict[str, object] | None = None

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag == "a":
            href = dict(attrs).get("href") or ""
            path = urlparse(href).path
            if path.rstrip("/").endswith("identity/proposals"):
                self.current = {"href": href, "text": []}
                self.links.append(self.current)

    def handle_data(self, data: str) -> None:
        if self.current is not None:
            self.current["text"].append(data.strip())

    def handle_endtag(self, tag: str) -> None:
        if tag == "a":
            self.current = None


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.cwd()
    landing = root / "site/index.html"
    if not landing.is_file():
        print(f"missing landing page: {landing}", file=sys.stderr)
        return 1

    parser = ProposalLinks()
    parser.feed(landing.read_text(encoding="utf-8"))
    if len(parser.links) != 1:
        print(
            f"expected one ombrand link to identity proposals; found {len(parser.links)}",
            file=sys.stderr,
        )
        return 1

    label = " ".join(parser.links[0]["text"]).strip()
    if label != "ombrand":
        print(f"expected link label 'ombrand'; found {label!r}", file=sys.stderr)
        return 1

    print("landing has one ombrand link to identity proposals")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
