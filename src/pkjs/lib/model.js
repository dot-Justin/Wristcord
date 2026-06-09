// src/pkjs/lib/model.js
// Pure transform module: Discord API responses -> flat row arrays for Pebble.
// No IO, no globals.
'use strict';

var color = require('./color');
var nameToAccentHex = color.nameToAccentHex;

/**
 * buildServerList(guilds, userSettings) -> flat ordered array of row objects.
 *
 * Row kinds:
 *   {kind:'f', id, name, color, parentIndex:'', memberColors:[]}  -- folder
 *   {kind:'g', id, name, color, parentIndex, memberColors:[]}     -- guild
 */
function buildServerList(guilds, userSettings) {
  // Build id -> name lookup
  var guildMap = {};
  for (var i = 0; i < guilds.length; i++) {
    guildMap[guilds[i].id] = guilds[i].name;
  }

  var folders = (userSettings && userSettings.guild_folders) || [];

  // Fallback: no folder info -> return all guilds flat
  if (!folders.length) {
    return guilds.map(function(g) {
      return {
        kind: 'g',
        id: g.id,
        name: g.name,
        color: nameToAccentHex(g.name),
        parentIndex: '',
        memberColors: [],
      };
    });
  }

  var rows = [];
  var seen = {};  // guild ids referenced by folders

  for (var fi = 0; fi < folders.length; fi++) {
    var entry = folders[fi];
    var guildIds = entry.guild_ids || [];

    if (entry.id != null) {
      // FOLDER row
      var folderName = entry.name || 'Folder';

      // Compute folder color
      var folderColor;
      if (typeof entry.color === 'number') {
        // 0 is a valid colour (black)
        var hex = (entry.color & 0xFFFFFF).toString(16).toUpperCase();
        while (hex.length < 6) hex = '0' + hex;
        folderColor = '0x' + hex;
      } else {
        // Fallback: first member's accent, or default
        var firstMemberName = guildMap[guildIds[0]];
        folderColor = firstMemberName
          ? nameToAccentHex(firstMemberName)
          : '0x5555FF';
      }

      // memberColors: up to first 4 guild_ids
      var memberColors = [];
      var maxDots = Math.min(guildIds.length, 4);
      for (var m = 0; m < maxDots; m++) {
        var mn = guildMap[guildIds[m]] || guildIds[m];
        memberColors.push(nameToAccentHex(mn));
      }

      var folderIndex = rows.length;
      rows.push({
        kind: 'f',
        id: String(entry.id),
        name: folderName,
        color: folderColor,
        parentIndex: '',
        memberColors: memberColors,
      });

      // Child guild rows
      for (var gi = 0; gi < guildIds.length; gi++) {
        var gid = guildIds[gi];
        seen[gid] = true;
        var gname = guildMap[gid] || gid;
        rows.push({
          kind: 'g',
          id: gid,
          name: gname,
          color: nameToAccentHex(gname),
          parentIndex: folderIndex,
          memberColors: [],
        });
      }
    } else {
      // STANDALONE top-level guilds
      for (var si = 0; si < guildIds.length; si++) {
        var sgid = guildIds[si];
        seen[sgid] = true;
        var sgname = guildMap[sgid] || sgid;
        rows.push({
          kind: 'g',
          id: sgid,
          name: sgname,
          color: nameToAccentHex(sgname),
          parentIndex: '',
          memberColors: [],
        });
      }
    }
  }

  // Append orphan guilds (not referenced by any folder)
  for (var oi = 0; oi < guilds.length; oi++) {
    var og = guilds[oi];
    if (!seen[og.id]) {
      rows.push({
        kind: 'g',
        id: og.id,
        name: og.name,
        color: nameToAccentHex(og.name),
        parentIndex: '',
        memberColors: [],
      });
    }
  }

  return rows;
}

// Snowflake compare without BigInt: same length -> lex, else longer wins.
function snowflakeGt(a, b) {
  if (!a) return false;
  if (!b) return true;
  if (a.length !== b.length) return a.length > b.length;
  return a > b;
}

/**
 * buildChannelTree(channels, readStateLookup?) -> flat ordered array of row objects.
 *
 * readStateLookup: optional fn(channelId) -> {lastReadId, mentionCount} | null.
 * When present, text-channel rows include `unread` ('0'/'1'/'') + `mentionCount`
 * ('0' decimal string) derived from the gateway's read-state map. When omitted
 * or returns null for a channel, both fields are empty strings so the C side
 * falls back to the local-persist readstate heuristic.
 *
 * Row kinds:
 *   {kind:'c', id, name, parentIndex:''}              -- category
 *   {kind:'t', id, name, parentIndex,                 -- text channel
 *      lastMessageId, unread, mentionCount}
 */
function buildChannelTree(channels, readStateLookup) {
  var TEXT_TYPES = { 0: true, 5: true };  // 5 = announcement, treat as text
  var CAT_TYPE = 4;

  var categories = [];
  var uncategorized = [];
  var byParent = {};

  for (var i = 0; i < channels.length; i++) {
    var ch = channels[i];
    if (ch.type === CAT_TYPE) {
      categories.push(ch);
    } else if (TEXT_TYPES[ch.type]) {
      if (!ch.parent_id) {
        uncategorized.push(ch);
      } else {
        if (!byParent[ch.parent_id]) byParent[ch.parent_id] = [];
        byParent[ch.parent_id].push(ch);
      }
    }
    // All other types (voice etc.) are ignored
  }

  // Sort by position
  uncategorized.sort(function(a, b) { return a.position - b.position; });
  categories.sort(function(a, b) { return a.position - b.position; });

  var rows = [];

  function makeChannelRow(ch, parentIndex) {
    var lastMsg = String(ch.last_message_id || '');
    var unread = '';
    var mentionCount = '';
    if (typeof readStateLookup === 'function') {
      var rs = readStateLookup(ch.id);
      if (rs) {
        unread = (lastMsg && snowflakeGt(lastMsg, rs.lastReadId || '0')) ? '1' : '0';
        mentionCount = String(rs.mentionCount | 0);
      } else {
        // Gateway is live but Discord has no read_state entry for this channel —
        // that means Discord considers it read (user has never had unread
        // activity there). Emit '0' explicitly so the C side trusts Discord
        // instead of falling back to the local "have I shown this" heuristic.
        unread = '0';
        mentionCount = '0';
      }
    }
    return {
      kind: 't', id: ch.id, name: ch.name, parentIndex: parentIndex,
      lastMessageId: lastMsg, unread: unread, mentionCount: mentionCount
    };
  }

  // Uncategorized first
  for (var ui = 0; ui < uncategorized.length; ui++) {
    rows.push(makeChannelRow(uncategorized[ui], ''));
  }

  // Categories with their children
  for (var ci = 0; ci < categories.length; ci++) {
    var cat = categories[ci];
    var catIndex = rows.length;
    rows.push({ kind: 'c', id: cat.id, name: cat.name, parentIndex: '',
                lastMessageId: '', unread: '', mentionCount: '' });

    var children = byParent[cat.id] || [];
    children.sort(function(a, b) { return a.position - b.position; });
    for (var ki = 0; ki < children.length; ki++) {
      rows.push(makeChannelRow(children[ki], catIndex));
    }
  }

  return rows;
}

/**
 * cleanText(content, msg) -> plain string safe for Pebble display.
 * Strips Discord markup, resolves mentions using msg.mentions.
 */
function cleanText(content, msg) {
  if (!content) return '';
  var byId = {};
  ((msg && msg.mentions) || []).forEach(function (u) { byId[u.id] = u.global_name || u.username; });
  return content
    .replace(/<a?:(\w+):\d+>/g, ':$1:')                                   // custom emoji -> :name:
    .replace(/<@!?(\d+)>/g, function (m, id) { return '@' + (byId[id] || 'user'); })  // user mention
    .replace(/<@&\d+>/g, '@role')                                         // role mention
    .replace(/<#\d+>/g, '#channel')                                       // channel mention
    .replace(/```[a-zA-Z]*\n?([\s\S]*?)```/g, '$1')                       // code block
    .replace(/`([^`]+)`/g, '$1')                                          // inline code
    .replace(/\*\*(.+?)\*\*/g, '$1')                                      // bold
    .replace(/__(.+?)__/g, '$1')                                          // underline
    .replace(/~~(.+?)~~/g, '$1')                                          // strike
    .replace(/\*(.+?)\*/g, '$1')                                          // italic
    .replace(/^>\s?/gm, '')                                               // blockquote markers
    .replace(/\s+/g, ' ')                                                 // collapse newlines/runs of whitespace
    .trim();
}

/**
 * packMessages(messages) -> array, oldest first.
 * Each: {author, color, time, text}
 */
// Attachments/embeds can't be rendered on the watch — surface a clear text tag instead.
function attachmentTag(msg) {
  var atts = (msg && msg.attachments) || [];
  var embeds = (msg && msg.embeds) || [];
  var hasImage = atts.some(function (a) {
    return (a.content_type && a.content_type.indexOf('image/') === 0) || (a.width && a.height);
  }) || embeds.some(function (e) { return e.type === 'image' || e.image || e.thumbnail; });
  if (hasImage) return '[image]';
  if (atts.length) return '[attachment]';
  return '';
}

function packMessages(messages, myUserId) {
  // Discord returns newest-first; reverse to oldest-first
  var ordered = messages.slice().reverse();
  var me = myUserId ? String(myUserId) : '';

  return ordered.map(function(msg) {
    var author = msg.author.global_name || msg.author.username;
    var msgColor = nameToAccentHex(author);

    // Parse timestamp UTC h:mm
    var d = new Date(msg.timestamp);
    var h = d.getUTCHours();
    var m = d.getUTCMinutes();
    var mm = m < 10 ? '0' + m : String(m);
    var time = h + ':' + mm;

    // Clean content, prepend an attachment tag the watch can render (it can't show images).
    var cleaned = cleanText(msg.content, msg);
    var tag = attachmentTag(msg);
    var body = tag ? (cleaned ? tag + ' ' + cleaned : tag) : cleaned;
    var full, text, truncated;
    if (!body) {
      full = '[no text]';
      text = '[no text]';
      truncated = false;
    } else if (body.length > 120) {
      full = body;
      // Don't cut on a high surrogate — a lone surrogate becomes invalid UTF-8,
      // which hard-faults graphics_draw_text on hardware.
      var cut = 120;
      var cc = body.charCodeAt(cut - 1);
      if (cc >= 0xD800 && cc <= 0xDBFF) cut--;
      text = body.slice(0, cut) + '…';  // '…'
      truncated = true;
    } else {
      full = body;
      text = body;
      truncated = false;
    }

    // v1.2: does this message @mention me? Used by the C side to draw a goldish
    // background tint on the cell.
    var mentionsMe = false;
    if (me) {
      var mentions = msg.mentions || [];
      for (var mi = 0; mi < mentions.length; mi++) {
        if (mentions[mi] && String(mentions[mi].id) === me) { mentionsMe = true; break; }
      }
    }

    return { author: author, color: msgColor, time: time, text: text,
             id: String(msg.id), full: full, truncated: truncated,
             mentionsMe: mentionsMe };
  });
}

// Normalize a REST DM channel (from GET /users/@me/channels) to the same shape
// gateway.getAllDMs() returns. Layers in unread/mention info from the gateway
// readMap when available, so the page works even before READY.
function normalizeRestDms(restDms, readStateLookup) {
  function recipName(r) { return (r && (r.global_name || r.username)) || 'user'; }
  function snowflakeGtLocal(a, b) {
    if (!a) return false; if (!b) return true;
    if (a.length !== b.length) return a.length > b.length;
    return a > b;
  }
  var out = [];
  for (var i = 0; i < restDms.length; i++) {
    var c = restDms[i];
    if (!c || (c.type !== 1 && c.type !== 3)) continue;
    var name;
    if (c.type === 1) {
      name = recipName((c.recipients || [])[0]);
    } else {
      if (c.name) name = c.name;
      else {
        var labels = [];
        for (var j = 0; j < (c.recipients || []).length && j < 4; j++) labels.push(recipName(c.recipients[j]));
        name = labels.join(', ');
      }
    }
    var lastMsg = String(c.last_message_id || '0');
    var rs = (typeof readStateLookup === 'function') ? readStateLookup(c.id) : null;
    var recipId = (c.recipients && c.recipients[0] && c.recipients[0].id) ? String(c.recipients[0].id) : '';
    out.push({
      id: String(c.id),
      type: c.type,
      name: name,
      lastMessageId: lastMsg,
      recipientId: recipId,
      mentionCount: rs ? (rs.mentionCount | 0) : 0,
      unread: rs ? snowflakeGtLocal(lastMsg, rs.lastReadId || '0') : false
    });
  }
  out.sort(function (a, b) {
    if (a.lastMessageId.length !== b.lastMessageId.length) {
      return b.lastMessageId.length - a.lastMessageId.length;
    }
    return a.lastMessageId < b.lastMessageId ? 1 : (a.lastMessageId > b.lastMessageId ? -1 : 0);
  });
  return out;
}

/**
 * buildHomePage(opts) -> flat row array for the home page.
 *
 * opts = {
 *   dms,             // array of {id, type, name, lastMessageId, mentionCount, unread, recipientId}
 *   guilds,          // raw Discord /users/@me/guilds response
 *   userSettings,    // /users/@me/settings response (for folder data)
 *   guildStats,      // fn(guildId) -> {unreadChannels, mentionCount, mostRecentMessageId}
 *   dmLimit,         // int 3..20
 *   serverLimit      // int 3..20
 * }
 *
 * Row kinds (first field):
 *   S = settings entry (alone, no other fields)
 *   H = section header — fields[1] = label, fields[2] = icon code ('dm'|'server')
 *   D = DM row — fields[1]=id, [2]=name, [3]=color, [4]=mentionCount, [5]=unreadBool
 *   M = "Show all" row — fields[1]=section id ('dm'|'server')
 *   g = guild preview row — same as buildServerList guild row, with extras:
 *       fields[1]=id, [2]=name, [3]=color, [4]='' (no parent), [5]=memberColorsCsv,
 *       [6]=pingCount, [7]=unreadBool
 */
function buildHomePage(opts) {
  opts = opts || {};
  var dmLimit = clampLimit(opts.dmLimit, 3, 20);
  var serverLimit = clampLimit(opts.serverLimit, 3, 20);
  var rows = [];

  // Settings entry
  rows.push({ kind: 'S' });

  // ---- DM section ----
  rows.push({ kind: 'H', label: 'Direct Messages', icon: 'dm' });
  var dms = opts.dms || [];
  var dmShown = Math.min(dmLimit, dms.length);
  for (var i = 0; i < dmShown; i++) {
    var dm = dms[i];
    rows.push({
      kind: 'D',
      id: dm.id,
      name: dm.name,
      color: nameToAccentHex(dm.name || dm.recipientId || dm.id),
      mentionCount: dm.mentionCount | 0,
      unread: !!dm.unread
    });
  }
  if (dms.length > dmLimit) {
    rows.push({ kind: 'M', section: 'dm' });
  }

  // ---- Server section ----
  // We send ALL guilds (in Discord folder order) so the C side can re-sort by
  // the user's chosen mode + local view counts + decide its own top-N cut. A
  // trailing "Show all servers" row marks where the preview window ends; the
  // C side uses serverLimit to slice the sorted list.
  rows.push({ kind: 'H', label: 'Servers', icon: 'server' });
  var serverRows = buildServerList(opts.guilds || [], opts.userSettings || {});
  var guildOnly = serverRows.filter(function (r) { return r.kind === 'g'; });
  for (var j = 0; j < guildOnly.length; j++) {
    var g = guildOnly[j];
    var st = (typeof opts.guildStats === 'function') ?
             (opts.guildStats(g.id) || { unreadChannels: 0, mentionCount: 0, mostRecentMessageId: '0' }) :
             { unreadChannels: 0, mentionCount: 0, mostRecentMessageId: '0' };
    rows.push({
      kind: 'g',
      id: g.id,
      name: g.name,
      color: g.color,
      memberColors: [],
      pingCount: st.mentionCount | 0,
      unread: st.unreadChannels > 0,
      mostRecent: st.mostRecentMessageId || '0',
      discordPos: j                    // 0..n-1, Discord's folder order
    });
  }
  // The "Show all" marker tells the C side where the preview slice ends; the
  // C side hides any 'g' rows after the limit when computing visible_count.
  if (guildOnly.length > serverLimit) {
    rows.push({ kind: 'M', section: 'server', limit: serverLimit });
  }

  return rows;
}

function clampLimit(n, lo, hi) {
  n = (n | 0);
  if (!n || n < lo) return lo;
  if (n > hi) return hi;
  return n;
}

module.exports = {
  buildServerList: buildServerList,
  buildChannelTree: buildChannelTree,
  packMessages: packMessages,
  cleanText: cleanText,
  buildHomePage: buildHomePage,
  normalizeRestDms: normalizeRestDms
};
