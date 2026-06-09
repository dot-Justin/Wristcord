// src/pkjs/lib/settings.js
var THEMES = ['dark', 'light', 'midnight'];
// Server sort modes (DMs are always newest-first). Indices match the C side.
var SORT_MODES = ['mostUsed', 'discordOrder', 'alphabetical', 'recentActivity'];
var DEFAULTS = { theme: 'midnight', accent: '0x5555FF', pollSeconds: 10, token: '',
                 dmCount: 3, serverCount: 3, sortMode: 'mostUsed' };
var COUNT_MIN = 3, COUNT_MAX = 20;

function clampCount(v) {
  var n = parseInt(v, 10);
  if (isNaN(n)) return DEFAULTS.dmCount;
  if (n < COUNT_MIN) return COUNT_MIN;
  if (n > COUNT_MAX) return COUNT_MAX;
  return n;
}

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
  var sortMode = SORT_MODES.indexOf(raw.sortMode) >= 0 ? raw.sortMode : DEFAULTS.sortMode;
  return {
    theme: theme,
    accent: raw.accent === undefined ? DEFAULTS.accent : toHex(raw.accent),
    pollSeconds: raw.pollSeconds === undefined ? DEFAULTS.pollSeconds : toPoll(raw.pollSeconds),
    token: typeof raw.token === 'string' ? raw.token : '',
    dmCount: raw.dmCount === undefined ? DEFAULTS.dmCount : clampCount(raw.dmCount),
    serverCount: raw.serverCount === undefined ? DEFAULTS.serverCount : clampCount(raw.serverCount),
    sortMode: sortMode
  };
}

function watchSubset(s) {
  return {
    SET_THEME: THEMES.indexOf(s.theme),
    SET_ACCENT: parseInt(s.accent, 16) & 0xFFFFFF,
    SET_POLL: s.pollSeconds,
    HAS_TOKEN: s.token && s.token.length > 0 ? 1 : 0,
    SET_DM_COUNT: s.dmCount,
    SET_SERVER_COUNT: s.serverCount,
    SET_SORT_MODE: SORT_MODES.indexOf(s.sortMode)
  };
}

module.exports = { normalize: normalize, watchSubset: watchSubset, THEMES: THEMES,
                   SORT_MODES: SORT_MODES, DEFAULTS: DEFAULTS,
                   COUNT_MIN: COUNT_MIN, COUNT_MAX: COUNT_MAX };
