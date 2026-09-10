#!/usr/bin/env python3
"""Local task bridge for FoloToy AI Passport.

GET  /tasks    -> serves bridge/tasks.json (read fresh on every request)
GET  /health   -> {"ok": true}
POST /feedback?task_id=..&hz=..&bits=..&ch=..  (chunked 16 kHz / 16-bit / mono PCM body)
               -> wraps the body into a WAV file under bridge/feedback/<task_id>/

Standard library only. Run: python3 bridge/server.py --port 8787
"""

import argparse
import json
import time
import wave
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

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
            print(f"[tasks] GET status=200 source=file count={len(tasks)}", flush=True)
            return self.send_json(200, {"tasks": tasks})
        return self.send_json(404, {"error": "not found"})

    def do_POST(self) -> None:
        self.close_connection = True
        if urlparse(self.path).path != "/feedback":
            return self.send_json(404, {"error": "not found"})
        query = parse_qs(urlparse(self.path).query)
        if (not query.get("task_id") or query.get("hz") != ["16000"] or
                query.get("bits") != ["16"] or query.get("ch") != ["1"]):
            return self.send_json(400, {"error": "expected task_id and 16000 Hz / 16-bit / mono"})
        if (self.headers.get("Transfer-Encoding", "").lower() != "chunked" or
                self.headers.get("Content-Length") is not None):
            return self.send_json(400, {"error": "chunked audio required"})
        task_id = sanitize(query["task_id"][0])
        if task_id in (".", ".."):
            return self.send_json(400, {"error": "invalid task_id"})
        out_dir = FEEDBACK_DIR / task_id
        out_dir.mkdir(parents=True, exist_ok=True)
        wav_path = out_dir / f"{uuid.uuid4().hex}.wav"
        partial = wav_path.with_suffix(".part")
        total = 0
        deadline = time.monotonic() + 40
        self.connection.settimeout(5)
        try:
            with wave.open(str(partial), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(16000)
                while True:
                    if time.monotonic() > deadline:
                        raise ValueError("recording deadline exceeded")
                    line = self.rfile.readline(32)
                    if not line:
                        raise ValueError("upload interrupted before final chunk")
                    if (not line.endswith(b"\r\n") or not line[:-2] or
                            any(c not in b"0123456789abcdefABCDEF" for c in line[:-2])):
                        raise ValueError("invalid chunk header")
                    size = int(line[:-2], 16)
                    if size == 0:
                        if self.rfile.read(2) != b"\r\n" or not total:
                            raise ValueError("empty or incomplete recording")
                        break
                    if size > 512 or size % 2 or total + size > 30 * 32000:
                        raise ValueError("audio limit exceeded")
                    pcm = self.rfile.read(size)
                    if len(pcm) != size or self.rfile.read(2) != b"\r\n":
                        raise ValueError("truncated audio")
                    wav.writeframesraw(pcm)
                    total += size
            partial.replace(wav_path)
        except (ValueError, OSError, EOFError, wave.Error) as exc:
            partial.unlink(missing_ok=True)
            print(f"[feedback] discarded bytes={total} reason={type(exc).__name__}: {exc}", flush=True)
            try:
                self.send_json(400, {"error": str(exc)})
            except OSError:
                pass
            return
        entry = {"time": time.strftime("%Y%m%d-%H%M%S"), "task_id": task_id,
                 "file": str(wav_path.relative_to(ROOT)), "bytes": total, "hz": 16000}
        try:
            with (FEEDBACK_DIR / "log.jsonl").open("a", encoding="utf-8") as log:
                log.write(json.dumps(entry, ensure_ascii=False) + "\n")
        except OSError:
            return self.send_json(500, {"error": "recording saved but index write failed"})
        print(f"[feedback] saved {total} bytes at 16000Hz", flush=True)
        self.send_json(201, {"ok": True, "file": str(wav_path.relative_to(ROOT))})

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
