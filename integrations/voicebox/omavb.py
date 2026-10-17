#!/usr/bin/env python3
"""Type text into an active omachat voice channel and speak it with Voicebox."""
from __future__ import annotations

import argparse
import base64
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path

omavb_root = Path(os.environ.get("OMAVB_ROOT", Path.home() / "ombrella/voice/omavb")).expanduser()
sys.path.insert(0, str(omavb_root / "integrations/voicebox-runtime"))
from voicebox_runtime import speak as voicebox_speak


def generate(args: argparse.Namespace) -> Path:
    if not args.profile:
        raise SystemExit("set --profile or VOICEBOX_PROFILE")
    fd, name = tempfile.mkstemp(suffix=".wav")
    os.close(fd)
    output = Path(name)
    voicebox_speak(" ".join(args.text), output, profile=args.profile,
                   language=args.language, url=args.url, engine=args.engine or None,
                   model_size=args.model_size or None)
    return output


def pcm48(path: Path) -> list[float]:
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        rate = wav.getframerate()
        width = wav.getsampwidth()
        data = wav.readframes(wav.getnframes())
    if width != 2:
        raise RuntimeError("Voicebox WAV must be 16-bit PCM")
    values = struct.unpack("<%dh" % (len(data) // 2), data)
    mono = [sum(values[i : i + channels]) / channels / 32768.0 for i in range(0, len(values), channels)]
    if rate == 48000:
        return mono
    count = int(len(mono) * 48000 / rate)
    return [mono[min(len(mono) - 1, int(i * rate / 48000))] for i in range(count)]


def send_voice(samples: list[float], socket_path: str) -> None:
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.connect(socket_path)
        for start in range(0, len(samples), 960):
            chunk = samples[start : start + 960]
            payload = struct.pack("<%df" % len(chunk), *chunk)
            request = {
                "v": 1,
                "id": start // 960 + 1,
                "method": "voice.speech_chunk",
                "params": {"pcm_f32le": base64.b64encode(payload).decode(), "final": start + 960 >= len(samples)},
            }
            sock.sendall((json.dumps(request, separators=(",", ":")) + "\n").encode())
            response = b""
            while not response.endswith(b"\n"):
                response += sock.recv(65536)
            result = json.loads(response)
            if not result.get("ok"):
                raise RuntimeError(response.decode().strip())
            # The daemon's speech ring holds 10 seconds. Keep the producer at
            # the audio's 48 kHz playback rate instead of filling it at IPC
            # speed with longer messages.
            time.sleep(len(chunk) / 48000)


def monitor_audio(path: Path, volume: int) -> subprocess.Popen[bytes]:
    if not 0 <= volume <= 65536:
        raise SystemExit("--monitor-volume must be between 0 and 65536")
    try:
        return subprocess.Popen(
            ["paplay", f"--volume={volume}", str(path)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
    except FileNotFoundError as exc:
        raise SystemExit("local monitoring needs paplay (PipeWire/PulseAudio tools)") from exc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("text", nargs="+")
    parser.add_argument("--url", default=os.environ.get("VOICEBOX_URL", "http://127.0.0.1:17493"))
    parser.add_argument("--profile", default=os.environ.get("VOICEBOX_PROFILE", ""))
    parser.add_argument("--language", default=os.environ.get("VOICEBOX_LANGUAGE", "en"))
    parser.add_argument("--engine", default=os.environ.get("VOICEBOX_ENGINE", ""))
    parser.add_argument("--model-size", choices=("0.6B", "1.7B"), default=os.environ.get("VOICEBOX_MODEL_SIZE", ""))
    parser.add_argument("--socket", default=os.environ.get("XDG_RUNTIME_DIR", "/tmp") + "/omachat/omachat.sock")
    parser.add_argument("--monitor", action="store_true", help="also play the generated speech locally")
    parser.add_argument("--monitor-volume", type=int, default=22938, help="local monitor volume (0-65536; default 35%%)")
    args = parser.parse_args()
    output = generate(args)
    monitor = monitor_audio(output, args.monitor_volume) if args.monitor else None
    try:
        send_voice(pcm48(output), args.socket)
    finally:
        if monitor is not None:
            try:
                monitor.wait(timeout=30)
            except subprocess.TimeoutExpired:
                monitor.terminate()
        output.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
