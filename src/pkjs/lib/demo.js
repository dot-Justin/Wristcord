// src/pkjs/lib/demo.js
// Fake Discord backend used for app-store screenshots. When DEMO_MODE is on in
// index.js, makeXhrRequest() routes here instead of hitting discord.com.
// All ids are obviously synthetic (1000…/2000…) and the names/messages are made
// up — no real user data leaks into the marketing screenshots.
'use strict';

var GUILDS = [
  { id: '1000000000000000001', name: 'DevHub' },
  { id: '1000000000000000002', name: 'Open Source' },
  { id: '1000000000000000003', name: 'Synthwave' },
  { id: '1000000000000000004', name: 'Game Dev' },
  { id: '1000000000000000005', name: 'Computer Science' },
  { id: '1000000000000000006', name: 'AI Research' },
  { id: '1000000000000000007', name: 'Cooking' },
  { id: '1000000000000000008', name: 'Game Squad' },
  { id: '1000000000000000009', name: 'Movie Night' }
];

var USER_SETTINGS = {
  guild_folders: [
    { id: null, guild_ids: ['1000000000000000001'] },
    { id: null, guild_ids: ['1000000000000000002'] },
    { id: null, guild_ids: ['1000000000000000003'] },
    { id: null, guild_ids: ['1000000000000000004'] },
    { id: '500001', name: 'Studies',  color: 0x5555FF, guild_ids: ['1000000000000000005', '1000000000000000006', '1000000000000000007'] },
    { id: '500002', name: 'Hangouts', color: 0xFF5500, guild_ids: ['1000000000000000008', '1000000000000000009'] }
  ]
};

// Channels for every guild (same shape Discord returns). Type 0 = text, 4 = category.
function channelsFor(guildId) {
  // A nicely-populated set so the channel list looks "real" with categories.
  return [
    { id: '2000000000000000001', type: 0, name: 'welcome',       parent_id: null,                 position: 0, last_message_id: '4000000000000000010' },
    { id: '2000000000000000002', type: 0, name: 'general',       parent_id: null,                 position: 1, last_message_id: '4000000000000000099' },
    { id: '2000000000000000010', type: 4, name: 'Tech Talk',     parent_id: null,                 position: 2 },
    { id: '2000000000000000011', type: 0, name: 'frontend',      parent_id: '2000000000000000010', position: 0, last_message_id: '4000000000000000050' },
    { id: '2000000000000000012', type: 0, name: 'backend',       parent_id: '2000000000000000010', position: 1, last_message_id: '4000000000000000051' },
    { id: '2000000000000000013', type: 0, name: 'devops',        parent_id: '2000000000000000010', position: 2, last_message_id: '4000000000000000052' },
    { id: '2000000000000000020', type: 4, name: 'Off Topic',     parent_id: null,                 position: 3 },
    { id: '2000000000000000021', type: 0, name: 'random',        parent_id: '2000000000000000020', position: 0, last_message_id: '4000000000000000060' },
    { id: '2000000000000000022', type: 0, name: 'memes',         parent_id: '2000000000000000020', position: 1, last_message_id: '4000000000000000061' }
  ];
}

// A friendly, plausible conversation. Discord API returns newest-first;
// model.packMessages then reverses to oldest-first. So write newest at top.
function messagesFor(channelId) {
  function mk(id, username, color, hours, mins, content, opts) {
    opts = opts || {};
    var ts = '2026-06-08T' + String(hours).padStart(2,'0') + ':' + String(mins).padStart(2,'0') + ':00.000Z';
    return {
      id: id,
      author: { id: opts.authorId || ('3' + id.slice(1)), username: username, global_name: opts.displayName || username },
      content: content,
      timestamp: ts,
      attachments: opts.attachments || [],
      embeds: opts.embeds || [],
      mentions: opts.mentions || []
    };
  }
  return [
    mk('4000000000000000107', 'alice',  0x00AAFF, 9, 18, 'great work everyone, ship it!'),
    mk('4000000000000000106', 'you',    0x5555FF, 9, 17, 'looks great team'),
    mk('4000000000000000105', 'bob',    0xFFAA00, 9, 17, 'thanks carol! deploying now'),
    mk('4000000000000000104', 'carol',  0x00AA55, 9, 16, 'I rewrote the menu layer to keep the scroll position when ambient polling fires — no more yanking the user back to the bottom when they were reading history.'),
    mk('4000000000000000103', 'alice',  0x00AAFF, 9, 15, 'yep, finalizing the deck'),
    mk('4000000000000000102', 'bob',    0xFFAA00, 9, 15, 'hey alice. ready for the demo?'),
    mk('4000000000000000101', 'alice',  0x00AAFF, 9, 14, 'morning everyone')
  ];
}

// Route a Discord REST request to a canned response. Mirrors the small surface
// the C app actually uses via lib/discord.js.
//
// A tiny async delay is applied so the watch's loading animation gets a moment to
// render before the data arrives — otherwise sync resolution skips the loader
// entirely and we can't screenshot it.
function ok(json, delayMs) {
  if (!delayMs) return { status: 200, json: json };
  return new Promise(function (resolve) {
    setTimeout(function () { resolve({ status: 200, json: json }); }, delayMs);
  });
}
function respond(method, path) {
  if (method === 'GET' && path === '/users/@me/guilds')   return ok(GUILDS, 1500);
  if (method === 'GET' && path === '/users/@me/settings') return ok(USER_SETTINGS);
  var m;
  if ((m = /^\/guilds\/([^/]+)\/channels$/.exec(path)) && method === 'GET')           return ok(channelsFor(m[1]), 600);
  if ((m = /^\/channels\/([^/]+)\/messages\?limit=\d+$/.exec(path)) && method === 'GET') return ok(messagesFor(m[1]), 600);
  if ((m = /^\/channels\/([^/]+)\/messages$/.exec(path)) && method === 'POST') return { status: 200, json: { id: 'demo-sent', content: 'ok' } };
  return { status: 404, json: null };
}

module.exports = { respond: respond, GUILDS: GUILDS, USER_SETTINGS: USER_SETTINGS };
