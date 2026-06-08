#!/usr/bin/env bash
# capture-store-shots.sh — generate app-store marketing screenshots from the
# emulator using fake "DEMO" data so no real Discord content leaks out.
#
# 1. Flips DEMO_MODE in pkjs and WC_DEMO in compose.c -> on.
# 2. Cleans (pkjs is cached) + builds + installs in the Emery emulator.
# 3. Walks the app through every marketing-relevant screen and screenshots each.
# 4. Always reverts the flags on exit, even on error.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:-$REPO_ROOT/screenshots/store}"
mkdir -p "$OUT_DIR"
cd "$REPO_ROOT"
export PATH="$HOME/.local/bin:$PATH"

# ── revert flags on exit, no matter what ──────────────────────────────────────
revert() {
  echo "→ reverting demo flags…"
  sed -i 's/^var DEMO_MODE = true;/var DEMO_MODE = false;/'   src/pkjs/index.js || true
  sed -i 's|^#define WC_DEMO 1$|#define WC_DEMO 0|'           src/c/compose.c   || true
  pebble kill >/dev/null 2>&1 || true
}
trap revert EXIT

# ── flip flags on ─────────────────────────────────────────────────────────────
echo "→ enabling DEMO_MODE + WC_DEMO"
sed -i 's/^var DEMO_MODE = false;/var DEMO_MODE = true;/'   src/pkjs/index.js
sed -i 's|^#define WC_DEMO 0$|#define WC_DEMO 1|'           src/c/compose.c
grep -E 'DEMO_MODE = (true|false)' src/pkjs/index.js | head -1
grep -E '#define WC_DEMO' src/c/compose.c | head -1

# ── clean build (pkjs is cached, so a stale demo flag would ship otherwise) ───
echo "→ pebble clean && build"
pebble clean >/dev/null
pebble build 2>&1 | tail -3

# ── helpers ───────────────────────────────────────────────────────────────────
shot() { pebble screenshot --emulator emery "$OUT_DIR/$1" >/dev/null 2>&1 && echo "   shot $1"; }
click() { pebble emu-button click "$1" >/dev/null 2>&1; }
hold()  { pebble emu-button push  "$1" >/dev/null 2>&1; sleep "${2:-0.7}"; pebble emu-button release "$1" >/dev/null 2>&1; }

# ── 0) wipe the emulator's SPI flash so onboarding shows on first launch ──────
echo "→ resetting emulator SPI"
pebble kill >/dev/null 2>&1 || true
rm -f ~/.local/share/pebble-sdk/4.9.169/emery/qemu_spi_flash.bin || true

# ── 1) install + grab onboarding welcome ──────────────────────────────────────
echo "→ install + capture (will take a few seconds)"
pebble install --emulator emery >/dev/null 2>&1 &
INSTALL_PID=$!
# the emulator launches as part of install; screenshot once the welcome card paints
sleep 6
shot 01-welcome.png

# ── 2) finish onboarding (4 cards) — loading screen is captured on a relaunch below
click select; sleep 0.6
click select; sleep 0.6
click select; sleep 0.6
click select; sleep 4              # let demo backend resolve (1.5s delay + render)
shot 02-server-list.png

# ── 4) channel list — select the first server (row 1; row 0 is Settings) ──────
click select; sleep 3
shot 03-channel-list.png

# ── 5) chat — select the first text channel (#welcome at row 0) ──────────────
click select; sleep 3
shot 04-chat.png

# ── 6) compose confirm — SELECT triggers WC_DEMO short-circuit ────────────────
click select; sleep 2
shot 05-compose.png

# back to chat
click back; sleep 1

# ── 7) read-full message — scroll up to carol's long message, long-press ──────
# chat selects newest (last) row by default; the long message is 3 above newest
click up; sleep 0.3
click up; sleep 0.3
click up; sleep 0.3
hold select 0.9
sleep 1
shot 09-message-actions.png      # bonus shot of the action menu
click select; sleep 3            # "Read full message" is the first action
shot 06-read-full.png

# back -> chat
click back; sleep 0.6
# back -> channel list
click back; sleep 0.6
# back -> server list
click back; sleep 0.6

# ── 8) settings menu — scroll up to row 0 and click ───────────────────────────
click up; sleep 0.4
click select; sleep 1
shot 08-settings-menu.png
click back; sleep 0.4    # close the action menu

# ── 9) loading screen — relaunch (persist keeps PK_TUTORIAL_DONE) so onboarding
#       is skipped and the server list sits in ST_LOADING long enough to capture.
echo "→ relaunch for a clean loading-screen capture"
pebble install --emulator emery >/dev/null 2>&1 &
# the 1500ms demo delay on /users/@me/guilds gives us a window to shoot the loader
sleep 4
for i in 1 2 3; do shot "07-loading-$i.png"; sleep 0.35; done
# also keep one canonical 07-loading.png — pick the middle frame
cp "$OUT_DIR/07-loading-2.png" "$OUT_DIR/07-loading.png" 2>/dev/null || true

echo
echo "✓ captured to $OUT_DIR"
ls -la "$OUT_DIR"
