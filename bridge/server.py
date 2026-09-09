#!/usr/bin/env python3
"""Local task bridge for FoloToy AI Passport.

GET  /tasks    -> serves bridge/tasks.json (read fresh on every request)
GET  /health   -> {"ok": true}
POST /feedback?task_id=..&hz=..&bits=..&ch=..  (raw PCM body)
               -> wraps the body into a WAV file under bridge/feedback/<task_id>/

Standard library only. Run: python3 bridge/server.py --port 8787
"""

import argparse
import json
import time
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse

ROOT = Path(__file__).resolve().parent
FEEDBACK_DIR = ROOT / "feedback"


def sanitize(name: str) -> str:
    cleaned = "".join(c if c.isalnum() or c in "-_." else "_" for c in name)
    return cleaned[:64] or "unknown"


class BridgeHandler(BaseHTTPRequestHandler):
    def send_json(self, code: int, payload: dict) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/health":
            return self.send_json(200, {"ok": True})
        if path == "/tasks":
            source = ROOT / "tasks.json"
            try:
                data = json.loads(source.read_text(encoding="utf-8"))
            except FileNotFoundError:
                return self.send_json(200, {"tasks": []})
            except ValueError as exc:
                return self.send_json(500, {"error": f"invalid tasks.json: {exc}"})
            if isinstance(data, dict):
                tasks = data.get("tasks", [])
            else:
                tasks = data
            return self.send_json(200, {"tasks": tasks})
        return self.send_json(404, {"error": "not found"})

    def do_POST(self) -> None:
        query = parse_qs(urlparse(self.path).query)
        task_id = sanitize(unquote(query.get("task_id", ["unknown"])[0]))
        hz = int(query.get("hz", ["16000"])[0])
        bits = int(query.get("bits", ["16"])[0])
        channels = int(query.get("ch", ["1"])[0])
        length = int(self.headers.get("Content-Length", 0))
        if length <= 0:
            return self.send_json(400, {"error": "empty body"})

        pcm = self.rfile.read(length)
        out_dir = FEEDBACK_DIR / task_id
        out_dir.mkdir(parents=True, exist_ok=True)
        stamp = time.strftime("%Y%m%d-%H%M%S")
        wav_path = out_dir / f"{stamp}.wav"
        with wave.open(str(wav_path), "wb") as wav:
            wav.setnchannels(channels)
            wav.setsampwidth(bits // 8)
            wav.setframerate(hz)
            wav.writeframes(pcm)

        entry = {
            "time": stamp,
            "task_id": task_id,
            "file": str(wav_path.relative_to(ROOT)),
            "bytes": length,
            "hz": hz,
        }
        with (FEEDBACK_DIR / "log.jsonl").open("a", encoding="utf-8") as log:
            log.write(json.dumps(entry, ensure_ascii=False) + "\n")
        print(f"[feedback] task={task_id} <- {wav_path.name} ({length} bytes, {hz}Hz)")
        self.send_json(200, {"ok": True, "file": entry["file"]})

    def log_message(self, fmt: str, *args) -> None:
        pass


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8787)
    args = parser.parse_args()
    FEEDBACK_DIR.mkdir(exist_ok=True)
    server = ThreadingHTTPServer((args.host, args.port), BridgeHandler)
    print(f"bridge listening on {args.host}:{args.port}; tasks from {ROOT / 'tasks.json'}")
    server.serve_forever()


if __name__ == "__main__":
    main()
