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
