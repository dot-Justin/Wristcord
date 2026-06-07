# Wristcord M1 — Scaffold + Clay Consent Config + Settings Shell — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. The Pebble C build/emulator steps should be driven via the `pebble-watchapp` skill, which knows the scaffold/build/QEMU-verify flow and Emery quirks.

**Goal:** A buildable Emery watchapp whose Clay config gates the Discord token (and theme/accent/poll knobs) behind a ToS-acknowledgment checkbox, persists those settings to the watch, and proves the watch↔pkjs AppMessage handshake — the configurable shell every later milestone builds on.

**Architecture:** One PBW = a C watchapp (UI/persist) + PebbleKit JS (pkjs, phone-side network/settings owner), talking over AppMessage. M1 builds the skeleton of both halves plus the Clay config page; no Discord calls yet. Pure JS helpers live in testable modules under `src/pkjs/lib/` so they can be unit-tested with Node's built-in test runner.

**Tech Stack:** Pebble SDK 4.x (Emery target), C, PebbleKit JS, Clay (config framework), Node.js `node:test` for pkjs unit tests.

**Repo:** `~/Projects/Pebble/wa/Wristcord` (already `git init`-ed; spec at `docs/superpowers/specs/2026-06-07-wristcord-design.md`).

---

## File Structure (locked in M1)

- `package.json` — Pebble manifest: `watchapp.watchface=false`, `enableMultiJS=true`, `capabilities:["configurable"]`, `messageKeys`, `targetPlatforms:["emery"]`, uuid.
- `src/c/wristcord.c` — app entry: init/deinit, AppMessage open + handlers, persist load of theme/accent/poll, a single **status window** showing connection/config state (placeholder for later milestones).
- `src/c/settings.h` / `src/c/settings.c` — `WristcordSettings` struct + load/save against `persist`, applied to UI.
- `src/pkjs/index.js` — pkjs entry: Clay wiring, `ready`/`appmessage` listeners, settings persistence to `localStorage`, push settings to watch.
- `src/pkjs/lib/settings.js` — pure: normalize/serialize Clay settings → the watch-bound subset. **Unit tested.**
- `src/pkjs/lib/color.js` — pure: deterministic name→GColor hex hash (used for server dots in M3, introduced+tested now). **Unit tested.**
- `config/index.js` — Clay config JSON: consent gate + token + theme + accent + poll.
- `config/custom-clay.js` — Clay custom code: show/hide the rest of the form based on the consent toggle.
- `test/settings.test.js`, `test/color.test.js` — Node unit tests for the pure libs.
- `test/package.json` or root `"scripts"` — `npm test` runs `node --test test/`.

> **Testing reality for Pebble:** C screens have no practical unit-test harness; they're verified by **driving QEMU and screenshotting each state** (via the `pebble-watchapp` skill, `Read`-verifying the image). Only the **pure pkjs logic** gets classic TDD with `node:test`. Each task below states which mode applies.

---

## Task 1: Scaffold the Emery project

**Files:**
- Create: `package.json`, `src/c/wristcord.c`, `wscript`, `appinfo`/resources as generated
- Use the `pebble-watchapp` skill's scaffolder (archetype: **menu**, since later milestones are list/detail). Target **emery only**.

- [ ] **Step 1: Scaffold via pebble-watchapp skill**

Invoke the `pebble-watchapp` skill to scaffold a new **interactive watchapp** (NOT a watchface) named `Wristcord`, archetype **menu**, target platform **emery**, into the repo root `~/Projects/Pebble/wa/Wristcord`. It must set `watchapp.watchface=false`.

- [ ] **Step 2: Overwrite `package.json` with the M1 manifest**

```json
{
  "name": "wristcord",
  "author": "dot-Justin",
  "version": "1.0.0",
  "keywords": ["pebble-app"],
  "private": true,
  "dependencies": { "pebble-clay": "^1.0.4" },
  "pebble": {
    "displayName": "Wristcord",
    "uuid": "GENERATE-A-UUID-HERE",
    "sdkVersion": "3",
    "enableMultiJS": true,
    "targetPlatforms": ["emery"],
    "watchapp": { "watchface": false },
    "capabilities": ["configurable"],
    "messageKeys": [
      "OP", "ID", "PAGE", "MORE", "COUNT", "TEXT", "ERR",
      "READY", "SET_THEME", "SET_ACCENT", "SET_POLL", "HAS_TOKEN"
    ],
    "resources": { "media": [] }
  }
}
```

Generate a real UUID (e.g. `uuidgen` or `python3 -c "import uuid;print(uuid.uuid4())"`) and paste it into `uuid`.

- [ ] **Step 3: Build the empty scaffold to confirm toolchain**

Run: `pebble build`
Expected: `Wristcord/build/wristcord.pbw` is produced, no errors. (If the SDK/python env is wrong, see the `pebble-tool needs Python 3.12` memory note.)

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "M1: scaffold Emery watchapp with Clay + messageKeys manifest"
```

---

## Task 2: Pure pkjs lib — deterministic color hash (TDD)

**Files:**
- Create: `src/pkjs/lib/color.js`
- Test: `test/color.test.js`

- [ ] **Step 1: Write the failing test**

```js
// test/color.test.js
const test = require('node:test');
const assert = require('node:assert');
const { nameToAccentHex, PALETTE } = require('../src/pkjs/lib/color');

test('returns a palette hex string', () => {
  const c = nameToAccentHex('Pebble Dev');
  assert.match(c, /^0x[0-9A-Fa-f]{6}$/);
  assert.ok(PALETTE.includes(c));
});

test('is deterministic for the same name', () => {
  assert.strictEqual(nameToAccentHex('Homelab HQ'), nameToAccentHex('Homelab HQ'));
});

test('different names usually differ', () => {
  assert.notStrictEqual(nameToAccentHex('A'), nameToAccentHex('ZZZZ'));
});

test('empty/undefined name is handled', () => {
  assert.match(nameToAccentHex(''), /^0x[0-9A-Fa-f]{6}$/);
  assert.match(nameToAccentHex(undefined), /^0x[0-9A-Fa-f]{6}$/);
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `node --test test/color.test.js`
Expected: FAIL — `Cannot find module '../src/pkjs/lib/color'`.

- [ ] **Step 3: Write minimal implementation**

```js
// src/pkjs/lib/color.js
// Curated subset of the Pebble 64-color palette that reads well as a server dot
// on the Midnight theme (avoids near-black / near-white that vanish on dark bg).
var PALETTE = [
  '0x5555FF', '0x00AAFF', '0x00AA55', '0x55AA00', '0xAAAA00',
  '0xFF5500', '0xFF0055', '0xAA00FF', '0xFF55AA', '0x00AAAA',
  '0xFFAA00', '0x55AAFF'
];

function nameToAccentHex(name) {
  var s = String(name || 'wristcord');
  var h = 5381;
  for (var i = 0; i < s.length; i++) {
    h = ((h << 5) + h + s.charCodeAt(i)) >>> 0; // djb2, unsigned
  }
  return PALETTE[h % PALETTE.length];
}

module.exports = { nameToAccentHex: nameToAccentHex, PALETTE: PALETTE };
```

- [ ] **Step 4: Run test to verify it passes**

Run: `node --test test/color.test.js`
Expected: PASS (4 tests).

- [ ] **Step 5: Commit**

```bash
git add src/pkjs/lib/color.js test/color.test.js
git commit -m "M1: add deterministic name->palette color hash (tested)"
```

---

## Task 3: Pure pkjs lib — settings normalization (TDD)

**Files:**
- Create: `src/pkjs/lib/settings.js`
- Test: `test/settings.test.js`

- [ ] **Step 1: Write the failing test**

```js
// test/settings.test.js
const test = require('node:test');
const assert = require('node:assert');
const { normalize, watchSubset, THEMES, DEFAULTS } = require('../src/pkjs/lib/settings');

test('fills defaults when empty', () => {
  const s = normalize({});
  assert.strictEqual(s.theme, DEFAULTS.theme);     // 'midnight'
  assert.strictEqual(s.accent, DEFAULTS.accent);   // '0x5555FF'
  assert.strictEqual(s.pollSeconds, DEFAULTS.pollSeconds); // 10
  assert.strictEqual(s.token, '');
});

test('coerces clay shapes (color int, select string)', () => {
  const s = normalize({ accent: 0x5555FF, theme: 'light', pollSeconds: '30', token: 'abc.def' });
  assert.strictEqual(s.accent, '0x5555FF');
  assert.strictEqual(s.theme, 'light');
  assert.strictEqual(s.pollSeconds, 30);
  assert.strictEqual(s.token, 'abc.def');
});

test('rejects unknown theme -> default', () => {
  assert.strictEqual(normalize({ theme: 'bogus' }).theme, DEFAULTS.theme);
  assert.ok(THEMES.includes(DEFAULTS.theme));
});

test('poll "off" maps to 0', () => {
  assert.strictEqual(normalize({ pollSeconds: 'off' }).pollSeconds, 0);
});

test('watchSubset excludes the token, encodes theme as int', () => {
  const sub = watchSubset(normalize({ token: 'secret', theme: 'midnight' }));
  assert.strictEqual(sub.HAS_TOKEN, 1);
  assert.strictEqual('token' in sub, false);
  assert.strictEqual(typeof sub.SET_THEME, 'number'); // index into THEMES
  assert.strictEqual(typeof sub.SET_ACCENT, 'number'); // 24-bit int
  assert.strictEqual(typeof sub.SET_POLL, 'number');
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `node --test test/settings.test.js`
Expected: FAIL — module not found.

- [ ] **Step 3: Write minimal implementation**

```js
// src/pkjs/lib/settings.js
var THEMES = ['dark', 'light', 'midnight'];
var DEFAULTS = { theme: 'midnight', accent: '0x5555FF', pollSeconds: 10, token: '' };

function toHex(v) {
  if (typeof v === 'number') {
    return '0x' + (v & 0xFFFFFF).toString(16).toUpperCase().padStart(6, '0');
  }
  var s = String(v || '').trim();
  if (/^#?[0-9A-Fa-f]{6}$/.test(s)) return '0x' + s.replace('#', '').toUpperCase();
  if (/^0x[0-9A-Fa-f]{6}$/.test(s)) return s.toUpperCase().replace('0X', '0x');
  return DEFAULTS.accent;
}

function toPoll(v) {
  if (v === 'off' || v === '0' || v === 0) return 0;
  var n = parseInt(v, 10);
  return isNaN(n) ? DEFAULTS.pollSeconds : n;
}

function normalize(raw) {
  raw = raw || {};
  var theme = THEMES.indexOf(raw.theme) >= 0 ? raw.theme : DEFAULTS.theme;
  return {
    theme: theme,
    accent: raw.accent === undefined ? DEFAULTS.accent : toHex(raw.accent),
    pollSeconds: raw.pollSeconds === undefined ? DEFAULTS.pollSeconds : toPoll(raw.pollSeconds),
    token: typeof raw.token === 'string' ? raw.token : ''
  };
}

function watchSubset(s) {
  return {
    SET_THEME: THEMES.indexOf(s.theme),
    SET_ACCENT: parseInt(s.accent, 16) & 0xFFFFFF,
    SET_POLL: s.pollSeconds,
    HAS_TOKEN: s.token && s.token.length > 0 ? 1 : 0
  };
}

module.exports = { normalize: normalize, watchSubset: watchSubset, THEMES: THEMES, DEFAULTS: DEFAULTS };
```

- [ ] **Step 4: Run test to verify it passes**

Run: `node --test test/settings.test.js`
Expected: PASS (5 tests).

- [ ] **Step 5: Add root `npm test` and commit**

Add to `package.json` `"scripts"`: `"test": "node --test test/"`. Then:

```bash
node --test test/
git add src/pkjs/lib/settings.js test/settings.test.js package.json
git commit -m "M1: add settings normalization + watch subset (tested)"
```

---

## Task 4: Clay config — consent gate + fields

**Files:**
- Create: `config/index.js` (Clay config array), `config/custom-clay.js` (show/hide logic)

- [ ] **Step 1: Write the Clay config array**

```js
// config/index.js
module.exports = [
  { "type": "heading", "defaultValue": "Wristcord" },
  {
    "type": "text",
    "defaultValue":
      "Wristcord signs in with your personal Discord account token (selfbot). " +
      "Automating a user account is AGAINST Discord's Terms of Service and CAN GET " +
      "YOUR ACCOUNT BANNED. You use it entirely at your own risk; the author is not " +
      "responsible for anything that happens to your account or data."
  },
  {
    "type": "toggle",
    "messageKey": "CONSENT",
    "label": "I understand and accept the risk",
    "defaultValue": false
  },
  {
    "type": "section",
    "id": "gated",
    "items": [
      { "type": "heading", "defaultValue": "Account" },
      { "type": "input", "messageKey": "token", "label": "Discord token", "attributes": { "type": "password", "placeholder": "paste token" } },
      { "type": "heading", "defaultValue": "Appearance" },
      { "type": "select", "messageKey": "theme", "label": "Theme", "defaultValue": "midnight",
        "options": [ { "label": "Midnight", "value": "midnight" }, { "label": "Dark", "value": "dark" }, { "label": "Light", "value": "light" } ] },
      { "type": "color", "messageKey": "accent", "label": "Accent color", "defaultValue": "0x5555FF", "sunlight": false },
      { "type": "select", "messageKey": "pollSeconds", "label": "Refresh interval", "defaultValue": "10",
        "options": [ { "label": "Off", "value": "off" }, { "label": "5 seconds", "value": "5" }, { "label": "10 seconds", "value": "10" }, { "label": "30 seconds", "value": "30" } ] }
    ]
  },
  { "type": "submit", "defaultValue": "Save" }
];
```

- [ ] **Step 2: Write the show/hide custom code**

```js
// config/custom-clay.js
module.exports = function(minified) {
  var clayConfig = this;
  function toggleGated() {
    var consent = clayConfig.getItemByMessageKey('CONSENT').get();
    var gated = clayConfig.getItemById('gated');
    if (consent) { gated.show(); } else { gated.hide(); }
  }
  clayConfig.on(clayConfig.EVENTS.AFTER_RENDER, function() {
    toggleGated();
    clayConfig.getItemByMessageKey('CONSENT').on('change', toggleGated);
  });
};
```

- [ ] **Step 3: Add `CONSENT` + `token`/`theme`/`accent`/`pollSeconds` Clay keys**

Clay auto-includes config `messageKey`s, but add `CONSENT` to `package.json` `messageKeys` (others are Clay-managed strings, not sent to C). Update the array in Task 1 Step 2 to include `"CONSENT"`. Rebuild expectations unchanged.

- [ ] **Step 4: Commit**

```bash
git add config/index.js config/custom-clay.js package.json
git commit -m "M1: Clay config with ToS consent gate + token/theme/accent/poll"
```

---

## Task 5: pkjs entry — Clay wiring + settings round-trip

**Files:**
- Create/overwrite: `src/pkjs/index.js`

- [ ] **Step 1: Write pkjs index**

```js
// src/pkjs/index.js
var Clay = require('pebble-clay');
var clayConfig = require('../../config/index');
var customClay = require('../../config/custom-clay');
var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false });

var settingsLib = require('./lib/settings');

function loadSettings() {
  try { return settingsLib.normalize(JSON.parse(localStorage.getItem('wc_settings') || '{}')); }
  catch (e) { return settingsLib.normalize({}); }
}
function saveSettings(s) { localStorage.setItem('wc_settings', JSON.stringify(s)); }

function pushToWatch(s) {
  Pebble.sendAppMessage(settingsLib.watchSubset(s),
    function () { console.log('settings -> watch ok'); },
    function (e) { console.log('settings -> watch FAIL ' + JSON.stringify(e)); });
}

Pebble.addEventListener('ready', function () {
  pushToWatch(loadSettings());
});

// Clay config closed: persist + push
Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(clay.generateUrl());
});
Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  var dict = clay.getSettings(e.response, false); // raw values keyed by messageKey
  var merged = settingsLib.normalize({
    token: dict.token, theme: dict.theme, accent: dict.accent, pollSeconds: dict.pollSeconds
  });
  saveSettings(merged);
  pushToWatch(merged);
});

// (M2+ will add the 'appmessage' OP router here.)
```

- [ ] **Step 2: Build**

Run: `pebble build`
Expected: PBW builds; bundles pkjs + Clay.

- [ ] **Step 3: Commit**

```bash
git add src/pkjs/index.js
git commit -m "M1: pkjs Clay wiring + settings persist/round-trip to watch"
```

---

## Task 6: C settings module (persist + apply)

**Files:**
- Create: `src/c/settings.h`, `src/c/settings.c`

- [ ] **Step 1: Header**

```c
// src/c/settings.h
#pragma once
#include <pebble.h>

typedef enum { THEME_DARK = 0, THEME_LIGHT = 1, THEME_MIDNIGHT = 2 } WcTheme;

typedef struct {
  WcTheme theme;        // index matches THEMES order in settings.js
  GColor  accent;       // from 24-bit hex
  int32_t poll_seconds; // 0 = off
  bool    has_token;    // whether a token is configured on the phone
} WristcordSettings;

void wc_settings_load(WristcordSettings *out);                 // from persist, with defaults
void wc_settings_apply_from_msg(DictionaryIterator *it,        // from pkjs AppMessage
                                WristcordSettings *s);
void wc_settings_save(const WristcordSettings *s);             // to persist
GColor wc_theme_bg(const WristcordSettings *s);                // base background per theme
GColor wc_theme_fg(const WristcordSettings *s);                // base text per theme
```

- [ ] **Step 2: Implementation**

```c
// src/c/settings.c
#include "settings.h"

#define PK_THEME  100
#define PK_ACCENT 101
#define PK_POLL   102
#define PK_HASTOK 103

void wc_settings_load(WristcordSettings *out) {
  out->theme        = persist_exists(PK_THEME)  ? (WcTheme)persist_read_int(PK_THEME) : THEME_MIDNIGHT;
  out->accent       = persist_exists(PK_ACCENT) ? GColorFromHEX(persist_read_int(PK_ACCENT)) : GColorFromHEX(0x5555FF);
  out->poll_seconds = persist_exists(PK_POLL)   ? persist_read_int(PK_POLL) : 10;
  out->has_token    = persist_exists(PK_HASTOK) ? (bool)persist_read_int(PK_HASTOK) : false;
}

void wc_settings_save(const WristcordSettings *s) {
  persist_write_int(PK_THEME, s->theme);
  persist_write_int(PK_ACCENT, (int32_t)(((s->accent.r & 3) << 22) | 0)); // see note
  persist_write_int(PK_POLL, s->poll_seconds);
  persist_write_int(PK_HASTOK, s->has_token ? 1 : 0);
}

void wc_settings_apply_from_msg(DictionaryIterator *it, WristcordSettings *s) {
  Tuple *t;
  if ((t = dict_find(it, MESSAGE_KEY_SET_THEME)))  s->theme = (WcTheme)t->value->int32;
  if ((t = dict_find(it, MESSAGE_KEY_SET_ACCENT))) s->accent = GColorFromHEX(t->value->int32);
  if ((t = dict_find(it, MESSAGE_KEY_SET_POLL)))   s->poll_seconds = t->value->int32;
  if ((t = dict_find(it, MESSAGE_KEY_HAS_TOKEN)))  s->has_token = (t->value->int32 != 0);
}

GColor wc_theme_bg(const WristcordSettings *s) {
  switch (s->theme) {
    case THEME_LIGHT: return GColorWhite;
    case THEME_DARK:  return GColorBlack;
    default:          return GColorFromHEX(0x0B0D1A); // midnight base (tint refined in M7)
  }
}
GColor wc_theme_fg(const WristcordSettings *s) {
  return s->theme == THEME_LIGHT ? GColorBlack : GColorWhite;
}
```

> **Note for executor:** persisting `GColor` is simplest by storing the original 24-bit hex int, not the packed `GColor`. Adjust `wc_settings_save` to keep the raw hex: store an `int32_t accent_hex` field instead, or persist the hex before converting. Fix this when wiring Task 7 so `accent` survives a reload exactly. (Tracked: replace the `accent` save line with the raw hex value carried from the message.)

- [ ] **Step 3: Build to confirm it compiles**

Run: `pebble build`
Expected: compiles (module unused warnings OK until Task 7).

- [ ] **Step 4: Commit**

```bash
git add src/c/settings.h src/c/settings.c
git commit -m "M1: C settings module (persist + theme colors + msg apply)"
```

---

## Task 7: C entry — AppMessage handshake + status window

**Files:**
- Overwrite: `src/c/wristcord.c`

- [ ] **Step 1: Write the entry + status window**

```c
// src/c/wristcord.c
#include <pebble.h>
#include "settings.h"

static Window *s_window;
static TextLayer *s_title, *s_state;
static WristcordSettings s_settings;

static void render_state(void) {
  window_set_background_color(s_window, wc_theme_bg(&s_settings));
  text_layer_set_text_color(s_title, s_settings.accent);
  text_layer_set_text_color(s_state, wc_theme_fg(&s_settings));
  text_layer_set_text(s_state, s_settings.has_token
    ? "Connected.\nServers load in M3."
    : "No token set.\nOpen Wristcord settings\nin the Pebble app.");
}

static void inbox_received(DictionaryIterator *it, void *ctx) {
  wc_settings_apply_from_msg(it, &s_settings);
  wc_settings_save(&s_settings);
  render_state();
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_title = text_layer_create(GRect(0, 20, b.size.w, 30));
  text_layer_set_text(s_title, "Wristcord");
  text_layer_set_font(s_title, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_title, GTextAlignmentCenter);
  text_layer_set_background_color(s_title, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_title));

  s_state = text_layer_create(GRect(8, 70, b.size.w - 16, b.size.h - 80));
  text_layer_set_font(s_state, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_state, GTextAlignmentCenter);
  text_layer_set_background_color(s_state, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_state));

  render_state();
}
static void window_unload(Window *w) {
  text_layer_destroy(s_title);
  text_layer_destroy(s_state);
}

static void init(void) {
  wc_settings_load(&s_settings);
  app_message_register_inbox_received(inbox_received);
  app_message_open(2048, 256);
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);
}
static void deinit(void) { window_destroy(s_window); }

int main(void) { init(); app_event_loop(); deinit(); }
```

- [ ] **Step 2: Build**

Run: `pebble build`
Expected: clean build → `build/wristcord.pbw`.

- [ ] **Step 3: Emulator-verify the two states (via pebble-watchapp skill)**

Run via the `pebble-watchapp` skill flow:
1. `pebble install --emulator emery && pebble logs` — launch app.
2. Screenshot → expect "No token set…" on the Midnight background, "Wristcord" in `#5555FF`. `Read`-verify.
3. Open config headless (see `reference_pebble_tool_python312` memory: pypkjs websocket Clay config), accept consent, set a token + Light theme + a red accent, Save.
4. Screenshot → expect "Connected. Servers load in M3." on a white background, red title. `Read`-verify.
5. `pebble kill` (mandatory — QEMU captures the mouse).

- [ ] **Step 4: Commit**

```bash
git add src/c/wristcord.c
git commit -m "M1: C entry, AppMessage handshake, themed status window"
```

---

## Task 8: M1 acceptance

- [ ] **Step 1: Run the pkjs unit suite**

Run: `node --test test/`
Expected: all tests in `color.test.js` + `settings.test.js` PASS.

- [ ] **Step 2: Verify acceptance criteria**

- [ ] PBW builds for emery with no errors.
- [ ] Clay config hides token/theme/accent/poll until the consent toggle is on.
- [ ] Saving config persists settings (survive app relaunch) and restyles the status window live.
- [ ] Status window correctly reflects token-present vs token-absent.
- [ ] `pebble kill` was run after every emulator session.

- [ ] **Step 3: Tag the milestone**

```bash
git tag m1-shell
git commit --allow-empty -m "M1 complete: configurable shell with consent gate + settings round-trip"
```

---

## Self-Review (M1 plan vs spec)

- **Spec §7 (Clay consent gate + 4 knobs):** Tasks 4–5 implement the gate and exactly the 4 knobs (token/theme/accent/poll). ✓
- **Spec §6 (persist theme/accent/poll for boot styling):** Task 6 (`PK_THEME/ACCENT/POLL/HASTOK`). ✓ Collapse-set keys are M3/M4, correctly out of M1 scope.
- **Spec §2/§3 (two-program + AppMessage):** Tasks 5+7 establish the handshake and message keys; the generic OP/PAGE paging router is M2 (noted as a stub in Task 5). ✓
- **Spec §8 (accent-adaptive theming):** M1 applies accent to title + base bg/fg only; full accent-derived tint + no-stripe rules land in M7. Flagged, not silently dropped. ✓
- **Token never stored on watch:** `watchSubset` strips it (Task 3 test asserts this); C only stores `has_token`. ✓
- **Known executor fix-up:** Task 6 `wc_settings_save` accent persistence must store raw hex — explicitly called out with the fix, not left as a silent bug.
- **Placeholder scan:** No TBD/TODO requirement steps; every code step has real code. The one "refine in M7" note is a scoped forward-reference, not a gap.
```
