# Wristcord v1.2 — DMs, server ping markers, mention bg

**Date:** 2026-06-09 · **Branch:** `feat/v1.2-dms-server-pings`
**Target:** Emery (PT2) · **Builds on:** v1.1 (gateway client + per-channel unread)

## 1. Summary

v1.2 makes the watchapp feel like a full Discord client by adding:

1. **Per-server ping markers** — small red badge overlaid on the
   bottom-right of each server's initials disc, mirroring Discord's
   guild-icon UX. Capped at "9+".
2. **Direct Messages** — a new section on the home page above the server
   list. Top N most-recent DM threads inline, with a "Show all DMs"
   row that opens the full sorted list. Tap into a DM to read +
   reply via the existing chat/compose flow.
3. **Mention-message background** — messages that @mention the current
   user render with a goldish background tint so they're scannable at
   a glance in a long channel.
4. **Home-page reflow** — three sections, in order: Settings, Direct
   Messages, Servers. The first DM row is selected by default on launch.
5. **Clay config:** integer counts for "how many DMs" and "how many
   servers" to preview inline on the home page (range 3–20 each).

Inline accent coloring of @user mention spans inside message bodies is a
stretch goal. If it lands cleanly with the existing packMessages output,
included; otherwise deferred to v1.3 — the gold bg is the load-bearing
signal.

## 2. Data sources

### 2.1 Per-guild ping aggregate

Discord's READY payload includes `d.guilds[]`, each guild has a
`channels[]` array with `id` and `last_message_id`. The gateway already
maintains a per-channel read map. Cross-joining the two gives
`{ unread: bool, mentionCount: number, mostRecentMessageId }` per guild
without any extra REST calls.

The gateway stores `guildIndex: Map<guildId, [{channelId, lastMessageId}]>`
at READY ingestion and refreshes it on `GUILD_CREATE` (covers joining
new guilds mid-session). Stats are computed on-demand from the live
read map.

### 2.2 DM channels

Two sources, in priority:

1. **READY.d.private_channels[]** — full list at IDENTIFY time, with
   `id`, `type` (1=DM, 3=group), `recipients[]`, `last_message_id`,
   `name` (for group DMs).
2. **REST GET /users/@me/channels** — fallback if private_channels is
   missing or empty (some account types).

The gateway stores them in `dms: Map<channelId, dmRecord>` and refreshes
on `CHANNEL_CREATE` / `CHANNEL_UPDATE` dispatches.

### 2.3 My user ID

Needed for mention detection. Pulled from `READY.d.user.id` at gateway
READY; stored as `gateway.getMyUserId()`. Falls back to REST
`GET /users/@me` if missing.

## 3. New AppMessage ops

| OP | Direction | Purpose |
|----|-----------|---------|
| 7  | C → pkjs  | `OP_HOME` — fetch home-page payload (settings row + DM section + server section) |
| 8  | C → pkjs  | `OP_DMS_ALL` — fetch full DM list (Show all DMs) |

`OP_GUILDS` is kept for the "Show all Servers" path; it already
returns the full sorted server list with folders.

`OP_CHANNELS` is unchanged.

`OP_MESSAGES` payload extended: each message row gets a 7th field
`mention_self` ('1'/'0') for mention-bg rendering.

### 3.1 OP_HOME wire format

A single paged ROWS response, three logical sections concatenated:

| field 0 (kind) | meaning |
|----------------|---------|
| `S`            | Settings entry (always row 0) |
| `H`            | section header — fields[1] = section label, fields[2] = icon code |
| `D`            | DM row — fields[1]=channel_id, fields[2]=display_name, fields[3]=accent_hex, fields[4]=mention_count, fields[5]=unread_bool |
| `M`            | "Show all" row — fields[1]=section_id ('dm' or 'server') |
| `g`            | guild row (top-N preview) — same as OP_GUILDS guild shape + fields[6]=ping_count, fields[7]=unread_bool |

The dispatcher in `server_list.c` switches on `kind` and dispatches each
row to the right rendering branch. This keeps OP_HOME a single fetch
(one network round trip, one ROWS response).

### 3.2 OP_DMS_ALL wire format

Same `D` row shape as OP_HOME, all DMs, sorted by `last_message_id` desc.

## 4. C rendering changes

### 4.1 server_list.c — home page rewrite

The window becomes a MenuLayer with **sections**:

```
Section 0 (no header):  Settings
Section 1 (header "Direct Messages" + icon):  N DM rows + optional "Show all DMs"
Section 2 (header "Servers" + icon):  N guild rows + optional "Show all servers"
```

Default selection: section 1 row 0 (first DM). If the user has no
DMs, default to section 0 row 0 (Settings) to avoid landing on a "Show
all DMs" or empty row.

Section headers draw with a small icon on the left and the section
label. Hand-drawn icons (no bitmaps):

- Settings: 3 horizontal sliders with circular knobs (~14×14)
- Direct Messages: speech-bubble outline with a single dot inside (~14×14)
- Servers: 3×3 grid of small filled squares (~14×14)

### 4.2 Server row — ping marker

The existing initials-disc render gains a small red ping marker
overlaid on the bottom-right when `ping_count > 0`. Marker spec:

- 14px disc, red fill (`GColorRed`)
- 2px stroke matching the screen background color, drawn as a slightly
  larger background-filled disc beneath the red one (cheap "outline")
- Centered text: GOTHIC_14_BOLD, white. "1"–"9" or "9+" (>9)
- Positioned so its center sits at (disc_x + disc_radius * 0.7,
  disc_y + disc_radius * 0.7) — Discord-style bottom-right overlap

The ping marker uses an unread+mention aggregate count, not a strict
mention count: any unread channel counts as 1, plus mention contributions.
Mirrors Discord's badge semantics (red dot = any unread, number = mentions).

### 4.3 DM row

Mirrors server row layout:

- Initials disc on the left, accent color hashed from the display name
  (same `nameToAccentHex` palette as servers — copies the server impl)
- Display name to the right
- Ping marker on the disc bottom-right if mention_count > 0 OR unread
- For group DMs, the disc shows up to 2 initials from the group name,
  same as a server

DM "channel name" for the chat-view title bar: `@username` (1:1) or
the group name (group).

### 4.4 Mention message bg (chat_view.c)

Per-message `mention_self` flag determined in pkjs from
`msg.mentions.some(u => u.id === myUserId)`. The C side fills the cell
background with a fixed gold-ish color (`GColorYellow` is too bright;
use `GColorChromeYellow` on Emery or a hand-mixed warm tint).

Stretch — inline accent on @user span — needs packMessages to emit
`[pre, mention, post]` triplets. If we do this we render via 3 draw
calls per message line. Estimated +30 minutes; tackle only if the bg
is already shipping.

## 5. Clay config additions

```js
{
  type: 'slider',         // pebble-clay 1.0.4 supports a slider input
  appKey: 'SET_DM_COUNT',
  label: 'DMs on home page',
  min: 3, max: 20, step: 1, default: 3
},
{
  type: 'slider',
  appKey: 'SET_SERVER_COUNT',
  label: 'Servers on home page',
  min: 3, max: 20, step: 1, default: 3
}
```

`messageKeys` in `package.json` gains `SET_DM_COUNT` and
`SET_SERVER_COUNT`. `settings.js` normalizes them (clamp 3..20).
`watchSubset` includes them so the watch knows the cap when it asks
pkjs for the home payload (the watch sends `LIMIT_DMS`, `LIMIT_SERVERS`
as part of `OP_HOME`).

## 6. Hand-drawn icons (no bitmap resources)

Section headers each get a single inline-drawn 14×14 icon via a new
helper `wc_draw_section_icon(ctx, box, kind)`:

- `WC_ICON_SETTINGS` — gear silhouette (8-sided shape with center hole)
- `WC_ICON_DM` — speech bubble (rounded rect + small triangle tail)
- `WC_ICON_SERVERS` — 3×3 dot grid

Programmatic so no resource budget is consumed and the icons inherit
the theme foreground.

## 7. Sort policy

- DMs: sorted by `last_message_id` desc (newest activity first).
- Servers: sorted by `mostRecentMessageId` across all channels in the
  guild, desc.

Both are deterministic, both use Discord-native data, neither needs an
extra "frequently messaged" counter (which Discord doesn't expose
through the gateway anyway — would require message-volume tracking
that we can't do without significant local persistence).

## 8. Tests

`test/gateway.test.js`:
- `getGuildStats(guildId)` returns 0/0 for guild with no read_state
  entries, increments unread + mentionCount correctly on a fixture
- `getAllDMs()` sorts type 1 + type 3 by last_message_id desc

`test/model.test.js`:
- new `buildHomePage(dms, guilds, settings, limits)` test covers
  section markers, ping joining, sort order, "Show all" emission

`test/discord.test.js`:
- `me()` already covered; no changes needed

`test/settings.test.js`:
- `SET_DM_COUNT` / `SET_SERVER_COUNT` clamp to [3, 20]

Target: 97 → ~115 tests, all green.

## 9. Risks

| Risk | Mitigation |
|------|-----------|
| OP_HOME payload too big for paged ROWS buffer | Cap inline previews to 20; if user sets max=20 both, worst case ~42 rows, well under WC_ROWS_BUF=4096 |
| READY.d.guilds is enormous (1241 channels in user's account) | Already loaded for v1.1 read_state; same data, no extra cost |
| READY.d.private_channels[] sometimes empty for new accounts | REST fallback `GET /users/@me/channels` |
| Group DM names sometimes blank | Fall back to comma-joined recipient names |
| Default selection on first DM row breaks if user has no DMs | Fall back to Settings (section 0 row 0) |
| MenuLayer sections + custom headers can flicker on the small Emery display | Test in emulator; hand-tune row heights |
| Section header height eats screen real estate | Make headers compact (24px), use the icon to save text space |

## 10. Definition of done

1. `node --test` green with new tests (~115+ total).
2. `pebble clean && pebble build` clean; memory still within budget.
3. `arm-none-eabi-nm` shows no banned firmware-faulting symbols.
4. Emulator: home page renders 3 sections, DMs section selected by
   default, server icon ping markers visible on unread guilds, DM
   list opens via "Show all DMs", mention message renders with gold bg
   in chat view.
5. Clay config has working DM/Server count sliders.
6. Squash-merged onto main and pushed.
