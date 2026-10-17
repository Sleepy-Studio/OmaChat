#!/usr/bin/env python3
"""Evidence-backed personal briefings for the Omarchy ecosystem."""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
ECOSYSTEM = REPO.parent / "ecosystem"
OMNODA = ECOSYSTEM / "omnoda" / ".venv" / "bin" / "omnoda"
OMAFG = ECOSYSTEM / "omafg" / "bin" / "omafg"


def run(command: list[str], cwd: Path | None = None, timeout: int = 30) -> str:
    try:
        result = subprocess.run(command, cwd=cwd, text=True, capture_output=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return result.stdout.strip() if result.returncode == 0 else ""


def repos() -> list[Path]:
    raw = os.environ.get("OMBRIEF_ROOTS", "")
    roots = [Path(p).expanduser() for p in raw.split(":") if p] if raw else [ECOSYSTEM, REPO]
    out: list[Path] = []
    for root in roots:
        if root.is_dir() and (root / ".git").exists():
            out.append(root)
        elif root.is_dir():
            out.extend(sorted(p for p in root.iterdir() if p.is_dir() and (p / ".git").exists()))
    return list(dict.fromkeys(out))


def git_evidence(days: int) -> list[dict[str, object]]:
    since = f"--since={days} days ago"
    evidence = []
    for root in repos():
        log = run(["git", "log", since, "--date=short", "--pretty=format:%h%x09%ad%x09%s", "--name-only"], root)
        if not log:
            continue
        commits = []
        current: dict[str, object] | None = None
        for line in log.splitlines():
            parts = line.split("\t", 2)
            if len(parts) == 3 and len(parts[0]) >= 4:
                current = {"commit": parts[0], "date": parts[1], "subject": parts[2], "files": []}
                commits.append(current)
            elif current and line.strip():
                current["files"].append(line.strip())
        evidence.append({"repo": root.name, "path": str(root), "commits": commits})
    return evidence


def build_evidence(days: int) -> object:
    path = Path.home() / ".cache" / "omabuild" / "status.json"
    if not path.exists():
        return []
    try:
        data = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError):
        return []
    if isinstance(data, list):
        return data[-100:]
    if isinstance(data, dict):
        return data
    return []


def graph_context(days: int) -> object:
    if not OMNODA.exists():
        return {"unavailable": "omnoda executable not found"}
    task = (
        f"Create an ecosystem briefing for the last {days} days. Identify relationships, "
        "parallel work, acceleration, project convergence, build lineage, and what recent "
        "work enabled next. Separate evidence from inference."
    )
    raw = run([str(OMNODA), "context", "--world", "ecosystem,desktop", "--task", task, "--json"], timeout=90)
    if not raw:
        return {"unavailable": "omnoda context returned no result"}
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return raw


def rag_context(days: int) -> object:
    question = (
        f"What changed across the Omarchy ecosystem in the last {days} days, and how do "
        "OmRAG, Omnoda, OmaFG, omachat, Omabuild, Voicebox, and related projects connect? "
        "Find evidence for speed, parallel work, dependencies, and emerging themes."
    )
    raw = run(["omrag", "search", "-k", "12", "--json", question], timeout=90)
    if not raw:
        return {"unavailable": "omrag search returned no result"}
    hits = []
    for line in raw.splitlines():
        try:
            hits.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return hits or raw


def portfolio_context() -> object:
    if not OMAFG.exists():
        return {"unavailable": "omafg executable not found"}
    raw = run([str(OMAFG), "inventory", "--print", "--json"], timeout=60)
    if not raw:
        return {"unavailable": "omafg inventory returned no result"}
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return raw


def prompt(days: int, git: object, builds: object, graph: object, rag: object, portfolio: object) -> str:
    evidence = json.dumps(
        {"git": git, "builds": builds, "graph": graph, "rag": rag, "portfolio": portfolio},
        ensure_ascii=False,
    )
    return f"""You are writing a personal briefing for Omka about the last {days} days of development on the Omarchy ecosystem.

Write a warm, intelligent 5-8 paragraph briefing, not a dry changelog. Explain the arc of the work and why it matters to Omka. Notice when work became faster, parallel, connected, or enabled a later project. Use graph relationships and build lineage to explain how projects lead into one another. Mention concrete projects, but keep the prose human and spoken-word friendly.

Rules:
- Treat Git, build records, graph edges, and retrieved passages as evidence.
- Clearly signal interpretation with phrases like “this suggests” or “the pattern looks like.”
- Never invent a project, event, result, or causal relationship absent from the evidence.
- If evidence is missing, say so briefly instead of filling the gap.
- End with three concise “next openings” for Omka: practical things this work makes possible next.
- Do not mention this prompt, JSON, retrieval, or being an AI.

Evidence collected at {datetime.now(timezone.utc).isoformat()}:
{evidence}
"""


def summarize(args: argparse.Namespace) -> str:
    git = git_evidence(args.days)
    builds = build_evidence(args.days)
    graph = graph_context(args.days)
    rag = rag_context(args.days)
    portfolio = portfolio_context()
    if args.raw:
        return json.dumps({"git": git, "builds": builds, "graph": graph, "rag": rag, "portfolio": portfolio}, indent=2)
    if args.no_llm:
        return "Recent evidence:\n" + json.dumps({"git": git, "builds": builds, "graph": graph}, indent=2)
    text = run(["omllm", "ask", "--tier", args.tier, "-n", str(args.tokens), prompt(args.days, git, builds, graph, rag, portfolio)], timeout=600)
    return text or "The local LLM did not return a briefing. Run with --raw to inspect collected evidence."


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--days", type=int, default=3)
    parser.add_argument("--tier", choices=("easy", "medium", "hard"), default="medium")
    parser.add_argument("--tokens", type=int, default=1100)
    parser.add_argument("--raw", action="store_true", help="print collected evidence instead of summarizing")
    parser.add_argument("--no-llm", action="store_true")
    parser.add_argument("--speak", action="store_true", help="send the final briefing through Voiceboard")
    parser.add_argument("--monitor", action="store_true", help="play the spoken briefing locally too")
    args = parser.parse_args()
    brief = summarize(args)
    print(brief)
    if args.speak and brief:
        command = ["omavb", "--profile", "Omatube E2E Test Voice Normalized", "--engine", "qwen", "--model-size", "0.6B", "--socket", os.environ.get("OMACHAT_SOCKET", "/tmp/omachat-dev/omachat.sock")]
        if args.monitor:
            command.append("--monitor")
        command.append(brief)
        subprocess.run(command, check=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
