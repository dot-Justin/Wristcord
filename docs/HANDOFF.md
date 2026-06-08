# Wristcord — Handoff

**Last updated:** 2026-06-08 (v1.1 gateway implementation complete on branch)

## Status

**v1.0.0 is live on repebble.com.** Tag: `v1.0.0` at commit `8f9de62`. Store icon
(`resources/icons/discord-80.png`) and dashboard polish landed in `092b864`.

**v1.1 gateway implementation is complete on `feat/v1.1-gateway`** — pkjs Discord
gateway WebSocket client (HELLO/HEARTBEAT/IDENTIFY/READY/RESUME/MESSAGE_ACK),
canonical Discord unread dots on channel rows, red mention-count badges, and
watch→Discord REST ack on read. Emulator-verified end-to-end against the live
Discord API (gateway loads 1241 read_state entries; OP_CHANNELS join produces
the correct unread/mention counts; ACK round-trip returns Discord 200). **Real
hardware verification is the next step** before tagging v1.1.0 and publishing.

App page: https://apps.rePebble.com/c61d68498f25458ebc9a2ec6
Repo: https://github.com/dot-Justin/Wristcord

## Codebase in 60 seconds

**Architecture:** C watchapp on the watch (`src/c/`) ↔ paged AppMessage transport
(`src/c/rows.c` with RS/US delimiters) ↔ PebbleKit JS bridge on the phone
(`src/pkjs/`) ↔ Discord REST API v10. No backend. No gateway (yet).

**Key files:**
- `src/c/wristcord.c` — init, AppMessage register/open, inbox router
- `src/c/server_list.c` — top-level server list + Settings entry + animated loader
- `src/c/channel_list.c` — categories + channels (unread visuals stripped for v1)
- `src/c/chat_view.c` — message list + ambient poll timer
- `src/c/compose.c` — dictation → confirm → OP_SEND (with WC_DEMO bypass)
- `src/c/msg_view.c` — read-full scroll view
- `src/c/tutorial.c` — first-run onboarding
- `src/c/rows.c` — paged AppMessage transport (shared by all data fetches)
- `src/c/ui_util.c` — `wc_utf8_copy`, `wc_atoi`, `wc_hex_to_color` (hand-rolled,
  see "the crash" below), `wc_make_initials`, drawing helpers
- `src/c/readstate.c` — local unread persist (dormant scaffolding; v1.1 source swap)
- `src/c/settings.c` — Clay-pushed settings state on watch
- `src/pkjs/index.js` — appmessage router + DEMO_MODE switch
- `src/pkjs/lib/discord.js` — REST client (guilds/channels/messages/sendMessage)
- `src/pkjs/lib/model.js` — `buildServerList` / `buildChannelTree` / `packMessages`
- `src/pkjs/lib/paging.js` — record encoding + chunking
- `src/pkjs/lib/demo.js` — fake backend for store screenshots
- `src/pkjs/lib/settings.js` / `lib/color.js` / `lib/clay.js` — small helpers
- `config/index.js` + `config/custom-clay.js` — Clay configuration page
- `scripts/capture-store-shots.sh` — flips DEMO flags, builds, walks emulator,
  captures `screenshots/store/*.png`, always reverts
- `screenshots/store/` — the curated 9-shot marketing set (tracked via .gitignore
  exception)

## **Hard-won gotchas — read these before touching code**

These are non-obvious, expensive-to-rediscover. They're also in `~/.claude/projects/-home-justin/memory/project_wristcord.md` so they survive context compaction.

1. **`strtol`/`atoi`/`strtok` data-abort on the new Core Devices firmware** for an
   SDK-4.9 app. Mechanism: newlib's parse fns reach `errno` via `__getreent()`,
   which the new firmware doesn't set up correctly. **Never call newlib parse
   fns**; the hand-rolled `wc_hex_to_color` / `wc_atoi` in `ui_util.c` replace
   them. After any C change verify `arm-none-eabi-nm build/emery/pebble-app.elf
   | grep -iE 'strtol|atoi|strtok|reent'` is empty. (`snprintf` was fine, so
   it's function-specific.)
2. **Invalid UTF-8 hard-faults `graphics_draw_text` on real hardware** (QEMU
   tolerates it). Any Discord-supplied string that gets truncated then drawn
   MUST go through `wc_utf8_copy`. Initials must be a complete leading
   code point (see `wc_make_initials`) — a lone lead byte crashes the device.
3. **Emery app RAM is 128KB (`MAX_APP_MEMORY_SIZE = 0x20000`)**, ~75KB free
   heap. NOT 64KB. The `uint16 virtual_size` cap (65535) is the *loaded image*
   size, not total RAM — the heap is separate.
4. **`pebble build` caches pkjs/resources/Clay.** If you change anything under
   `src/pkjs/`, `config/`, `resources/`, or messageKeys, do `pebble clean &&
   pebble build`. C is always recompiled.
5. **Clay (pebble-clay 1.0.4):** events are only `BEFORE_BUILD` /
   `AFTER_BUILD` / `BEFORE_DESTROY` / `AFTER_DESTROY`. There is no
   `AFTER_RENDER`. Using it makes `.on()` call `undefined.split` and the
   form blanks. `getItemById` does NOT return sections — gate items by id.
   Always wrap your custom fn in try/catch.
6. **Onboarding's `window_stack_push` rule:** never push a window *during*
   another window's `appear`/`load` transition — the new window's click
   handlers end up uninstalled. Defer via `app_timer_register(350, …)`. See
   `server_list.c::window_appear`.
7. **Pebble emulator quirks:** wedges every few installs — fix is
   `pebble kill` + `rm ~/.local/share/pebble-sdk/4.9.169/emery/qemu_spi_flash.bin`
   then reboot. `pebble emu-button` syntax is `pebble emu-button click <button>`
   (the `click` action is required; bare `select` is silently a no-op).
8. **Syncthing-on-repo bites:** Syncthing fighting git produces
   `.syncthing.*.tmp` corpses in `src/c/` and "untracked file would be
   overwritten" pull errors. **Use `git` as the source of truth** between
   dev-01 and the laptop; Syncthing the repo is actively harmful.
9. **`channel.last_message_id` is canonical Discord state** — when v1.1 needs
   the "newest message in channel X" it should still come from the REST
   `/guilds/{id}/channels` response, not from the gateway READY (which only
   has the user's read pointer, not the channel's tip).

## Dev environment

**Two machines, intentional split:**

- **`dotj-ct-dev-01`** (Debian 12 LXC at 192.168.1.123, SSH alias `dev`) — where
  Claude runs and edits code. pebble-tool 5.0.37 in `~/.pebble-venv/`, wrapper
  at `~/.local/bin/pebble`. SDK 4.9.169 in `~/.local/share/pebble-sdk/`.
  cairosvg installed in the venv for icon rasterization.
- **Laptop** — where the user builds, tests on hardware, and (now) ran the
  publish flow. The verified-on-hardware PBW lives at
  `~/Projects/Pebble/wa/Wristcord/build/Wristcord.pbw` on the laptop.

**Workflow (do NOT use Syncthing):**

```
dev-01: edit code → commit → push (auto-push hook handles it)
laptop: git pull && pebble clean && pebble build → test on watch
```

**Capture marketing screenshots** (anywhere with the emulator):
```
bash scripts/capture-store-shots.sh   # see comments in the script
```
Output goes to `screenshots/store/`. The script flips `DEMO_MODE` (pkjs) and
`WC_DEMO` (compose.c) on, captures, and ALWAYS reverts on exit (trap).

**Publish to repebble.com:**
```
pebble login                # browser-based Firebase auth, laptop only
pebble publish --non-interactive --name "Wristcord" \
  --description "<copy>" --source <repo> \
  --release-notes "<notes>" \
  --screenshots screenshots/store/emery/emery_*.png \
  --no-gif-all-platforms
```
**Don't pass `--category` from the CLI** — the API wants a category *slug* and
"Communication" 500'd. Set it via the dashboard. Same for icons (the watch's
25/56 px assets are too small; upload `resources/icons/discord-{80,144,256,512}.png`
via the dashboard).

## v1.1: The gateway WebSocket — IMPLEMENTED

**Status as of 2026-06-08:** all the code below has been built. Branch
`feat/v1.1-gateway` (3 commits on top of `v1.0.0`). 97 pkjs unit tests pass
(was 73), no firmware-faulting symbols in the ELF, footprint = 58,218 bytes
(+~2.4KB), free heap = 72,854 bytes. Emulator-verified end-to-end against
the live Discord REST + gateway: 1241 read_state entries loaded; channel
rows show the white unread dot for actually-unread channels; mention badges
render with white-on-red numbers; `OP_ACK` round-trips to Discord and
returns 200. Hardware verification is the next step.

Files added/modified by v1.1:
- New: `src/pkjs/lib/gateway.js`, `test/gateway.test.js`,
  `docs/superpowers/specs/2026-06-08-wristcord-v1.1-gateway-design.md`
- Modified: `src/pkjs/{index.js,lib/model.js,lib/discord.js}`,
  `src/c/{channel_list.c,chat_view.c,rows.h,ui_util.{c,h}}`,
  `test/{model.test.js,discord.test.js}`

The original v1.1 roadmap from before implementation:

**The goal:** show canonical Discord unread state on the watch when the user
opens Wristcord, and have watch reads sync back to Discord/phone. v1 hides
unread markers entirely because pure-REST can't agree with Discord's read state.
v1.1 fills that in with a gateway WebSocket from pkjs.

**Why pkjs WS works for us:** the modern Rebble pkjs runtime exposes the
`WebSocket` global (proven by `@moddable/pebbleproxy`). Phone-side WS to
`wss://gateway.discord.gg` works directly — no server needed.

**Why "only when app is open" is fine:** pkjs only runs while the watchapp is
open on the watch. Closing Wristcord tears down pkjs and the WS dies. That's
actually the right shape: every time you open the app, you see canonical
state, and battery isn't burning while the watch is asleep. Spelled out for
the user on 2026-06-08 — they explicitly endorsed this shape.

**Implementation roadmap (estimated half-day to a day of focused work):**

1. **Add a gateway client in pkjs.** New module `src/pkjs/lib/gateway.js`:
   - Open `wss://gateway.discord.gg/?v=10&encoding=json` (skip zlib stream
     for the first cut — JSON is simpler and the v1.1 traffic is light).
   - Receive `Op 10 HELLO` → start heartbeating at `heartbeat_interval` ms.
   - Send `Op 1 HEARTBEAT` with the last sequence number; expect `Op 11 ACK`.
   - Send `Op 2 IDENTIFY` with the user token and **selfbot-safe identify
     properties** (`{ os: "iOS"/"Android", browser: "Discord iOS"/etc.,
     device: "" }` mimicking the official client — using "DiscordBot" or
     generic strings flags the connection).
   - Receive `Op 0 READY` → extract `d.read_state[]` (array of
     `{ id, last_message_id, mention_count, last_pin_timestamp }`).
   - Maintain a `Map<channel_id, last_read_message_id>` from READY +
     incremental `MESSAGE_ACK` events (`t == "MESSAGE_ACK"`).
   - Reconnect on close with exponential backoff; use `Op 6 RESUME` with the
     last `s` if the close code allows.
   - Handle `Op 7 RECONNECT` (cleanly close + RESUME) and `Op 9
     INVALID_SESSION` (clean re-IDENTIFY).
2. **Wire the read map to the C app.** Two options, pick one:
   - **Pull model:** when the C app fetches `OP_CHANNELS`, pkjs joins each
     row with the read map and sets a sixth field (e.g. `last_read_message_id`
     per channel, or just `unread` flag derived from
     `last_message_id != last_read_message_id`). Add to the wire encoding in
     `src/pkjs/index.js`'s `OP_CHANNELS` branch.
   - **Push model:** new `OP_READ_STATE` op that pkjs pushes whenever the
     gateway reports a change while the channel list is on top. More
     real-time but more wiring.
   The pull model is simpler for v1.1 and is what I'd ship.
3. **Re-enable unread visuals in `channel_list.c`.** The scaffolding is still
   there — the `wc_readstate_is_unread`/`wc_readstate_seed_if_absent`/
   `wc_readstate_mark` calls remain wired up, just unused for rendering.
   Either replace the source with the gateway-fed flag, or just delete the
   local-only fallback entirely and use the new field. The comment block in
   `channel_list.c::draw_row` says exactly where to swap.
4. **Watch → Discord ACK.** When `chat_view`'s `wc_readstate_mark` runs (the
   user reached the newest message), also send an `OP_ACK` AppMessage to
   pkjs, which then `POST /channels/{cid}/messages/{mid}/ack` to Discord.
   This is documented and works for user tokens (Discord disabled it for
   bot tokens; we're a selfbot). discord.py-self's `ack_message` shows the
   exact request shape.
5. **Verify on hardware.** The whole point. Login on phone, read on phone,
   re-open watch — channel that was just read on phone should not be marked
   unread on the watch.

**Open design questions for v1.1:**
- How visible should the gateway disconnect state be? Tiny dot in titlebar?
  Reuse the loading-screen dots? Silent?
- Connection delay vs perceived responsiveness — gateway IDENTIFY takes
  ~1-2s. Should the server list paint immediately and the unread flags
  arrive after, or wait? (I'd paint immediately and update.)
- Mention count vs simple unread bool — Discord's read state has both. We
  could show "@3" badges on channels with active mentions for free.

**Research already done** (saved you a few hours of digging):
- ACK endpoint **works** for user tokens via REST:
  `POST /channels/{cid}/messages/{mid}/ack`, body `{ "manual": true }`,
  returns `{ "token": "..." }`. Source: discord.py-self `http.py:1370-1410`.
- Discord read_state is **only** in the gateway READY payload. There's no
  REST equivalent. Confirmed against discord.py-self routes.
- `GET /users/@me/mentions?limit=50` returns recent @-mentions across
  channels — useful fallback if gateway implementation drags but you want a
  "mentions inbox" indicator without a WS.

## What's left for v1.1.0 release

1. **Hardware verification on PT2.** Same drill as v1.0: install on real Pebble
   Time 2, sign in, open a server with known phone/desktop reads, confirm the
   dots match Discord truth. Open an unread channel, return to the channel
   list, confirm the dot is gone (REST ACK round-trip propagated). Watch for
   the firmware-libc gotcha — the `arm-none-eabi-nm` symbol check is green
   but the only authoritative test is real hardware.
2. **Bump version + tag + publish.**
   - `package.json` version `1.0.0` → `1.1.0`
   - Tag the merge commit `v1.1.0`
   - `pebble publish --non-interactive --name "Wristcord" --release-notes
     "<...>"` — the `--release-notes` should highlight cross-device unread
     sync and mention badges. Keep the same UUID + category.
3. **Update the memory `project_wristcord.md`** entry once it ships.
4. **Squash or rebase commits if desired.** Three feat/test commits + the
   spec/handoff commits sit cleanly on top of `main`. Merge as-is or squash.

If anything regresses on hardware (gateway never reaches READY, dots never
render, dots render but never clear), the most likely suspects in order:
- pkjs WebSocket polyfill behavior differs from emulator (very unlikely —
  it ships on every modern phone runtime).
- Token scope: bot tokens won't work; this is a user-token selfbot only.
- `app_message_outbox_begin` busy when `OP_ACK` fires — the code silently
  drops the attempt and retries on the next on_rows_done. Harmless.

## Other backlog (post-v1.1)

- **Role-colored author names** in chat (needs gateway anyway — role data
  comes via gateway).
- **Image previews** — needs an image proxy / resizing service. Currently
  we just show `[image]` / `[attachment]` text tags.
- **Real server icons** — needs proxy + caching.
- **True ambient updates while watchapp is closed** — would require a
  server holding the gateway connection per user. Privacy/ToS dumpster fire,
  not worth it.
- **Reply / react actions** in the chat action menu — currently stubbed
  "(soon)".
- **More themes** — Midnight (default), Dark, Light exist. User feedback
  has been positive; not a priority.

## Memories that already exist (in `~/.claude/projects/-home-justin/memory/`)

- `project_wristcord.md` — the long-form project memory with the hardware
  crash root cause, UTF-8 gotcha, Clay gotchas, etc. **Read this first** in
  any new chat.
- `reference_pebble_store.md` — repebble.com vs apps.rebble.io. Don't
  confuse them.
- `feedback_no_ai_highlight_bars.md` — no left-aligned solid-color accent
  stripes on cards/rows. Reads as AI-generated.
- `project_dev01_server.md` — dev-01 LXC details, SSH alias, layout.
- `feedback_pebble_emulator_kill.md` — always `pebble kill` after emulator
  use.
- `reference_pebble_tool_python312.md` — pebble-tool Python quirks.

## Test pattern

- **pkjs unit tests:** `node --test` runs 73 tests under `test/`. Always
  green; add new tests when changing `model.js` / `paging.js` / `settings.js`
  / `color.js`.
- **C / UI:** emulator screenshots through `scripts/capture-store-shots.sh`
  (with DEMO data) or the manual sequence noted in
  `scripts/seed-emulator-token.py`. Real-hardware test required before any
  publish.
- **Marketing capture:** `bash scripts/capture-store-shots.sh` regenerates
  the 9 store shots in `screenshots/store/`. Flags are reverted on exit.
- **UTF-8 logic verification:** a standalone host-gcc test exists in
  `/tmp/utf8_test.c` (not in repo) — easy to recreate; it asserts
  `wc_utf8_copy` and `wc_make_initials` produce valid UTF-8 across all of
  the user's real Discord guild names.

## Quick start for a fresh chat

1. Read `~/.claude/projects/-home-justin/memory/project_wristcord.md`.
2. Read this doc (`docs/HANDOFF.md`).
3. `git log --oneline -10` on `main` to see what's recent.
4. `node --test` to confirm pkjs is green.
5. `pebble clean && pebble build` to confirm C is clean. Memory report
   should show `~55,862` bytes footprint, `~75 KB` free heap. If those drift
   significantly, investigate before doing anything else.
6. For any user-facing visual change, screenshot before/after in the
   emulator. For any C code change, the user tests on real hardware before
   you push to the store.

When v1.1 lands and you ship a new release: bump `version` in `package.json`,
re-tag, re-run `pebble publish` (it'll create a new release under the same
app entry on the dashboard).
