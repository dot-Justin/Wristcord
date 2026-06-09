// src/pkjs/index.js
var Clay = require('pebble-clay');
var clayConfig = require('../../config/index');
var customClay = require('../../config/custom-clay');
var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false });

var settingsLib = require('./lib/settings');
var clayLib = require('./lib/clay');
var discordLib = require('./lib/discord');
var model = require('./lib/model');
var paging = require('./lib/paging');
var gatewayLib = require('./lib/gateway');

// DEMO_MODE: routes Discord REST calls to a fake backend (lib/demo.js) instead of
// the network. Used only to capture marketing screenshots — kept false for prod.
// scripts/capture-store-shots.sh flips this and the C-side WC_DEMO sentinel.
var DEMO_MODE = false;
var demo = DEMO_MODE ? require('./lib/demo') : null;

function loadSettings() {
  // In demo mode, fake a token so the watch's has_token bit is set and the server
  // list actually fetches (the demo backend ignores the token value itself).
  if (DEMO_MODE) return settingsLib.normalize({ token: 'demo-token' });
  try { return settingsLib.normalize(JSON.parse(localStorage.getItem('wc_settings') || '{}')); }
  catch (e) { return settingsLib.normalize({}); }
}
function saveSettings(s) { localStorage.setItem('wc_settings', JSON.stringify(s)); }

function pushToWatch(s) {
  Pebble.sendAppMessage(settingsLib.watchSubset(s),
    function () { console.log('settings -> watch ok'); },
    function (e) { console.log('settings -> watch FAIL ' + JSON.stringify(e)); });
}

// ---- Gateway: maintains canonical read_state while the watchapp is open ----
// onStateChange fires when the gateway transitions in or out of READY. When it
// becomes READY we push HOME_REFRESH to the watch so the home page can ask for
// a fresh OP_HOME — the watch's initial fetch otherwise lands during
// IDENTIFYING and comes back with no DMs (gateway.getAllDMs() is empty).
var gateway = gatewayLib.create({
  getToken: currentToken,
  onStateChange: function (newState) {
    if (newState === 'READY') {
      // Invalidate any cached row pages that used "no gateway yet" data —
      // they shipped without mentionsMe (myUserId was null) and without
      // mention/unread badges. Next request will rebuild against the live map.
      cacheKey = null;
      try { Pebble.sendAppMessage({ HOME_REFRESH: 1 }); } catch (e) {}
    }
  }
});
function ensureGateway() {
  if (DEMO_MODE) return;                       // demo backend, no real auth
  var token = currentToken();
  if (token) gateway.start();
}

Pebble.addEventListener('ready', function () {
  pushToWatch(loadSettings());
  ensureGateway();
});

// Clay config closed: persist + push
Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(clay.generateUrl());
});
Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  var dict = clay.getSettings(e.response, false); // raw values, each wrapped as {value: ...}
  var merged = settingsLib.normalize(clayLib.unwrap(dict));
  saveSettings(merged);
  pushToWatch(merged);
  // Token may have changed; refresh the gateway connection.
  gateway.stop();
  ensureGateway();
});

// ---- Discord request adapter (XHR; attaches the user token) ----
function makeXhrRequest(getToken) {
  if (DEMO_MODE) {
    // Marketing/screenshot mode — return canned responses; never touch the network.
    return function (method, path) { return Promise.resolve(demo.respond(method, path)); };
  }
  return function (method, path, body) {
    return new Promise(function (resolve) {
      var xhr = new XMLHttpRequest();
      xhr.open(method, 'https://discord.com/api/v10' + path, true);
      xhr.setRequestHeader('Authorization', getToken());
      if (body) xhr.setRequestHeader('Content-Type', 'application/json');
      xhr.onload = function () {
        var j = null; try { j = JSON.parse(xhr.responseText); } catch (e) {}
        resolve({ status: xhr.status, json: j });
      };
      xhr.onerror = function () { resolve({ status: 0, json: null }); };
      xhr.send(body ? JSON.stringify(body) : null);
    });
  };
}

// ---- OP router with a simple page cache ----
var OP_GUILDS = 1, OP_CHANNELS = 2, OP_MESSAGES = 3, OP_SEND = 4, OP_MSG_FULL = 5, OP_ACK = 6;
var OP_HOME = 7, OP_DMS_ALL = 8, OP_MESSAGES_BEFORE = 9;
var MAX_ROWS_LEN = 1400;   // bytes of ROWS per batch (C inbox is 2048)
var cacheKey = null, cacheBatches = [];
var lastFullById = {};

function currentToken() { return loadSettings().token; }

function chunkText(s, size) {
  var out = [];
  var i = 0;
  while (i < s.length) {
    var end = Math.min(i + size, s.length);
    // Don't split a surrogate pair across chunks — a lone surrogate encodes to
    // invalid UTF-8, which hard-faults graphics_draw_text on real hardware.
    if (end < s.length) {
      var c = s.charCodeAt(end - 1);
      if (c >= 0xD800 && c <= 0xDBFF) end--;   // high surrogate -> defer to next chunk
    }
    out.push([s.slice(i, end)]);
    i = end;
  }
  return out.length ? out : [['']];
}

// returns { records: [...] } or { err: <code> }
// `extra` is the optional MESSAGE_KEY_TEXT value (for OP_MESSAGES_BEFORE's
// `before` parameter). Most ops ignore it.
function buildRecords(op, id, extra) {
  var client = discordLib.makeClient(makeXhrRequest(currentToken));
  function mapStatusErr(status) { return status === 401 ? 1 : (status === 429 ? 2 : 3); }
  if (op === OP_GUILDS) {
    return client.guilds().then(function (g) {
      if (g.status !== 200) return { err: mapStatusErr(g.status) };
      return client.userSettings().then(function (s) {
        var rows = model.buildServerList(g.json, (s.status === 200 ? s.json : {}) || {});
        return { records: rows.map(function (r) {
          return [r.kind, r.id, r.name, r.color, r.parentIndex, (r.memberColors || []).join(',')];
        }) };
      });
    });
  }
  if (op === OP_CHANNELS) {
    return client.channels(id).then(function (ch) {
      if (ch.status !== 200) return { err: mapStatusErr(ch.status) };
      // Join with the gateway's read_state if the connection is live; otherwise
      // emit empty unread/mention fields and let the C side fall back to the
      // local-persist readstate heuristic.
      var lookup = (gateway.getState() === 'READY') ? gateway.getReadState : null;
      var rows = model.buildChannelTree(ch.json, lookup);
      return { records: rows.map(function (r) {
        return [r.kind, r.id, r.name, r.parentIndex,
                r.lastMessageId || '', r.unread || '', r.mentionCount || ''];
      }) };
    });
  }
  if (op === OP_MESSAGES) {
    return client.messages(id, 20).then(function (m) {
      if (m.status !== 200) return { err: mapStatusErr(m.status) };
      var rows = model.packMessages(m.json, gateway.getMyUserId());
      lastFullById = {};
      rows.forEach(function (r) { lastFullById[r.id] = r.full; });
      // 7-field message row: [author, color, time, text, id, truncated, mentionsMe]
      return { records: rows.map(function (r) {
        return [r.author, r.color, r.time, r.text, r.id,
                r.truncated ? '1' : '0', r.mentionsMe ? '1' : '0'];
      }) };
    });
  }
  if (op === OP_HOME) {
    // Fetch guilds, user settings, and DMs in parallel via REST so the home
    // page populates as fast as the network allows — don't wait for the
    // gateway. Read-state (mention count, unread flag) layers in from the
    // gateway when it's ready; until then DMs show with all-read styling.
    var stng = loadSettings();
    var dmLimit = clampLimit(stng.dmCount, 3, 20);
    var serverLimit = clampLimit(stng.serverCount, 3, 20);
    return Promise.all([
      client.guilds(),
      client.userSettings(),
      client.dmChannels()
    ]).then(function (results) {
      var g = results[0], s = results[1], d = results[2];
      if (g.status !== 200) return { err: mapStatusErr(g.status) };
      var us = (s.status === 200 ? s.json : {}) || {};
      var restDms = (d.status === 200 && Array.isArray(d.json)) ? d.json : [];
      var lookup = (gateway.getState() === 'READY') ? gateway.getReadState : null;
      var dms = model.normalizeRestDms(restDms, lookup);
      var rows = model.buildHomePage({
        dms: dms,
        guilds: g.json,
        userSettings: us,
        guildStats: gateway.getGuildStats,
        dmLimit: dmLimit,
        serverLimit: serverLimit
      });
      return { records: encodeHomeRows(rows) };
    });
  }
  if (op === OP_DMS_ALL) {
    // Same fast path: REST is authoritative; gateway provides mention/unread
    // overlay only.
    var lookupAll = (gateway.getState() === 'READY') ? gateway.getReadState : null;
    return client.dmChannels().then(function (d) {
      if (d.status !== 200) return { err: mapStatusErr(d.status) };
      var dmsAll = model.normalizeRestDms(d.json || [], lookupAll);
      return { records: dmsAll.map(function (dm) {
        return ['D', dm.id, dm.name || '(unknown)',
                require('./lib/color').nameToAccentHex(dm.name || dm.recipientId || dm.id),
                String(dm.mentionCount | 0),
                dm.unread ? '1' : '0'];
      }) };
    });
  }
  if (op === OP_MSG_FULL) {
    var fullText = lastFullById[id] || '(message unavailable)';
    var chunks = chunkText(fullText, 100);
    return Promise.resolve({ records: chunks });
  }
  if (op === OP_MESSAGES_BEFORE) {
    // chat_view sends ID = channel id, TEXT = before message id (oldest currently loaded).
    var beforeId = extra || '';
    return client.messagesBefore(id, beforeId, 30).then(function (m) {
      if (m.status !== 200) return { err: mapStatusErr(m.status) };
      var rows = model.packMessages(m.json, gateway.getMyUserId());
      // Also stash full text so OP_MSG_FULL still works on older messages.
      rows.forEach(function (r) { lastFullById[r.id] = r.full; });
      return { records: rows.map(function (r) {
        return [r.author, r.color, r.time, r.text, r.id,
                r.truncated ? '1' : '0', r.mentionsMe ? '1' : '0'];
      }) };
    });
  }
  return Promise.resolve({ records: [] });
}

// Serialize the heterogeneous buildHomePage row shapes into ROWS records.
function encodeHomeRows(rows) {
  return rows.map(function (r) {
    if (r.kind === 'S') return ['S'];
    if (r.kind === 'H') return ['H', r.label || '', r.icon || ''];
    if (r.kind === 'D') return ['D', r.id, r.name || '', r.color || '',
                                String(r.mentionCount | 0), r.unread ? '1' : '0'];
    if (r.kind === 'M') return ['M', r.section || ''];
    if (r.kind === 'g') return ['g', r.id, r.name || '', r.color || '', '',
                                (r.memberColors || []).join(','),
                                String(r.pingCount | 0), r.unread ? '1' : '0',
                                r.mostRecent || '0', String(r.discordPos | 0)];
    return ['?'];
  });
}

function clampLimit(n, lo, hi) {
  n = (n | 0);
  if (!n || n < lo) return lo;
  if (n > hi) return hi;
  return n;
}

function sendPage(op, page) {
  var rows = cacheBatches[page] || '';
  var more = page < cacheBatches.length - 1 ? 1 : 0;
  Pebble.sendAppMessage({ OP: op, PAGE: page, MORE: more, ROWS: rows });
}

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload || {};
  // Watch → pkjs settings push: the on-watch Sort submenu sends a
  // PUSH_SORT_MODE int when the user picks a new mode. Persist it so the next
  // Clay config-page open reflects the change.
  if (p.PUSH_SORT_MODE !== undefined && p.PUSH_SORT_MODE !== null) {
    try {
      var cur = settingsLib.normalize(JSON.parse(localStorage.getItem('wc_settings') || '{}'));
      var idx = p.PUSH_SORT_MODE | 0;
      var name = (settingsLib.SORT_MODES || [])[idx];
      if (name) {
        cur.sortMode = name;
        saveSettings(cur);
      }
    } catch (e2) {}
  }
  var op = p.OP, id = p.ID || '', page = p.PAGE || 0;
  if (!op) return;                 // not a data request (e.g. settings echo)
  if (op === OP_SEND) {
    var client = discordLib.makeClient(makeXhrRequest(currentToken));
    client.sendMessage(id, p.TEXT || '').then(function (r) {
      var ok = (r.status === 200 || r.status === 201);
      Pebble.sendAppMessage({ OP: OP_SEND, PAGE: 0, MORE: 0, ROWS: '', ERR: ok ? 0 : (r.status === 401 ? 1 : (r.status === 429 ? 2 : 3)) });
    }).catch(function () {
      Pebble.sendAppMessage({ OP: OP_SEND, PAGE: 0, MORE: 0, ROWS: '', ERR: 3 });
    });
    return;
  }
  if (op === OP_ACK) {
    // Fire-and-forget watch-read ACK. Optimistically update the local gateway
    // map so the next OP_CHANNELS reflects it immediately; the gateway's own
    // MESSAGE_ACK echo will follow.
    var msgId = p.TEXT || '';
    if (id && msgId) {
      gateway.markRead(id, msgId);
      var ackClient = discordLib.makeClient(makeXhrRequest(currentToken));
      ackClient.ack(id, msgId).catch(function () { /* silent: gateway will recover */ });
    }
    return;
  }

  // OP_MESSAGES_BEFORE includes the `before` snowflake in the cache key so two
  // load-older requests for the same channel don't collide.
  var extra = p.TEXT || '';
  var key = op + ':' + id + (op === OP_MESSAGES_BEFORE ? ':' + extra : '');
  if (page === 0 || key !== cacheKey) {
    buildRecords(op, id, extra).then(function (res) {
      if (res.err) { Pebble.sendAppMessage({ OP: op, PAGE: 0, MORE: 0, ROWS: '', ERR: res.err }); return; }
      cacheKey = key;
      cacheBatches = paging.batches(paging.encodeRows(res.records), MAX_ROWS_LEN);
      if (cacheBatches.length === 0) cacheBatches = [''];
      sendPage(op, page);
    }).catch(function () {
      Pebble.sendAppMessage({ OP: op, PAGE: 0, MORE: 0, ROWS: '', ERR: 3 });
    });
  } else {
    sendPage(op, page);
  }
});
