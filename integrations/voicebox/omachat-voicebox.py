#!/usr/bin/env python3
"""Optional Voicebox bridge for omachat speech and transcription."""
from __future__ import annotations
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

omavb_root = Path(os.environ.get("OMAVB_ROOT", Path.home() / "ombrella/voice/omavb")).expanduser()
sys.path.insert(0, str(omavb_root / "integrations/voicebox-runtime"))
from voicebox_runtime import speak as voicebox_speak, transcribe as voicebox_transcribe


def speak(args) -> int:
    temporary = None
    output = Path(args.output) if args.output else None
    if output is None:
        temporary = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
        temporary.close()
        output = Path(temporary.name)
    voicebox_speak(" ".join(args.text), output, profile=args.profile,
                   language=args.language, url=args.url)
    if args.output:
        print(output)
    else:
        player = next((shutil.which(name) for name in ("pw-play", "paplay", "ffplay", "mpv") if shutil.which(name)), None)
        if player:
            command = [player, "--no-video", str(output)] if Path(player).name in {"ffplay", "mpv"} else [player, str(output)]
            subprocess.run(command, check=False)
        else:
            print(f"saved: {output}")
    if temporary is not None:
        output.unlink(missing_ok=True)
    return 0


def transcribe(args) -> int:
    print(voicebox_transcribe(args.audio, url=args.url))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default=os.environ.get("VOICEBOX_URL", "http://127.0.0.1:17493"))
    sub = parser.add_subparsers(dest="command", required=True)
    speak_parser = sub.add_parser("speak")
    speak_parser.add_argument("text", nargs="+")
    speak_parser.add_argument("--profile", default=os.environ.get("VOICEBOX_PROFILE", ""))
    speak_parser.add_argument("--language", default=os.environ.get("VOICEBOX_LANGUAGE", "en"))
    speak_parser.add_argument("--output")
    transcribe_parser = sub.add_parser("transcribe")
    transcribe_parser.add_argument("audio", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "speak":
            if not args.profile:
                parser.error("speak requires --profile or VOICEBOX_PROFILE")
            return speak(args)
        return transcribe(args)
    except Exception as exc:
        print(f"Voicebox unavailable: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
