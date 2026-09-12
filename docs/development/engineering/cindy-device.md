<p align="right"><a href="cindy-device.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Cindy device connection and interaction

This is Cindy device firmware. [Codex Buddy](https://github.com/zhangsan2000w-art/ai-passport-codex-buddy/tree/52d612cbe47c4528b95994710d320b19cc7479c6) supplies design ideas for BLE task state, heartbeat, Chinese screens and physical buttons. Its Windows bridge and Codex hooks do not connect Cindy.

## Chinese and audio references

[AI Passport Xiaozhi](https://github.com/FoloToy/folo-ai-passport-xiaozhi/tree/d24fce080d86d7cc642f71585f6efde40fb99104/main/audio) captures 16 kHz mono audio, encodes 60 ms Opus frames at complexity 0, and sends them through bounded queues. Its transport is WebSocket/Wi-Fi. Cindy keeps the same encoder settings with 40 ms frames for the no-PSRAM BLE target. At 16 kbit/s, each payload stays within the 120-byte indication bound. Congestion aborts a recording; words are never silently dropped.

Xiaozhi uses Noto font assets, including binary-font loading and optional pushed glyphs. Cindy embeds a 14 px / 1 bpp Noto CJK bitmap in Flash. See [font source and license](../../../assets/README.md#passport-font). ASCII, CJK punctuation, Extension A, unified ideographs and fullwidth forms are covered; emoji and Extension B are not covered. Task previews truncate on UTF-8 character boundaries. No PSRAM is required.

## Bluetooth interaction

Use the matching Cindy macOS client: Settings → Shortcuts → Accessories → Cindy Passport. Enable it, open **Cindy BLE** on the device, select the discovered identifier, and enter the device's displayed code if macOS requests pairing. Permission and connection states are visible. No IP address or HTTP bridge is required. The menu-bar control also remains available.

UP/DOWN selects a task in the status list; OK opens its details. In details, UP/DOWN pages through Cindy's latest visible reply and OK starts recording. Another OK stops for transcription; 30 seconds stops automatically. The device then previews the transcript: UP discards it and records again, DOWN cycles review pages, and OK confirms sending it to the original task. Holding OK exits and cancels an unconfirmed recording. Task replies respect clear and rewind boundaries.

Wi-Fi recording keeps its separate sender worker and eight-block PCM queue. BLE recording runs Opus on the page worker's fixed 28 KB stack and keeps the 512-byte capture block out of that stack. The LVGL pool is 32 KB. Device controls and paged text reuse the existing bounded BLE transport; full replies and transcripts stay on the Mac. Each indication waits at most 1.5 seconds for acknowledgement. A write or acknowledgement error cancels the recording.

The client keeps observed completed/error tasks after the transient activity feed clears them. The canonical catalog still removes archived/deleted tasks. At most eight tasks appear, ordered waiting, error, running, completed. Retention is memory-only, capped at 100 observed terminal tasks, and clears when the adapter stops. Active tasks can occupy all eight slots. This is the physical Mac's catalog, not remote mirrored rows or sidebar ordering.

Complete recordings are decoded to 16 kHz mono PCM in Mac memory and passed to Cindy's currently selected voice-input service and ASR model. Managed Cindy voice uses the existing session service; explicitly configured providers use their existing credential and connection paths. Changing the voice configuration or account during transcription aborts that recording. Passport does not select another model. The reviewed text is only released after the matching hardware confirmation. Cindy opens the original task and submits through its normal input queue, preserving any existing composer draft. Each confirmation is claimed once; a rejected send returns to device review and requires another confirmation. Account, connection and task boundaries are checked before delivery.

## Protocol

Service `C1DC0001-51C4-499D-A186-4621A4938301` uses authenticated pairing with Secure Connections and 16-byte keys. RX `8302` accepts authenticated writes; TX `8303` provides authenticated reads and task-open notifications. Snapshots are uint16LE payload length, version 1, count 0–8, then 328 bytes per task: NUL-padded UTF-8 id/title/status/message widths 40/80/16/192. The Mac sends a heartbeat every 5 seconds; tasks expire after 10 seconds without a snapshot. BLE callbacks never touch LVGL.

Voice characteristic `8304` v1 indicates `kind:u8, token:u32LE, sequence:u16LE, payload`. Start kind 1 has a 40-byte task ID and sequence 0. Kind 2 carries 1–120 bytes of 40 ms Opus and a contiguous sequence starting at 0. Finish kind 3 and cancel kind 4 have no payload and use the next sequence. Up to 752 audio frames allow 30 seconds plus a padded partial frame and encoder flush. The Mac rejects an incomplete sequence or a recording longer than 45 seconds elapsed. Ogg keeps the encoder delay and silent flush instead of guessing a preskip value. Old clients without `8304` cannot record; update both sides.

Task-control TX notifications contain `version=2:u8, action:u8, taskId:40 bytes, recordingToken:u32LE` (46 bytes, requiring ATT MTU ≥49). Actions 1–6 are open, previous page, next page, re-record, confirm and cancel. Review actions must match the task and recording token. Snapshot status `draft:<8-hex-token>` identifies review, `retry:<token>` a rejected send, `asr:<token>` transcription and `error:<token>` a failed transcription; the device displays Chinese labels instead of tokens. Text pages use `[page/count]` and four short rows inside the 192-byte message field. Update the firmware and desktop client together.

## Re-pairing

A stored bond that is not authenticated, or a Mac that still holds stale keys for
"Cindy Passport", makes every reconnect short-lived: the link encrypts with the
old key and the firmware drops it. The firmware now deletes that peer entry and
terminates the link once, so the next attempt starts a fresh passkey pairing. If
macOS keeps reusing the old key, forget "Cindy Passport" in System Settings ->
Bluetooth and connect again.

Flashing `FoloToy-AI-Passport.bin` at `0x10000` updates the application without
touching NVS. The merged image written from `0x0` fills the gaps, including the
NVS partition at `0x9000`, with `0xFF`.

## Legacy Wi-Fi mode

**Cindy Tasks** remains a separate, explicitly selected LAN mode using the ignored `main/app_config.h` and `python3 bridge/server.py --port 8787`. Tasks come from a file; HTTP chunked PCM is archived as WAV. It does not supply live Cindy events or transcription. BLE never switches to this path. Local configured firmware may contain credentials and must not be uploaded publicly.

## Build and acceptance

Activate ESP-IDF 5.5.3 and run `./tools/validate.sh`. The verified merged image is `build/FoloToy-AI-Passport-full.bin`. Keep 8 MB Flash, the 3 MB app limit and protected `cardid` at `0x356000`. A merged image verified to end before `cardid` can be written at `0x0` without whole-chip erase.

Report Build, Host tests, Device tests and Unverified separately. Host checks cover UTF-8/model logic, bounded recording queue behavior and cleanup; Mac checks cover protocol bounds, task history and Ogg framing. Hardware acceptance still requires CJK screen inspection, BLE recording/ASR continuity, repeated recordings/heap stability, permission prompts, reconnect and page exit. A successful build does not establish those results.
