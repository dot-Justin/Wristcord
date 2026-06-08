// test/paging.test.js  — TDD for src/pkjs/lib/paging.js
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { encodeRows, batches, RS, US } = require('../src/pkjs/lib/paging');

// ---------------------------------------------------------------------------
// RS / US constants
// ---------------------------------------------------------------------------

test('RS and US are single characters (ASCII 30 and 31)', () => {
  assert.equal(RS.length, 1);
  assert.equal(US.length, 1);
  assert.equal(RS.charCodeAt(0), 30);
  assert.equal(US.charCodeAt(0), 31);
});

// ---------------------------------------------------------------------------
// encodeRows
// ---------------------------------------------------------------------------

test('encodeRows: basic two-field record', () => {
  const result = encodeRows([['hello', 'world']]);
  assert.equal(result, 'hello' + US + 'world');
});

test('encodeRows: multiple records joined by RS', () => {
  const result = encodeRows([['a', 'b'], ['c', 'd']]);
  assert.equal(result, 'a' + US + 'b' + RS + 'c' + US + 'd');
});

test('encodeRows: numbers are stringified', () => {
  const result = encodeRows([[42, 3.14]]);
  assert.equal(result, '42' + US + '3.14');
});

test('encodeRows: RS characters in field values are stripped', () => {
  const dirty = 'foo' + RS + 'bar';
  const result = encodeRows([[dirty, 'ok']]);
  // RS removed from field value -> 'foobar' + US + 'ok'
  assert.equal(result, 'foobar' + US + 'ok');
});

test('encodeRows: US characters in field values are stripped', () => {
  const dirty = 'a' + US + 'b';
  const result = encodeRows([[dirty]]);
  assert.equal(result, 'ab');
});

test('encodeRows: empty array returns empty string', () => {
  assert.equal(encodeRows([]), '');
});

test('encodeRows: single empty record produces single record with just separator between fields', () => {
  // A record with two empty-string fields
  const result = encodeRows([['', '']]);
  assert.equal(result, US);
});

// ---------------------------------------------------------------------------
// batches
// ---------------------------------------------------------------------------

test('batches: empty string returns []', () => {
  assert.deepEqual(batches('', 100), []);
});

test('batches: single record that fits', () => {
  const row = 'abc';
  assert.deepEqual(batches(row, 10), ['abc']);
});

test('batches: two records that both fit in one chunk', () => {
  const row = 'abc' + RS + 'def';
  // total length = 7 (3 + 1 + 3), maxLen >= 7 -> one chunk
  assert.deepEqual(batches(row, 10), ['abc' + RS + 'def']);
});

test('batches: two records that must be split into separate chunks', () => {
  const row = 'abc' + RS + 'def';
  // maxLen = 5 -> 'abc' fits (3), adding RS+'def' = 3+1+3=7 > 5, so split
  const result = batches(row, 5);
  assert.equal(result.length, 2);
  assert.equal(result[0], 'abc');
  assert.equal(result[1], 'def');
});

test('batches: single record longer than maxLen becomes its own chunk', () => {
  const longRecord = 'A'.repeat(200);
  const result = batches(longRecord, 10);
  assert.equal(result.length, 1);
  assert.equal(result[0], longRecord);
});

test('batches: greedy grouping — fits as many records as possible per chunk', () => {
  // records: 'aa', 'bb', 'cc', 'dd' — each 2 chars; RS = 1 char
  // maxLen = 5 -> 'aa'+RS+'bb' = 5, fits; adding RS+'cc' = 8 > 5 -> new chunk
  //              'cc'+RS+'dd' = 5, fits
  const row = 'aa' + RS + 'bb' + RS + 'cc' + RS + 'dd';
  const result = batches(row, 5);
  assert.equal(result.length, 2);
  assert.equal(result[0], 'aa' + RS + 'bb');
  assert.equal(result[1], 'cc' + RS + 'dd');
});

test('batches: mix of normal and overlong records', () => {
  const normal = 'hi';
  const big = 'X'.repeat(50);
  const row = normal + RS + big + RS + normal;
  const result = batches(row, 10);
  // 'hi' fits (2), adding RS+big -> 2+1+50=53 > 10 -> new chunk
  // big alone: 50 > 10 but must not be split
  // 'hi': 2 <= 10, fits as new chunk
  assert.equal(result.length, 3);
  assert.equal(result[0], normal);
  assert.equal(result[1], big);
  assert.equal(result[2], normal);
});
