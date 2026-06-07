# Wristcord M2 — Discord REST + Paging Data Layer — Plan

> Executed via subagent-driven-development. Pure JS gets node:test TDD; the live Discord client is validated read-only against the real account (token in `.secrets/discord_token`).

**Goal:** A node-testable PebbleKit JS data layer that fetches Discord servers/channels/messages with the user token, transforms them into the watch's display model, and serializes them into the AppMessage paging protocol the C side will consume in M3–M5.

**Architecture:** Pure transform/encode modules (no IO) + a thin injectable HTTP adapter, wired into `src/pkjs/index.js`'s AppMessage OP router. The watch sends `{OP, ID, PAGE}`; pkjs fetches, builds the ordered display model, and replies with record-delimited `ROWS` string batches.

## AppMessage paging protocol (concrete)

Dynamic per-item keys aren't possible (AppMessage keys are predeclared), so a batch is ONE delimited string under `ROWS`.

**Request (C → pkjs):**
- `OP` (uint8): 1=GUILDS, 2=CHANNELS, 3=MESSAGES, 4=SEND (M6)
- `ID` (string): context id — guild id for CHANNELS, channel id for MESSAGES/SEND ("" for GUILDS)
- `PAGE` (uint8): batch cursor, 0-based
- `TEXT` (string): message body (SEND only, M6)

**Response (pkjs → C):**
- `OP` (uint8): echoes request
- `PAGE` (uint8): this batch index
- `MORE` (uint8): 1 if more batches follow, else 0
- `ROWS` (string): records joined by RS (0x1E), fields within a record by US (0x1F)
- `ERR` (uint8, optional): 1=auth, 2=ratelimited, 3=network/other

**Record field layouts (by OP):**
- GUILDS: `kind, id, name, color, parentIndex, memberColorsCSV`
  - `kind`: `f`=folder, `g`=guild. Folders are collapsed by default (C decides render); `parentIndex` is the row index of the owning folder or "" for top level. `memberColorsCSV` = up to 4 hex colors for a folder's 2×2 micro-grid (empty for guilds). `color` = guild dot color (from name hash) or folder color.
- CHANNELS: `kind, id, name, parentIndex`
  - `kind`: `c`=category (expanded by default), `t`=text channel. `parentIndex` = row index of owning category or "" if uncategorized.
- MESSAGES: `author, color, time, text`
  - newest LAST in the array (C appends; renders newest at bottom). `text` truncated to ~120 chars. `time` = "h:mm".

Batch size: target ≤ ~1.5 KB of ROWS per message (tune). Outbox opened larger on C side in M3.

## Modules

### `src/pkjs/lib/discord.js` (HTTP via injected adapter)
```
makeClient(request)  // request(method, path, {token, body}) -> Promise<{status, json}>
  .me()                       -> GET /users/@me
  .guilds()                   -> GET /users/@me/guilds
  .userSettings()             -> GET /users/@me/settings  (guild_folders for ordering)
  .channels(guildId)          -> GET /guilds/{id}/channels
  .messages(channelId, limit) -> GET /channels/{id}/messages?limit=limit
```
Adapter is injected so node tests mock it, the real-account probe uses https, and pkjs uses XHR. No token handling beyond passing it to the adapter.

### `src/pkjs/lib/model.js` (pure)
```
buildServerList(guilds, userSettings) -> [{kind,id,name,color,parentIndex,memberColors[]}]
  // Order from userSettings.guild_folders: each entry is either a single guild (standalone)
  // or a folder {id,name,color,guild_ids[]}. Standalone -> one 'g' row. Folder -> one 'f' row
  // (color from folder.color or first member; memberColors = up to 4 member dot colors) followed
  // by its 'g' rows with parentIndex = the folder row. Guilds absent from folders append after,
  // in guilds order. Guild dot color from color.nameToAccentHex(name). Unnamed folder -> "Folder".
buildChannelTree(channels) -> [{kind,id,name,parentIndex,position}]
  // categories (type 4) ordered by position; text channels (type 0) grouped under their
  // parent_id category (ordered by position); uncategorized text channels first (parentIndex "").
packMessages(messages) -> [{author,color,time,text}]
  // reverse to chronological (oldest first), author color via nameToAccentHex(author),
  // time "h:mm" from timestamp, text truncated 120 + ellipsis.
```

### `src/pkjs/lib/paging.js` (pure)
```
encodeRows(records) -> string         // records: array of field-arrays; join US/RS, strip US/RS from fields
batches(rowString, maxLen) -> [string]// split by whole records to keep each <= maxLen
RS = '\x1e', US = '\x1f'
```

### `src/pkjs/index.js` (router, integration)
Add `appmessage` listener: read OP/ID/PAGE, fetch via discord client (token from localStorage), build model, encode rows, send batch for PAGE with MORE flag. Errors → `ERR`.

## Validation
- node:test for model.js (fixtures: folders incl. standalone+named+unnamed; categorized+uncategorized channels; message truncation/time/order) and paging.js (delimiter stripping, record-boundary batching).
- discord.js tested with a mocked adapter (status passthrough, path building).
- `scripts/probe-real.js` (committed; reads token from .secrets, never prints it): runs guilds()/userSettings()→buildServerList, channels() on one guild→buildChannelTree, messages() on one channel→packMessages; prints counts + a few names ONLY. Read-only.

## Acceptance
- All node tests pass.
- probe-real prints a sane ordered server list (folders + standalone), a channel tree, and recent messages for a real server/channel — read-only, no sends.
- No token printed anywhere; `.secrets/` stays ignored.
