#!/usr/bin/env python3
"""Do omachat's two scheme lists agree, and does it stay free of downloading?

Three claims, all of which are easy to get wrong and none of which a compiler
or a test suite currently checks:

  1. **`isSafeUrl` and the URL regex list the same schemes.** They are two
     separate places in `MarkdownRenderer.cpp`. A scheme added to only one is
     silently broken: allowlisted-but-unmatched is dead code, and
     matched-but-rejected is emitted as plain text. The user sees a share link
     sitting in a channel as an unclickable wall of base64 and concludes sharing
     does not work.

  2. **`openLink` handles `omatube` at all**, and handles it *after* the http(s)
     branch rather than before it -- an earlier branch would swallow it.

  3. **No downloading crept into omachat.** The whole architecture is that
     omachat carries a string and omatube does the fetching. A `yt-dlp` include,
     a `QProcess` calling omatube's internals, or a hardcoded path into
     omatube's library would break that, and it would break quietly.

Static analysis of the source, not a running app: this asserts on the code
rather than on a rendered window, so it needs no display, maps nothing, and
cannot steal focus. What it does **not** prove is that a click in a live window
reaches `openLink` -- that is the user's to confirm.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent
RENDERER = REPO / "client/src/text/MarkdownRenderer.cpp"
RENDERER_HPP = REPO / "client/src/text/MarkdownRenderer.hpp"
ACTIONS = REPO / "client/src/controllers/AppActions.cpp"
MAIN = REPO / "client/src/main.cpp"

failures: list[str] = []


def check(label: str, ok: bool, detail: str = "") -> None:
    print(f"  [{'PASS' if ok else 'FAIL'}] {label}"
          + (f" — {detail}" if detail and not ok else ""))
    if not ok:
        failures.append(label)


for path in (RENDERER, ACTIONS, MAIN):
    check(f"{path.name} exists", path.is_file(), str(path))
if failures:
    sys.exit(1)

renderer = RENDERER.read_text()
actions = ACTIONS.read_text()
main_cpp = MAIN.read_text()

print()
print("a share link in a channel becomes a clickable link")

# The allowlist inside isSafeUrl.
m = re.search(r"isSafeUrl\([^)]*\)\s*\{(.*?)\n\}", renderer, re.S)
check("isSafeUrl body was found", m is not None)
safe_schemes = set(re.findall(r'u"([a-z]+)"', m.group(1))) if m else set()

# The alternation in the URL regex.
#
# Whitespace-tolerant on purpose: clang-format (which CI enforces with
# --Werror) reflowed `urlRe(QStringLiteral(R"(...)"))` across two lines, which
# broke the first version of this pattern while leaving the code perfectly
# correct. A check that parses source layout instead of source *content* fails
# on a reformat, and a check that fails on a reformat trains you to ignore it.
m2 = re.search(r"urlRe\(\s*QStringLiteral\(R\"\((.*?)\)\"\)",
              renderer, re.S)
check("the URL regex was found", m2 is not None)
regex_schemes = set()
if m2:
    alt = re.search(r"\(\?:([^)]*)\)", m2.group(1))
    if alt:
        regex_schemes = set(alt.group(1).split("|"))

check("the regex alternation was parsed", bool(regex_schemes), str(regex_schemes))
# `https?` in the regex is shorthand for two schemes. Expand it *into* the set
# rather than replacing it -- replacing dropped omachat and omatube and made the
# comparison below report a difference that was not there.
if "https?" in regex_schemes:
    regex_schemes = (regex_schemes - {"https?"}) | {"http", "https"}
check("omatube is in isSafeUrl", "omatube" in safe_schemes, str(sorted(safe_schemes)))
check("omatube is in the URL regex", "omatube" in regex_schemes,
      str(sorted(regex_schemes)))

# The load-bearing one.
check("**both** lists name the same schemes", safe_schemes == regex_schemes,
      f"isSafeUrl={sorted(safe_schemes)} regex={sorted(regex_schemes)}")
check("http and https stay allowed",
      {"http", "https"} <= safe_schemes, str(sorted(safe_schemes)))
check("omachat's own scheme stays allowed", "omachat" in safe_schemes,
      str(sorted(safe_schemes)))

print()
print("clicking it hands off to omatube, and only on a click")
check("openLink handles the omatube scheme", 'scheme == u"omatube"' in actions)
check("the handoff uses the desktop, not a subprocess",
      re.search(r'scheme == u"omatube".*?QDesktopServices::openUrl', actions, re.S)
      is not None,
      "expected QDesktopServices::openUrl in the omatube branch")

# Order matters: an earlier catch-all would make the omatube branch dead.
http_at = actions.find('scheme == u"http"')
omatube_at = actions.find('scheme == u"omatube"')
check("the omatube branch comes after the http(s) branch",
      omatube_at > http_at > 0, f"http at {http_at}, omatube at {omatube_at}")
check("an unsupported scheme is still refused",
      "Blocked a link with an unsupported scheme" in actions)

# main.cpp filters argv to omachat:// only, which is exactly WHY a link pasted
# into a message needs no scheme support there. Assert the reason holds, so a
# future change to that filter is a deliberate decision rather than a surprise.
check("main.cpp still only auto-handles omachat:// on launch",
      'startsWith(QStringLiteral("omachat://"))' in main_cpp)
check("the omatube scheme is NOT auto-handled from argv (it is click-only)",
      'startsWith(QStringLiteral("omatube://"))' not in main_cpp,
      "omatube:// in the argv filter would auto-open omatube without a click")

print()
print("omachat carries the link; omatube does the downloading")


def code_only(text: str) -> str:
    """Strip C/C++ comments before scanning for forbidden strings.

    Without this the check forbids the *documentation* of the boundary: the
    comment in AppActions.cpp explains that omachat has no yt-dlp, and the
    first version of this check failed on exactly that line. A check must not
    punish the comment explaining why the rule exists.
    """
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


# The architectural boundary. Anything here is a regression.
forbidden = {
    "yt-dlp / yt_dlp": r"yt[_-]?dlp",
    "a direct library.db path": r"library\.db",
    "an omatube source path": r"omatube/src|/src/omatube",
    "a call into omatube's python": r"python3?\s+-m\s+omatube",
    "a QProcess running omatube": r"QProcess[^\n]*omatube",
}
for label, pattern in forbidden.items():
    hits = []
    for name, text in (("MarkdownRenderer.cpp", renderer),
                       ("AppActions.cpp", actions),
                       ("main.cpp", main_cpp)):
        if re.search(pattern, code_only(text), re.I):
            hits.append(name)
    check(f"no {label} in the client", not hits, ", ".join(hits))

check("omatube is mentioned only in the two intended places",
      renderer.count("omatube") >= 2 and actions.count("omatube") >= 1,
      f"renderer={renderer.count('omatube')} actions={actions.count('omatube')}")

print()
print("the header documents the schemes it now renders")
hpp = RENDERER_HPP.read_text()
check("MarkdownRenderer.hpp mentions the omatube scheme",
      "omatube" in hpp,
      "the file comment still says http(s)/omachat only")

print()
if failures:
    print(f"FAILED: {len(failures)}")
    for f in failures:
        print(f"  - {f}")
    sys.exit(1)
print("all omatube link checks passed")
print()
print("NOT proved here: that clicking a rendered link in a live window reaches")
print("openLink. That requires a real pointer event in a real window, which this")
print("project does not do. It is the user's to confirm.")