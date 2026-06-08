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

/**
 * buildChannelTree(channels) -> flat ordered array of row objects.
 *
 * Row kinds:
 *   {kind:'c', id, name, parentIndex:''}              -- category
 *   {kind:'t', id, name, parentIndex}                 -- text channel
 */
function buildChannelTree(channels) {
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

  // Uncategorized first
  for (var ui = 0; ui < uncategorized.length; ui++) {
    var u = uncategorized[ui];
    rows.push({ kind: 't', id: u.id, name: u.name, parentIndex: '', lastMessageId: String(u.last_message_id || '') });
  }

  // Categories with their children
  for (var ci = 0; ci < categories.length; ci++) {
    var cat = categories[ci];
    var catIndex = rows.length;
    rows.push({ kind: 'c', id: cat.id, name: cat.name, parentIndex: '', lastMessageId: '' });

    var children = byParent[cat.id] || [];
    children.sort(function(a, b) { return a.position - b.position; });
    for (var ki = 0; ki < children.length; ki++) {
      var ch2 = children[ki];
      rows.push({ kind: 't', id: ch2.id, name: ch2.name, parentIndex: catIndex, lastMessageId: String(ch2.last_message_id || '') });
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

function packMessages(messages) {
  // Discord returns newest-first; reverse to oldest-first
  var ordered = messages.slice().reverse();

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

    return { author: author, color: msgColor, time: time, text: text, id: String(msg.id), full: full, truncated: truncated };
  });
}

module.exports = {
  buildServerList: buildServerList,
  buildChannelTree: buildChannelTree,
  packMessages: packMessages,
  cleanText: cleanText,
};
