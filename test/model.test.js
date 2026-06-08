// test/model.test.js  — TDD for src/pkjs/lib/model.js
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { buildServerList, buildChannelTree, packMessages, cleanText } = require('../src/pkjs/lib/model');
const { nameToAccentHex } = require('../src/pkjs/lib/color');

// ---------------------------------------------------------------------------
// buildServerList
// ---------------------------------------------------------------------------

test('buildServerList: named folder + unnamed folder + standalone + orphan guild', () => {
  const guilds = [
    { id: '111', name: 'Alpha' },
    { id: '222', name: 'Beta' },
    { id: '333', name: 'Gamma' },
    { id: '444', name: 'Delta' },
    { id: '555', name: 'Orphan' },   // not in any folder
  ];
  const userSettings = {
    guild_folders: [
      // Named folder with integer color
      { id: 9000, name: 'Gaming', color: 5793266, guild_ids: ['111', '222'] },
      // Unnamed folder (name: null) — must produce name 'Folder'
      { id: 9001, name: null, color: null, guild_ids: ['333'] },
      // Standalone (id null) — top-level guild
      { id: null, name: null, color: null, guild_ids: ['444'] },
    ],
  };

  const rows = buildServerList(guilds, userSettings);

  // Expected order: folder 'Gaming', guild Alpha, guild Beta,
  //                 folder 'Folder', guild Gamma,
  //                 guild Delta (standalone),
  //                 guild Orphan (appended)

  // row 0 — folder 'Gaming'
  assert.equal(rows[0].kind, 'f');
  assert.equal(rows[0].id, '9000');
  assert.equal(rows[0].name, 'Gaming');
  // color: 5793266 = 0x588272 -> '0x588272'
  assert.equal(rows[0].color, '0x' + (5793266 & 0xFFFFFF).toString(16).toUpperCase().padStart(6, '0'));
  // parentIndex is '' for folder rows
  assert.equal(rows[0].parentIndex, '');
  // memberColors: up to 4 for guild_ids ['111','222'] => 2 colors
  assert.equal(rows[0].memberColors.length, 2);
  assert.equal(rows[0].memberColors[0], nameToAccentHex('Alpha'));
  assert.equal(rows[0].memberColors[1], nameToAccentHex('Beta'));

  // row 1 — guild Alpha (child of folder at index 0)
  assert.equal(rows[1].kind, 'g');
  assert.equal(rows[1].id, '111');
  assert.equal(rows[1].name, 'Alpha');
  assert.equal(rows[1].color, nameToAccentHex('Alpha'));
  assert.equal(rows[1].parentIndex, 0);

  // row 2 — guild Beta (child of folder at index 0)
  assert.equal(rows[2].kind, 'g');
  assert.equal(rows[2].id, '222');
  assert.equal(rows[2].parentIndex, 0);

  // row 3 — unnamed folder (id set, name null -> 'Folder')
  assert.equal(rows[3].kind, 'f');
  assert.equal(rows[3].id, '9001');
  assert.equal(rows[3].name, 'Folder');
  // color: entry.color null -> fallback to first member nameToAccentHex
  assert.equal(rows[3].color, nameToAccentHex('Gamma'));
  assert.equal(rows[3].memberColors.length, 1);

  // row 4 — guild Gamma (child of folder at index 3)
  assert.equal(rows[4].kind, 'g');
  assert.equal(rows[4].id, '333');
  assert.equal(rows[4].parentIndex, 3);

  // row 5 — standalone guild Delta (id null -> top-level)
  assert.equal(rows[5].kind, 'g');
  assert.equal(rows[5].id, '444');
  assert.equal(rows[5].parentIndex, '');

  // row 6 — orphan guild Orphan (appended)
  assert.equal(rows[6].kind, 'g');
  assert.equal(rows[6].id, '555');
  assert.equal(rows[6].name, 'Orphan');
  assert.equal(rows[6].parentIndex, '');

  assert.equal(rows.length, 7);
});

test('buildServerList: memberColors capped at 4', () => {
  const guilds = [
    { id: '1', name: 'A' },
    { id: '2', name: 'B' },
    { id: '3', name: 'C' },
    { id: '4', name: 'D' },
    { id: '5', name: 'E' },
  ];
  const userSettings = {
    guild_folders: [
      { id: 1, name: 'Big', color: 0, guild_ids: ['1', '2', '3', '4', '5'] },
    ],
  };
  const rows = buildServerList(guilds, userSettings);
  assert.equal(rows[0].kind, 'f');
  assert.equal(rows[0].memberColors.length, 4);
});

test('buildServerList: empty guild_folders falls back to flat guild list', () => {
  const guilds = [
    { id: '10', name: 'Solo' },
    { id: '20', name: 'Duo' },
  ];
  // No userSettings at all
  const rows = buildServerList(guilds, null);
  assert.equal(rows.length, 2);
  assert.equal(rows[0].kind, 'g');
  assert.equal(rows[0].id, '10');
  assert.equal(rows[0].parentIndex, '');
  assert.equal(rows[1].id, '20');
});

test('buildServerList: folder color 0 is still a valid number (black -> 0x000000)', () => {
  const guilds = [{ id: '1', name: 'A' }];
  const userSettings = {
    guild_folders: [
      { id: 42, name: 'Dark', color: 0, guild_ids: ['1'] },
    ],
  };
  const rows = buildServerList(guilds, userSettings);
  assert.equal(rows[0].color, '0x000000');
});

// ---------------------------------------------------------------------------
// buildChannelTree
// ---------------------------------------------------------------------------

test('buildChannelTree: uncategorized first, categories in position order, voice ignored', () => {
  const channels = [
    // voice channel — type 2, must be ignored
    { id: 'vc1', name: 'voice-chat', type: 2, position: 0, parent_id: null },
    // uncategorized text channels (parent_id null)
    { id: 't2', name: 'general', type: 0, position: 2, parent_id: null },
    { id: 't1', name: 'welcome', type: 0, position: 1, parent_id: null },
    // categories
    { id: 'cat2', name: 'Media', type: 4, position: 20, parent_id: null },
    { id: 'cat1', name: 'Info', type: 4, position: 10, parent_id: null },
    // children of cat1
    { id: 'c1b', name: 'rules', type: 0, position: 5, parent_id: 'cat1' },
    { id: 'c1a', name: 'announcements', type: 5, position: 1, parent_id: 'cat1' }, // type 5 = announcement
    // children of cat2
    { id: 'c2a', name: 'memes', type: 0, position: 3, parent_id: 'cat2' },
  ];

  const rows = buildChannelTree(channels);

  // Expected: t1 (pos1), t2 (pos2), cat1, c1a (pos1), c1b (pos5), cat2, c2a (pos3)
  assert.equal(rows.length, 7);

  assert.equal(rows[0].kind, 't');
  assert.equal(rows[0].id, 't1');
  assert.equal(rows[0].parentIndex, '');

  assert.equal(rows[1].kind, 't');
  assert.equal(rows[1].id, 't2');
  assert.equal(rows[1].parentIndex, '');

  // cat1 is at index 2
  assert.equal(rows[2].kind, 'c');
  assert.equal(rows[2].id, 'cat1');
  assert.equal(rows[2].name, 'Info');
  assert.equal(rows[2].parentIndex, '');

  // cat1 children sorted by position
  assert.equal(rows[3].kind, 't');
  assert.equal(rows[3].id, 'c1a');
  assert.equal(rows[3].parentIndex, 2);

  assert.equal(rows[4].kind, 't');
  assert.equal(rows[4].id, 'c1b');
  assert.equal(rows[4].parentIndex, 2);

  // cat2 is at index 5
  assert.equal(rows[5].kind, 'c');
  assert.equal(rows[5].id, 'cat2');
  assert.equal(rows[5].parentIndex, '');

  assert.equal(rows[6].kind, 't');
  assert.equal(rows[6].id, 'c2a');
  assert.equal(rows[6].parentIndex, 5);
});

test('buildChannelTree: empty channels array', () => {
  assert.deepEqual(buildChannelTree([]), []);
});

// ---------------------------------------------------------------------------
// packMessages
// ---------------------------------------------------------------------------

test('packMessages: newest-first input becomes oldest-first output', () => {
  const messages = [
    { author: { global_name: 'Carol', username: 'carol#0001' }, content: 'Third', timestamp: '2024-01-01T09:30:00.000Z' },
    { author: { global_name: null, username: 'bob#0002' }, content: 'Second', timestamp: '2024-01-01T09:15:00.000Z' },
    { author: { global_name: 'Alice', username: 'alice#0003' }, content: 'First', timestamp: '2024-01-01T09:04:00.000Z' },
  ];
  const result = packMessages(messages);
  // Input newest-first: Carol, bob, Alice -> reversed: Alice, bob, Carol
  assert.equal(result.length, 3);
  assert.equal(result[0].author, 'Alice');
  assert.equal(result[1].author, 'bob#0002');
  assert.equal(result[2].author, 'Carol');
});

test('packMessages: author prefers global_name over username', () => {
  const msg = [
    { author: { global_name: 'GlobalAlice', username: 'alice#0001' }, content: 'hi', timestamp: '2024-01-01T00:00:00.000Z' },
  ];
  assert.equal(packMessages(msg)[0].author, 'GlobalAlice');
});

test('packMessages: time format "h:mm" with zero-padded minutes, no leading zero on hour', () => {
  const msg = [
    { author: { global_name: 'X', username: 'x' }, content: 'hi', timestamp: '2024-06-15T09:04:00.000Z' },
  ];
  assert.equal(packMessages(msg)[0].time, '9:04');
});

test('packMessages: time format hour 13 (1pm UTC)', () => {
  const msg = [
    { author: { global_name: 'X', username: 'x' }, content: 'hi', timestamp: '2024-06-15T13:05:00.000Z' },
  ];
  assert.equal(packMessages(msg)[0].time, '13:05');
});

test('packMessages: 130-char content truncates to 120 chars + ellipsis', () => {
  const longText = 'A'.repeat(130);
  const msg = [
    { author: { global_name: 'X', username: 'x' }, content: longText, timestamp: '2024-01-01T00:00:00.000Z' },
  ];
  const result = packMessages(msg)[0];
  assert.equal(result.text.length, 121); // 120 + '…' (1 char)
  assert.equal(result.text, 'A'.repeat(120) + '…');
});

test('packMessages: 120-char content is NOT truncated', () => {
  const text = 'B'.repeat(120);
  const msg = [
    { author: { global_name: 'X', username: 'x' }, content: text, timestamp: '2024-01-01T00:00:00.000Z' },
  ];
  assert.equal(packMessages(msg)[0].text, text);
});

test('packMessages: empty content yields "[no text]"', () => {
  const cases = [
    { author: { global_name: 'X', username: 'x' }, content: '', timestamp: '2024-01-01T00:00:00.000Z' },
    { author: { global_name: 'Y', username: 'y' }, content: null, timestamp: '2024-01-01T00:00:00.000Z' },
  ];
  const result = packMessages(cases);
  assert.equal(result[0].text, '[no text]');
  assert.equal(result[1].text, '[no text]');
});

test('packMessages: color is nameToAccentHex of author', () => {
  const msg = [
    { author: { global_name: 'Zara', username: 'z' }, content: 'hi', timestamp: '2024-01-01T00:00:00.000Z' },
  ];
  assert.equal(packMessages(msg)[0].color, nameToAccentHex('Zara'));
});

// ---------------------------------------------------------------------------
// cleanText
// ---------------------------------------------------------------------------

test('cleanText: resolves user mention via msg.mentions (global_name preferred)', () => {
  assert.equal(
    cleanText('hi <@42>', { mentions: [{ id: '42', username: 'bob', global_name: 'Bob' }] }),
    'hi @Bob'
  );
});

test('cleanText: unknown user mention falls back to @user', () => {
  assert.equal(cleanText('hello <@99>', { mentions: [] }), 'hello @user');
});

test('cleanText: role mention -> @role', () => {
  assert.equal(cleanText('ping <@&5>', null), 'ping @role');
});

test('cleanText: channel mention -> #channel', () => {
  assert.equal(cleanText('see <#7>', null), 'see #channel');
});

test('cleanText: custom emoji -> :name:', () => {
  assert.equal(cleanText('wow <:rocket:811>', null), 'wow :rocket:');
});

test('cleanText: animated emoji -> :name:', () => {
  assert.equal(cleanText('wee <a:spin:1>', null), 'wee :spin:');
});

test('cleanText: strips **bold**', () => {
  assert.equal(cleanText('**bold** text', null), 'bold text');
});

test('cleanText: strips *italic*', () => {
  assert.equal(cleanText('*italic* text', null), 'italic text');
});

test('cleanText: strips __underline__', () => {
  assert.equal(cleanText('__u__ text', null), 'u text');
});

test('cleanText: strips ~~strike~~', () => {
  assert.equal(cleanText('~~s~~ text', null), 's text');
});

test('cleanText: strips inline code backticks -> inner text', () => {
  assert.equal(cleanText('run `ls -la` now', null), 'run ls -la now');
});

test('cleanText: collapses newlines to single space', () => {
  assert.equal(cleanText('a\n\nb', null), 'a b');
});

test('cleanText: empty string -> empty string', () => {
  assert.equal(cleanText('', null), '');
});

test('cleanText: undefined/null content -> empty string', () => {
  assert.equal(cleanText(null, null), '');
  assert.equal(cleanText(undefined, null), '');
});

test('packMessages: cleans Discord markup in text field', () => {
  const msg = [
    {
      author: { global_name: 'Alice', username: 'alice' },
      content: '**hey** <@9>',
      mentions: [{ id: '9', username: 'al', global_name: null }],
      timestamp: '2024-01-01T00:00:00.000Z',
    },
  ];
  assert.equal(packMessages(msg)[0].text, 'hey @al');
});

// ---------------------------------------------------------------------------
// packMessages: M6 additions — id, full, truncated
// ---------------------------------------------------------------------------

test('packMessages: 200-char content yields truncated:true, full untruncated, text ends with ellipsis', () => {
  const longContent = 'X'.repeat(200);
  const msg = [
    { id: 'msg-abc', author: { global_name: 'A', username: 'a' }, content: longContent, timestamp: '2024-01-01T00:00:00.000Z' },
  ];
  const result = packMessages(msg)[0];
  assert.equal(result.id, 'msg-abc');
  assert.equal(result.truncated, true);
  assert.ok(result.full.length >= 200, 'full should be untruncated (~200 chars)');
  assert.ok(result.text.endsWith('…'), 'text should end with ellipsis');
  assert.ok(result.text.length <= 121, 'text should be at most 121 chars (120 + ellipsis)');
});

test('packMessages: short message yields truncated:false and full===text', () => {
  const msg = [
    { id: 'msg-xyz', author: { global_name: 'B', username: 'b' }, content: 'Hello world', timestamp: '2024-01-01T00:00:00.000Z' },
  ];
  const result = packMessages(msg)[0];
  assert.equal(result.id, 'msg-xyz');
  assert.equal(result.truncated, false);
  assert.equal(result.full, result.text);
  assert.equal(result.full, 'Hello world');
});

test('packMessages: id is returned as string', () => {
  const msg = [
    { id: 123456, author: { global_name: 'C', username: 'c' }, content: 'hi', timestamp: '2024-01-01T00:00:00.000Z' },
  ];
  assert.equal(typeof packMessages(msg)[0].id, 'string');
  assert.equal(packMessages(msg)[0].id, '123456');
});

// ---------------------------------------------------------------------------
// buildChannelTree: M6 additions — lastMessageId
// ---------------------------------------------------------------------------

test('buildChannelTree: text channel with last_message_id yields lastMessageId as string', () => {
  const channels = [
    { id: 'ch1', name: 'general', type: 0, position: 1, parent_id: null, last_message_id: '999' },
  ];
  const rows = buildChannelTree(channels);
  assert.equal(rows[0].lastMessageId, '999');
});

test('buildChannelTree: text channel with null last_message_id yields empty string', () => {
  const channels = [
    { id: 'ch2', name: 'announcements', type: 5, position: 1, parent_id: null, last_message_id: null },
  ];
  const rows = buildChannelTree(channels);
  assert.equal(rows[0].lastMessageId, '');
});

test('buildChannelTree: category row has empty lastMessageId', () => {
  const channels = [
    { id: 'cat1', name: 'General', type: 4, position: 0, parent_id: null },
    { id: 'ch1', name: 'chat', type: 0, position: 1, parent_id: 'cat1', last_message_id: '777' },
  ];
  const rows = buildChannelTree(channels);
  const catRow = rows.find(r => r.kind === 'c');
  assert.equal(catRow.lastMessageId, '');
  const textRow = rows.find(r => r.kind === 't');
  assert.equal(textRow.lastMessageId, '777');
});
