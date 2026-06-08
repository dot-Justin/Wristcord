# Wristcord M6 + M7 — Send, Chat Interaction, Unread, Polish — Plan

Executed via subagent-driven-development with verification (build / node tests / emulator) between tasks.

## M6 — Send + chat interaction rework + local unread

**M6-T1 (pkjs):** extend the data/router layer (model tests where pure):
- `model.buildChannelTree` adds `last_message_id` to each text-channel record → CHANNELS record becomes `[kind, id, name, parentIndex, last_message_id]`.
- `model.packMessages` returns per message `{author,color,time,text(truncated),full(untruncated cleaned),id,truncated}` → MESSAGES list record becomes `[author, color, time, text, id, truncated]`.
- Router caches the last channel's messages; new ops:
  - `OP_SEND=4`: `{OP, ID:channel, TEXT}` → `discord.sendMessage` → reply `{OP:4, ERR:0|code}`.
  - `OP_MSG_FULL=5`: `{OP, ID:messageId, PAGE}` → look up cached message by id → send its full cleaned text as paged ROWS.
- New messageKeys: none beyond existing (reuse OP/ID/PAGE/MORE/ROWS/TEXT/ERR). Add `MSG_ID`? No — message id rides inside the MESSAGES record + is sent back as ID for OP_MSG_FULL.

**M6-T2 (C chat interaction):** rework `chat_view.c`:
- Visible MenuLayer selection (remove invisible-highlight trick).
- `select_click` → compose (dictation) for the current channel — always.
- `select_long_click` → per-message `ActionMenu`: `Read full message` (only if msg truncated); `Reply`/`React` as disabled "soon" items.
- Parse new MESSAGES record (id, truncated). Truncated rows show a dim ` …more`.
- `Read full` → push a scrollable detail window that requests `OP_MSG_FULL(ID=msg id)` and renders the full text (ScrollLayer or single tall text).

**M6-T3 (C send flow):** dictation → action bar:
- `dictation_session_create/start` (confirmation off, error dialogs off).
- Success → result window with `ActionBarLayer`: SELECT=Send, UP=Redo, DOWN=Cancel (BACK cancels).
- Failure (NoSpeech/Connectivity/Disabled/rejected) → fallback: Retry / Cancel (quick-messages deferred).
- Send → AppMessage `OP_SEND, ID=channel, TEXT`; on ack, optimistic: refetch channel messages (re-fetch OP_MESSAGES) so the sent message appears.

**M6-T4 (C local unread):** in `channel_list.c`:
- Parse `last_message_id` per text channel.
- Persisted read-state: `channel_id -> last_seen_message_id` (persist_write_data blob; cap ~100 entries; snowflake string compare bigger=newer).
- Baseline: first time a guild's channels load with no stored state for a channel, seed `last_seen = last_message_id` (so nothing shows unread until new activity).
- Mark read: when a chat opens, set that channel's `last_seen = its newest message id`.
- Render: unread channel = small white dot left of `#` + name in theme-fg (white); read = no dot + dimmed gray name. (Caveat: local-only; no gateway sync.)

**M6 validation:** emulator browse + open channel (unread→read transition), Read-full screen, dictation flow (note: emulator likely can't do real STT → exercises fallback). One live send test to "Justin's testing server" to prove the POST path, verified by the message appearing on refetch.

## M7 — Polish

**M7-T1:** per-screen titles — show guild name on channel list, channel name (`#name`) on chat, in the top bar (custom title text in/over the status bar).
**M7-T2:** onboarding — first-run tutorial cards (navigate, click/tap=send, long-press=actions); in-app **Help/Settings** via an app-level `ActionMenu` on long-press of the server list (Help replays the tutorial; Settings = "open Wristcord settings in the phone app" note).
**M7-T3:** theming/animation — Midnight accent-adaptive tint; verify Light theme legibility across all screens; collapse/expand + window-push animations feel smooth; confirm no AI-style highlight bars.
**M7-T4:** cross-theme emulator validation + refreshed screenshots; final review; merge to main.
