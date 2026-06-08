#!/usr/bin/env python3
# Dev helper: seed the Pebble *emulator's* pkjs localStorage with the Discord token
# (+ default settings) so you don't have to drive the interactive Clay config to test.
# Reads the token from .secrets/discord_token (gitignored). Writes the same `wc_settings`
# key/shape that src/pkjs/index.js persists. EMULATOR ONLY — never touches real hardware.
#
# Usage: pebble kill ; python3 scripts/seed-emulator-token.py [theme] [accent] ; pebble install --emulator emery
import dbm.dumb, glob, json, os, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def main():
    pkg = json.load(open(os.path.join(REPO, 'package.json')))
    uuid = pkg['pebble']['uuid']
    tok_path = os.path.join(REPO, '.secrets', 'discord_token')
    if not os.path.exists(tok_path):
        sys.exit('No token at .secrets/discord_token')
    token = open(tok_path).read().strip()
    if not token:
        sys.exit('Empty token file')

    ls_dirs = sorted(glob.glob(os.path.expanduser('~/.local/share/pebble-sdk/*/emery/localstorage')))
    if not ls_dirs:
        sys.exit('No emery localstorage dir found — install the app on the emulator once first.')
    db_path = os.path.join(ls_dirs[-1], uuid)  # dbm.dumb manages .dat/.dir/.bak

    theme = sys.argv[1] if len(sys.argv) > 1 else 'midnight'
    accent = sys.argv[2] if len(sys.argv) > 2 else '0x5555FF'
    settings = {'theme': theme, 'accent': accent, 'pollSeconds': 10, 'token': token}

    db = dbm.dumb.open(db_path, 'c')
    db['wc_settings'] = json.dumps(settings)
    db.close()
    print('Seeded wc_settings (theme=%s accent=%s, token present) into %s' % (theme, accent, db_path))
    print('Token NOT printed. Now: pebble install --emulator emery')

if __name__ == '__main__':
    main()
