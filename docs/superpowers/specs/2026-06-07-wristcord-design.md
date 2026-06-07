# Wristcord — Design Spec

**A Discord client for the Pebble Time 2 (Emery).**
Date: 2026-06-07 · Status: approved design, pre-implementation · Target: Emery (200×228, color, touch)

---

## 1. Summary

Wristcord is a Pebble watchapp that lets you browse your Discord servers, read text
channels, and send messages by voice — driven by your own account token (selfbot). It is
a personal-use client for a single account, with an explicit in-config consent gate.

The watch has no internet; all network access happens in **PebbleKit JS (pkjs)** on the
phone, which calls Discord's REST API and streams results to the C watchapp over **AppMessage**.

### Scope at a glance

**In v1:**
- First-run onboarding (tutorial if token set; otherwise points to Clay config)
- Server list, sorted as in the Discord app, with **folders** (collapsed by default)
- Channel list per server with **categories** (expanded by default), collapsible; collapse
  state persists across launches
- Channel chat view: most-recent messages, newest at bottom, scrollable
- **Send a message** to the current channel via the system dictation flow → Send/Redo/Cancel
- Touch as an accelerator (drag-scroll, tap-to-open) — never the only path
- Clay config: token (behind ToS consent gate), theme, accent color, poll interval
- Midnight default theme, accent-adaptive palette
- Server "icons" = colored dot + initials (auto color from name hash); folder = 2×2 circle micro-grid

**Explicitly deferred (v2+):**
- Discord-style quoted replies (tap a message → reply/jump-to-original)
- Real downloaded server icons (needs an image-proxy service)
- Real-time gateway/WebSocket push (v1 polls)
- DMs / group DMs, reactions, attachments/images, threads, message editing/deletion
- Sorting servers by most-recent-activity (only if trivial; not planned for v1)
- Typing indicators, presence, unread badges/counts

---

## 2. Architecture

```
┌──────────────────────────┐     AppMessage (BT, paged)    ┌────────────────────────────┐     HTTPS / XHR     ┌──────────────┐
│  Watch app (C / Pebble)  │ ◀───────────────────────────▶ │  PebbleKit JS (phone)      │ ◀─────────────────▶ │ Discord REST │
│  - UI: 4 window types     │   request: {op, id, page}     │  - holds token (Clay)      │   Authorization:    │  api/v10     │
│  - input: buttons + touch │   response: {op, items…}      │  - Discord API client      │   <user token>      │              │
│  - persist: collapse state│                               │  - rate-limit + poll mgr   │                     │              │
│  - in-RAM data model      │                               │  - paging/chunking to watch│                     │              │
└──────────────────────────┘                               └────────────────────────────┘                     └──────────────┘
```

### Two programs, one PBW

- **C watchapp** — renders screens, handles input, holds the current screen's data in RAM,
  persists UI state (collapse sets, last location). Knows nothing about HTTP.
- **pkjs** — the only component that touches the network. Owns the token, talks to Discord,
  manages rate limiting and polling, and translates Discord JSON into compact paged
  AppMessage payloads.

### Why this split
The watch can't make network calls and has tiny RAM and a tiny message channel. Keeping all
Discord knowledge in pkjs means the C side stays a thin, testable renderer over a simple
message protocol, and the data-volume problem is solved once (in the paging layer) rather
than per-screen.

---

## 3. The AppMessage paging protocol

AppMessage carries only small key-value dicts, so no list is sent whole. One generic,
reusable request/response pattern serves every screen.

### Request (C → pkjs)
| key | meaning |
|-----|---------|
| `OP` | what to fetch: `GUILDS`, `CHANNELS`, `MESSAGES`, `SEND`, `BOOTSTRAP` |
| `ID` | context id (guild id for `CHANNELS`, channel id for `MESSAGES`/`SEND`) as a string |
| `PAGE` | page cursor / batch index (0-based) |
| `TEXT` | message body (for `SEND` only) |

### Response (pkjs → C), one batch per AppMessage
| key | meaning |
|-----|---------|
| `OP` | echoes the request op |
| `PAGE` | which batch this is |
| `MORE` | 1 if more batches follow, 0 if this is the last |
| `COUNT` | items in this batch |
| `ITEM_n_*` | packed fields per item (see item shapes below) |
| `ERR` | optional error code (auth failed, rate-limited, network) |

pkjs sends batches of **N items** (N tuned to fit the outbox, ~5–10 to start), waits for the
C-side `outbox`/`inbox` ack cadence, and continues until `MORE=0`. The C side appends each
batch to its in-RAM list and re-renders incrementally (so the user sees rows populate,
matching the "loading" chat state). Buffers opened generously: `app_message_open(2048, 256)`
(tune during implementation).

### Item shapes (packed, truncated to fit)
- **Guild/folder item:** `kind` (folder|guild), `id`, `name` (≤ ~28 chars), `color` (folder
  color or hashed server color), `parent` (folder id or none), `member_colors` (for folder
  micro-grid: up to 4 packed colors).
- **Channel item:** `kind` (category|text), `id`, `name` (≤ ~24 chars), `parent` (category
  id), `position`.
- **Message item:** `author` (≤ ~18 chars), `author_color`, `ts` (epoch or "h:mm"),
  `text` (≤ ~120 chars, truncated with ellipsis).

Tree structure (folders→guilds, categories→channels) is computed **in pkjs** from Discord's
`position`/`parent_id` and sent already-ordered and already-flattened, so the C side just
renders rows in receive order with an indent flag.

---

## 4. Discord API layer (pkjs)

All requests use `Authorization: <user token>` (no `Bot ` prefix). Base `https://discord.com/api/v10`.

| Need | Endpoint |
|------|----------|
| Server order + folders | `GET /users/@me/settings` → `guild_positions`, `guild_folders` |
| Servers | `GET /users/@me/guilds` |
| Channels (+ categories) | `GET /guilds/{guild.id}/channels` (type 4 = category, type 0 = text) |
| Recent messages | `GET /channels/{channel.id}/messages?limit=N` (N≈20) |
| Send message | `POST /channels/{channel.id}/messages` `{content}` |

### Ordering
Server list = `guild_folders` order, with folder membership and top-level guilds interleaved
exactly as the Discord app lays them out; guilds not referenced by any folder appear at top
level in `guild_positions` order. Channels = sorted by `position` within each category;
categories by their own `position`; uncategorized channels render above the first category.

### Rate limiting & polling
- Respect Discord's `X-RateLimit-Remaining` / `Retry-After`; on 429, back off and surface a
  soft error to the watch (no hammering).
- **Poll only the open channel**, at the user-chosen interval (Off / 5s / 10s / 30s; default
  10s). Stop polling when the chat view closes. This keeps traffic low — friendlier to both
  battery and Discord's bot-detection.
- Fetch on demand otherwise (guild list on launch, channels when a server is opened, messages
  when a channel is opened).

---

## 5. Watch app structure (C)

### Windows
1. **Onboarding** (first run only) — if a token is configured: a short 2–3 card tutorial
   (navigate, collapse, speak-to-send). If no token: a single card explaining how to open
   Clay config in the phone app and paste a token. Re-shown until a token exists.
2. **Server list** — `MenuLayer`. Slim folder rows (2×2 circle micro-grid icon, name, count,
   chevron) interleaved with server rows (colored dot + initials). Folders collapsed by
   default. SELECT on a folder toggles collapse; SELECT on a server opens its channels.
3. **Channel list** — `MenuLayer`. Slim category rows (chevron + name) and `#` channel rows
   (indented). Categories expanded by default; collapse state persisted. SELECT on a category
   toggles; SELECT on a channel opens the chat.
4. **Chat view** — `ScrollLayer` (not a menu). Messages newest-at-bottom, author in user
   color, dimmed timestamp; own messages rendered like any other. UP/DOWN scroll, **SELECT
   starts the send flow**, BACK returns to channels. Slim bottom compose hint ("🎤 SELECT to
   send"). States: normal, empty, loading.
5. **Send result** — our screen after system dictation returns. `ActionBarLayer`:
   **SELECT = Send, UP = Redo, DOWN = Cancel** (BACK also cancels). On STT failure, becomes
   the fallback: **SELECT = Retry, UP = Quick messages, DOWN = Cancel**.

### Send flow detail
`SELECT` in chat → `dictation_session_start()` with **built-in confirmation OFF** and error
dialogs OFF (we render our own). The OS shows mic → progress, then calls our callback:
- `Success` → push **Send result** screen with the transcript.
- `NoSpeechDetected` / `ConnectivityError` / `Disabled` / rejected → push the **fallback**
  variant (retry / quick canned messages / cancel).
On Send: AppMessage `OP=SEND, ID=<channel>, TEXT=<transcript>`; pkjs POSTs; on success the
new message appears on the next poll (or we optimistically append).

### Input rules (from pebble-watchapp skill)
- Every action reachable by **buttons**; touch only supplements (drag-scroll lists/chat,
  tap a row to open). Guard touch with `touch_service_is_enabled()` at runtime.
- Exactly-3-action screens use the ActionBar.

---

## 6. Persistence (C `persist_*`)

Budget: 256 bytes/value, ~4 KB total. We store **only UI state**, never message content or
the token (token lives in Clay/localStorage on the phone side).

| Key | Contents |
|-----|----------|
| `PK_LAST_GUILD`, `PK_LAST_CHANNEL` | resume location (optional jump-back on launch) |
| `PK_COLLAPSED_FOLDERS` | set of collapsed folder ids |
| `PK_COLLAPSED_CATS_<guild>` | set of collapsed category ids for a guild |
| `PK_THEME`, `PK_ACCENT`, `PK_POLL` | mirror of Clay settings for instant boot styling |

**ID packing:** Discord snowflakes are 64-bit (8 bytes). To respect the 256-byte/value cap,
store collapse sets as packed `uint64` arrays (≤ 32 ids/value) and, if needed, shard across
numbered keys. Folders default expanded=false (collapsed); categories default expanded=true,
so we **only need to store the exceptions** (collapsed categories / expanded folders), which
keeps the sets tiny in practice.

---

## 7. Clay configuration (phone)

A Clay HTML config page, opened from the Pebble phone app.

### Consent gate
The page **first** shows a required acknowledgment checkbox; the rest of the form (token,
theme, etc.) is hidden until it is checked. Wording acknowledges, in plain language:
- selfbotting/automating a user account is **against Discord's Terms of Service** and **can
  get the account banned**;
- the **user accepts all risk** to their account;
- the **author is not responsible** for anything that happens to the account or data.

### Fields (after consent)
1. **Discord token** (required, password field) — stored via Clay/localStorage; used only by
   pkjs for API calls.
2. **Theme** — Dark / Light / Midnight (default **Midnight**).
3. **Accent color** — color picker, default **`GColorVeryLightBlue` (`0x5555FF`)**. Drives the
   accent-adaptive palette (selection, "you" name, mic, action-bar primary, theme tint) so the
   palette stays coherent for any accent — no clashing combinations.
4. **Message poll interval** — Off / 5s / 10s / 30s (default 10s).

Settings flow to pkjs on change and are mirrored to watch persist for instant styling at boot.

---

## 8. Theming

- **Three preset bases** (Dark / Light / Midnight) define neutral background/surface colors.
- The **accent injects color** into: selection highlight, the "you" author name, the mic
  button, and the ActionBar primary. The chosen base **tints toward the accent hue** (e.g.
  Midnight + red → dark-red-tinted), guaranteeing readable, non-clashing combinations.
- **No left-aligned accent stripes / tinted highlight bars** on rows or messages (reads as
  AI-generated). Distinction comes from content-native means (author color, the standard
  full-row Pebble selection invert).
- All colors are Pebble 64-color palette values (`GColorFromHEX`).

### Visual identity (from the approved mockups)
- Server icon: colored circle + 1–2 letter initials; color auto-derived from a hash of the
  server name (deterministic, no network).
- Folder icon: 2×2 grid of **circles** (member-server colors), tinted by folder color.
- Group rows (folders, categories) are **slim** but are first-class selectable list rows.
- Animations: lean on `Animation`/`PropertyAnimation` for collapse/expand and window
  transitions to feel "fun" and classic-Pebble-smooth.

---

## 9. Error & edge handling

| Situation | Behavior |
|-----------|----------|
| No token configured | Onboarding routes to Clay config; no API calls attempted |
| Invalid/expired token (401) | Watch shows "Sign-in failed — check token in config" |
| Rate limited (429) | pkjs backs off; watch shows transient "Slow down…" then resumes |
| No phone/BT/network | "Disconnected" state; retry on reconnect (ConnectionService) |
| Dictation failure | Fallback action-bar (retry / quick messages / cancel) |
| Empty channel | Empty state with "SELECT to send the first message" |
| Long lists/messages | Paged transfer; names/messages truncated with ellipsis |

---

## 10. Build/test notes

- Built with the `pebble-watchapp` skill flow; default and only target **Emery**.
- `package.json`: `watchapp.watchface=false`, `enableMultiJS=true`, `messageKeys` for the
  protocol above, `capabilities` incl. `configurable`.
- Emulator testing: drive each window via QEMU button/touch injection, screenshot-verify
  every state. **Always `pebble kill` after emulator use** (QEMU captures the mouse).
- The Discord layer can be exercised against real endpoints from pkjs with a test token.

---

## 11. Open implementation questions (resolve during planning)

- Exact batch size N and AppMessage buffer sizes (measure against real payloads).
- Whether to optimistically append a just-sent message or wait for the next poll.
- Onboarding tutorial card count/content.
- Quick-messages fallback list contents (canned phrases / emoji set).
```
