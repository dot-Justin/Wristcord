# Wristcord v1.1 — Gateway-Driven Read State

**Date:** 2026-06-08 · **Status:** approved (user 2026-06-08), pre-implementation
**Branch:** `feat/v1.1-gateway` · **Target:** Emery (PT2)

## 1. Summary

v1.0 ships with unread indicators hidden because pure-REST can't agree with
Discord's canonical read state — reads on phone/desktop are invisible to the
watch. v1.1 closes that gap by opening a Discord gateway WebSocket from
PebbleKit JS while the watchapp is running, maintaining a per-channel read map,
and joining it into the existing `OP_CHANNELS` payload. When the user reads a
message on the watch we POST a REST `ack` so other clients reflect it too.

### Scope

**In:** gateway client (HELLO/HEARTBEAT/IDENTIFY/READY/RESUME/MESSAGE_ACK only);
re-enabled unread visual on channel rows; **mention-count badge** (red circle
with white number) when `mention_count > 0`; watch → Discord ACK on read.

**Out:** role colors, image proxy, true ambient/background updates, mentions
inbox screen, presence, typing, MESSAGE_CREATE-driven live chat. All v1.2+.

## 2. Architecture changes

```
                          ┌─ wss://gateway.discord.gg ──────────────┐
                          │  Op 10 HELLO → heartbeat loop           │
┌──────────────┐          │  Op 2  IDENTIFY → Op 0 READY            │
│  C watchapp  │ AppMsg ◀▶│  Op 0  MESSAGE_ACK (live updates)       │
└──────────────┘ phone JS │  Op 6  RESUME on reconnect              │
                          └──────────────────────────────────────────┘
                          (lib/gateway.js — new module, ~250 LOC)
```

The gateway runs **only while the watchapp is open** (pkjs lifetime). On every
`OP_CHANNELS` fetch, pkjs joins the channel rows against the gateway's read
map and emits a new `unread`/`mention_count` field. The C app reads those
fields and renders the dot/badge — no new appmessage flow for unread.

For ACK: a new tiny `OP_ACK` appmessage from C (`channel_id`, `message_id`)
triggers `POST /channels/{cid}/messages/{mid}/ack` in pkjs.

## 3. Gateway client (`src/pkjs/lib/gateway.js`)

State machine (single connection):

```
DISCONNECTED → CONNECTING → AWAITING_HELLO → IDENTIFYING/RESUMING → READY
                                                        ↓
                                                  (live MESSAGE_ACK)
                                                        ↓
                       CLOSED ←──────────  (reconnect w/ backoff)
```

**Protocol details (verified against discord.py-self):**

- URL: `wss://gateway.discord.gg/?v=10&encoding=json` (no compression — keep simple).
- HELLO (Op 10): `{ heartbeat_interval }`. Start heartbeat loop.
- HEARTBEAT (Op 1): `{ op: 1, d: lastSeq | null }`. Expect Op 11 ACK.
- IDENTIFY (Op 2): selfbot-safe properties mimicking real client:
  ```json
  { "op": 2, "d": {
      "token": "<user token>",
      "capabilities": 16381,
      "properties": { "os": "iOS", "browser": "Discord iOS", "device": "iPhone" },
      "presence": { "status": "online", "afk": false, "since": 0, "activities": [] },
      "compress": false,
      "client_state": { "guild_versions": {} }
    } }
  ```
- READY (Op 0, t="READY"): read `d.session_id`, `d.resume_gateway_url`, and
  `d.read_state.entries[]` — each `{ id, last_message_id, mention_count }`.
- MESSAGE_ACK (Op 0, t="MESSAGE_ACK"): `{ channel_id, message_id, mention_count? }`.
  Update the in-memory map. (No subscription needed — MESSAGE_ACK is user-scoped.)
- RESUME (Op 6): `{ token, session_id, seq }`. Reconnect to `resume_gateway_url`
  if available, otherwise the default URL.
- Close codes: do NOT reconnect on 4004, 4010-4014 (auth/intent failure).
  Otherwise: backoff 1s → 2s → 4s → 8s → 30s (cap), then attempt RESUME first,
  fall back to fresh IDENTIFY on Op 9 INVALID_SESSION.

**Public API** (consumed by `src/pkjs/index.js`):

```js
var gw = require('./lib/gateway').create({
  getToken: function () { return loadSettings().token; }
});
gw.start();                              // connect; idempotent
gw.stop();                               // close; resets state
gw.getReadState(channelId)               // -> { lastReadId, mentionCount } | null
gw.isUnread(channelId, channelLastMsgId) // -> bool (snowflake compare; null map → false)
```

The module exposes a minimal interface. **No globals**. Tests inject a fake
`WebSocket` constructor for the state machine; live tests use the real one in
the emulator.

## 4. Wire-format change for OP_CHANNELS

The current channel row is 5 fields:
`[kind, id, name, parentIndex, lastMessageId]`

v1.1 appends 2 fields, making 7 total:
`[kind, id, name, parentIndex, lastMessageId, unread, mentionCount]`

- `unread`: `'1'` if gateway map shows `last_message_id > last_read_message_id` for
  the channel, else `'0'`. Empty string for category rows.
- `mentionCount`: decimal string of mention count, `'0'` (or empty) when none.

The C parser in `channel_list.c::on_rows_done` will read `w->fields[5]` and
`w->fields[6]`, store them in `CRow`, and use them in `draw_row`. The existing
`wc_readstate_*` local-persist machinery becomes a **fallback only**: if the
gateway hasn't sent READY yet (`unread` field empty for all rows), fall back
to the local heuristic. Once READY lands, the gateway flag wins.

## 5. Channel-row visual

```
 #general              (just the channel name; no badge)
 #announcements ●      (white dot — unread, no mentions)
 #ping-me        [3]   (red filled circle + white "3" — mention count)
```

Render details:
- Dot: 6px white circle, baseline-aligned with text.
- Badge: red filled circle (`GColorRed` ≈ accent red of the theme; on Midnight
  use `GColorRed` for contrast). White centered number (font GOTHIC_14_BOLD).
  If `mentionCount > 99` show `"99+"`. Diameter 16px, right-aligned.
- Selected rows: invert against accent highlight — white outline dot, accent-on-
  white badge to remain visible. (Discord uses red even on highlighted rows.)
- Position: right edge of the row, before any chevron space. Indented channels
  keep the same left geometry as v1.

## 6. Watch → Discord ACK (`OP_ACK`)

When `chat_view.c::on_rows_done` already calls `wc_readstate_mark`, also
dispatch `OP_ACK` with the channel + newest-visible message id. pkjs sends
`POST /channels/{cid}/messages/{mid}/ack` with body `{"manual": true}`. No
response is shown on the watch; failures are silent (next gateway connect's
READY will re-establish truth either way).

We **deliberately** do not gate on gateway connection state — the REST ACK
works independently and Discord propagates it to the gateway.

## 7. Lifecycle / connection management

- `Pebble.addEventListener('ready', …)` already runs once when pkjs boots
  (i.e. when the user opens the watchapp). v1.1 adds a `gw.start()` call there
  (only if a token is set).
- When the user reconfigures the token via Clay, `gw.stop()` then `gw.start()`
  to pick up the new token.
- pkjs exits when the watchapp closes; WebSocket dies with it. By design.

This matches the user-confirmed shape (2026-06-08): "every time you open the
app, you see canonical state, and battery isn't burning while the watch is
asleep."

## 8. Testing strategy

**Unit tests (node --test):**
- `test/gateway.test.js` — exercise the state machine against an injectable
  fake `WebSocket`: HELLO → IDENTIFY → READY ingestion → MESSAGE_ACK update →
  isUnread queries → close + RESUME → INVALID_SESSION re-IDENTIFY.
- `test/model.test.js` — extend with: channel rows include the new unread +
  mention fields when a read-state lookup is passed in.
- `test/discord.test.js` — extend with: `ack(channelId, messageId)` builds the
  right request shape.

Target: keep `node --test` green, total tests grow from 73 → ~85.

**Emulator tests (recursive, automated):**
- Build + install in QEMU with the live token (seed-emulator-token.py).
- Walk: server list → pick channel list → screenshot (verify channel-row badge
  rendering with real Discord data). Try a channel with no unread, a channel
  with unread, and (if available) a channel with mentions.
- Open a channel that's marked unread → confirm chat shows → return → confirm
  channel is now marked read (ACK round-trip).
- Verify build memory report: footprint < 60KB, free heap > 65KB.
- Verify `arm-none-eabi-nm` shows no `strtol|atoi|strtok|reent` symbols.

**Hardware** is the user's job. The branch is left ready; the user verifies on
PT2 before merging to main and re-publishing.

## 9. Files touched

**New:**
- `src/pkjs/lib/gateway.js` — gateway client (~250 LOC).
- `test/gateway.test.js` — state-machine tests.

**Modified:**
- `src/pkjs/index.js` — instantiate gateway, wire ack handler, gate on token.
- `src/pkjs/lib/model.js` — `buildChannelTree(channels, readMap)`; new fields.
- `src/pkjs/lib/discord.js` — add `ack(channelId, messageId)`.
- `src/pkjs/lib/settings.js` — no change expected.
- `src/c/channel_list.c` — parse 2 new fields; draw dot/badge.
- `src/c/channel_list.h` — bump CRow struct.
- `src/c/chat_view.c` — dispatch `OP_ACK` alongside `wc_readstate_mark`.
- `src/c/ui_util.{c,h}` — helpers `wc_draw_unread_dot`, `wc_draw_mention_badge`.
- `package.json` — add `OP_ACK` and `UNREAD`/`MENTIONS` messageKeys (only if
  needed for the ack flow; channel-row fields ride in existing ROWS).
- `docs/HANDOFF.md` — append a v1.1 "shipped" section once done.
- `test/model.test.js`, `test/discord.test.js` — extend coverage.

## 10. Risks + mitigations

| Risk | Mitigation |
|------|-----------|
| pypkjs `CloseEvent` doesn't expose `.code` correctly (bug in `ws.py:177`) | Don't depend on code in emulator — treat any close as "reconnect+RESUME, fall back to IDENTIFY on INVALID_SESSION". Real phone runtime is unaffected. |
| Discord rate-limit on REST `ack` | One ACK per channel-open is well under limits. No backoff needed. |
| Snowflake string compare (length-then-lex) fails for IDs > 2^53 | We already do this correctly in `readstate.c::wc_snowflake_gt`. Mirror in JS. |
| READY can arrive after the first OP_CHANNELS fetch | Fallback to local read-state heuristic when gateway map is empty. Next OP_CHANNELS request (or category-collapse, etc.) picks up canonical state. |
| WebSocket disconnect during long chat session | Silent reconnect with backoff. Worst case: unread flags lag by ~30s after disconnect. Acceptable per user. |
| Token exposed in IDENTIFY logs | pkjs `console.log` is local to the phone; we never log full token. |
| Hardware-only crash (newlib, UTF-8) | The 2 hard-won gotchas are already enforced: no newlib parse fns; route external strings through `wc_utf8_copy`. New code adds no new exposure. |

## 11. Definition of done

1. `node --test` green with new tests.
2. `pebble clean && pebble build` clean; memory report shows footprint /
   free-heap within v1.0 envelope.
3. `arm-none-eabi-nm build/emery/pebble-app.elf | grep -iE 'strtol|atoi|strtok|reent'`
   returns empty.
4. Emulator screenshots show channel-row unread dot + mention badge rendering
   correctly with live Discord data.
5. Emulator demonstrates: open channel marked unread → ACK propagates → on
   re-entering channel list it's read.
6. Branch `feat/v1.1-gateway` pushed; HANDOFF.md updated with what landed and
   what the user must hardware-verify before tag.
7. Code commits are atomic and descriptive (gateway, model, C parser, draw,
   ack, tests, docs as separate commits).
