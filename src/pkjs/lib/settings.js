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
