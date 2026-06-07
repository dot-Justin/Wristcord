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
