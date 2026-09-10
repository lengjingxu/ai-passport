<p align="right">
  <a href="cindy-device.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Cindy device connection and interaction

The product is a Cindy task device. The external
[Codex Buddy source](https://github.com/zhangsan2000w-art/ai-passport-codex-buddy/tree/52d612cbe47c4528b95994710d320b19cc7479c6)
is a design reference, not the firmware delivered for this product.

## What the reference implements

Codex lifecycle hooks feed a local Windows bridge, which sends state over
encrypted Nordic UART BLE to a Chinese pixel-pet interface. The controller
scans `Codex-*`, connects, subscribes to notifications and synchronizes time,
owner and status. It sends heartbeats every 10 seconds; the device marks state
stale after 30 seconds. Pairing alone does not deliver task events.

Its 240 x 320 UI shows working, waiting, completed and error states, time,
battery and sound. UP changes pages or scrolls an approval; DOWN changes
subpages or denies; OK confirms or allows once; holding OK opens the menu.
The first key after backlight sleep only wakes the display. Pending approval
IDs and a decision lock prevent duplicate decisions. Windows startup code uses
`winreg`; its installer writes `~/.codex/hooks.json`. These assumptions must not
be presented as Cindy/macOS integration.

Useful ideas for Cindy are visible connection stages, fresh task details,
bounded messages, worker-owned I/O and explicit feedback acknowledgement.
Importing Codex hooks, approval semantics or the reference binary does not
connect Cindy. A future approval adapter needs actual Cindy request IDs and
expiry/cancellation events before device buttons can authorize anything.

## Current Cindy path

```text
Task writer -> bridge/tasks.json -> local HTTP bridge -> Wi-Fi -> Cindy Tasks
Device recording -> HTTP POST /feedback -> WAV archive on the computer
```

Configure the ignored `main/app_config.h` using its example: the 2.4 GHz Wi-Fi
credentials and the computer's Wi-Fi LAN URL on port 8787. Start
`python3 bridge/server.py --port 8787`; `/health` checks availability and `/tasks`
returns the file contents. A VPN default route is not the Wi-Fi LAN interface.
Firmware contains the local configuration and must not be publicly uploaded.

Tasks owns its station netif/DHCP client. Scanning an AP does not prove
authentication or DHCP success. The page shows connecting, getting an IP,
numeric disconnect reason, initialization error, or bridge error. The worker
polls only after an IP event. Page exit stops the worker, releases Wi-Fi and
deletes the screen before another demo can claim the radio. Button callbacks
queue events; the application task handles page lifecycle with LVGL locking.

UP/DOWN selects a task in the scrollable list; OK opens details. Details update
with polling; UP/DOWN switches tasks. OK starts recording and another OK stops
and sends it. The 30-second recording limit also submits the recording;
holding OK exits and discards an unfinished recording. The battery appears at
the top right, below the cloud. UI text remains English because the current
font does not contain Chinese glyphs.

The bridge currently has no automatic Cindy session subscription, speech
transcription or agent-reply delivery. WAV storage only acknowledges receipt.
Do not describe task fixtures as live Cindy events or claim that recording
already controls Cindy. Automatic session state and feedback routing require
an explicit Cindy adapter, independently of this firmware connection fix.

## Build and acceptance

Activate ESP-IDF 5.5.3 and run `./tools/validate.sh`. The merged image is
`build/FoloToy-AI-Passport-full.bin`. Preserve 8 MB Flash, the 3 MB application
limit and `cardid` at `0x356000`. A merged file ending before `cardid` can be
written at `0x0` without whole-chip erase. Never erase a provisioned card.

Host regression tests exercise actual station initialization, DHCP events,
disconnect reasons, cleanup, failed initialization and reentry using ESP-IDF
test doubles. Hardware acceptance still needs Wi-Fi association/DHCP, task
rendering, recording upload and repeated Tasks/scan/menu navigation. Report
Build, Host tests, Device tests and Unverified separately.

## Streaming recording

The [AI Passport Xiaozhi implementation](https://github.com/FoloToy/folo-ai-passport-xiaozhi/tree/d24fce080d86d7cc642f71585f6efde40fb99104/main/audio)
uses frame-based capture, bounded queues and Opus transmission. Cindy borrows
bounded streaming; the current LAN bridge accepts uncompressed PCM (32,000
bytes/second). No Opus encoder is added in this change.

Eight 512-byte PCM blocks decouple microphone reads from network writes. Queue
metadata, the uploader stack and HTTP buffers consume additional memory. A full queue
waits up to 20 ms so the lower-priority uploader can drain buffered microphone
bursts. Continued exhaustion aborts the recording instead of silently dropping
speech. Start/end
logs include free heap, largest block, byte count and error code, without audio
or credentials. On-device timing, capture continuity and repeated-recording
heap stability remain hardware acceptance checks.

POST /feedback requires chunked 16 kHz / 16-bit / mono PCM, chunks at most 512
bytes, at most 30 seconds total. The bridge writes a temporary WAV and renames
it only after a valid final chunk; HTTP 201 acknowledges the saved file.
Cancellation or truncation removes the temporary file. A missing acknowledgement
is a send failure; the device does not retry automatically. Update bridge and
firmware together; fixed Content-Length uploads are no longer accepted.

The host recording-controller regression models buffered input and a sender that
runs when the producer waits. It covers repeated recordings, sustained queue
congestion, cancellation, the duration limit and resource cleanup; it does not
measure hardware scheduling or audio continuity. Bridge logs identify task data
as `source=file`, and report discarded audio byte counts when an upload ends
before its final chunk. These logs do not indicate a live Cindy subscription.

Recording temporarily selects `WIFI_PS_NONE` and restores the previous mode on
success, cancellation and failure. The upload socket uses `TCP_NODELAY` to avoid
Nagle delays between PCM frames. A failure to configure either setting aborts
recording. These settings reduce latency at the cost of higher radio activity
during recording; board power consumption and real-network reliability require
measurement. End logs include captured/sent byte counts and maximum HTTP write
time. Queue waits remain bounded at 20 ms; no additional audio RAM is allocated.

## Bluetooth task mode (v1)

Choose **Cindy BLE** in the device menu. This mode starts only NimBLE; it never
falls back to Wi-Fi or the file bridge. UP/DOWN selects a task, OK opens its
local detail, and a second OK asks Cindy to open that exact task ID. Holding OK
returns to the menu and stops Bluetooth. Bluetooth recording and approval
buttons are not implemented in v1; the separate Tasks page retains Wi-Fi audio.

The matching Cindy Desktop adapter lives under `src/main/passport/`, with its
macOS CoreBluetooth helper under `native/passport/`. Both firmware and Desktop
must be updated. Existing released Cindy builds cannot consume this service.
The development opt-in is `CINDY_PASSPORT_BLE=1` when launching Cindy through its
normal development wrapper. The default is off; removing the environment
variable disables the adapter. The helper adds a **Cindy BLE** menu-bar item:
select the discovered Passport, then enter the device's six-digit passkey in
the macOS pairing dialog. Bond keys persist in NimBLE NVS and macOS; no key is
logged. The selected peripheral identifier is scoped to the Cindy profile.
The menu can disconnect and disable auto-reconnect without erasing bond keys.

Cindy's input-device activity feed provides state. The existing active task
catalog provides titles and excludes archived tasks and worker sessions.
At most eight tasks are ordered by waiting, error, running, then completed,
with newest activity first within each state. This is an activity projection,
not a copy of every sidebar row or its user-selected sort order. The desktop
validates device-supplied task IDs against both current activity and the current
catalog before opening. It never accepts an arbitrary command or approval.
SSH-backed tasks use the same host activity feed; this adapter connects to the
physical Mac's task catalog, not a remote desktop's mirrored sidebar.

Service UUID: `C1DC0001-51C4-499D-A186-4621A4938301`. RX ends in `8302`, TX in
`8303`. RX requires authenticated encrypted writes. TX provides an authenticated
40-byte NUL-terminated task ID read; a one-byte notification tells the central
to read it. The initial read establishes authentication and is never interpreted
as an old button action. Notifications do not carry task content.

A snapshot starts with a little-endian uint16 payload length, version byte `1`,
and task count byte. Each task has four NUL-terminated, zero-padded UTF-8 fields
of 40/80/16/192 bytes (ID/title/status/message); status values are `queued`,
`running`, `waiting`, `done`, `failed`. IDs must fit without truncation; display
text is truncated at UTF-8 boundaries. Maximum frame size is 2,628 bytes.
ATT writes may split a frame anywhere but must not combine frame boundaries.
Invalid versions, lengths, counts, duplicate IDs and unterminated fields fail
closed. A snapshot is published only when the full frame validates.

The central writes one ATT chunk with response at a time, retains only the
latest unsent snapshot and refreshes a heartbeat every five seconds. A pending
write exceeding ten seconds disconnects the link; reconnect attempts wait three
seconds. Disconnect discards partial frames and clears device tasks. Ten seconds
without a snapshot also clears tasks and displays `Cindy sync paused`. BLE
callbacks do not access LVGL; the existing page worker consumes complete frames.

Host checks cover fragmentation, invalid frames, UTF-8 boundaries, state mapping,
bounded pending updates and rejected stale task IDs. Physical pairing,
auto-reconnect, task freshness, repeated page entry/exit, Mac permission prompts
and RAM use still require board validation. Current screen fonts lack CJK glyphs.
