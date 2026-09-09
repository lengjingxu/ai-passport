<p align="right">
  <strong>English</strong> · <a href="README.zh_CN.md">简体中文</a>
</p>

# Cindy Task Bridge

A zero-dependency local HTTP service that connects the AI Passport device to task state on your computer.

## Protocol

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/tasks` | Returns `{"tasks": [...]}` read fresh from `bridge/tasks.json` on every request, so any writer can update it between polls. |
| GET | `/health` | Liveness check. |
| POST | `/feedback?task_id=<id>&hz=16000&bits=16&ch=1` | Body is raw little-endian PCM. The server wraps it into a WAV file under `bridge/feedback/<task_id>/` and appends a line to `bridge/feedback/log.jsonl`. |

Task fields consumed by the device: `id` (required, unique), `title`, `status` (`queued` / `running` / `in_progress` / `done` / `completed` / `failed` / `error`), `message`, `updated_at` (epoch seconds). At most 8 tasks are shown.

## Run

`bash
python3 bridge/server.py --port 8787
`

## Feed task data

`tasks.json` is the single integration point. Any process on this computer can rewrite it between polls: a Cindy session, a cron job, or a manual edit all work. Keep task messages ASCII/Latin where possible, because the device font has no CJK glyphs.

## Device setup

1. Copy `main/app_config.h.example` to `main/app_config.h` (gitignored).
2. Fill `APP_WIFI_SSID`, `APP_WIFI_PASSWORD` (2.4 GHz), and `APP_BRIDGE_URL` with this computer LAN address, for example `http://192.168.1.20:8787`.
3. Build and flash per `docs/development/engineering/build-and-test.md`, then open the Tasks page in the device menu.
