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

function loadSettings() {
  try { return settingsLib.normalize(JSON.parse(localStorage.getItem('wc_settings') || '{}')); }
  catch (e) { return settingsLib.normalize({}); }
}
function saveSettings(s) { localStorage.setItem('wc_settings', JSON.stringify(s)); }

function pushToWatch(s) {
  Pebble.sendAppMessage(settingsLib.watchSubset(s),
    function () { console.log('settings -> watch ok'); },
    function (e) { console.log('settings -> watch FAIL ' + JSON.stringify(e)); });
}

Pebble.addEventListener('ready', function () {
  pushToWatch(loadSettings());
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
});

// ---- Discord request adapter (XHR; attaches the user token) ----
function makeXhrRequest(getToken) {
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
var OP_GUILDS = 1, OP_CHANNELS = 2, OP_MESSAGES = 3, OP_SEND = 4, OP_MSG_FULL = 5;
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
function buildRecords(op, id) {
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
      var rows = model.buildChannelTree(ch.json);
      return { records: rows.map(function (r) { return [r.kind, r.id, r.name, r.parentIndex, r.lastMessageId || '']; }) };
    });
  }
  if (op === OP_MESSAGES) {
    return client.messages(id, 20).then(function (m) {
      if (m.status !== 200) return { err: mapStatusErr(m.status) };
      var rows = model.packMessages(m.json);
      lastFullById = {};
      rows.forEach(function (r) { lastFullById[r.id] = r.full; });
      return { records: rows.map(function (r) { return [r.author, r.color, r.time, r.text, r.id, r.truncated ? '1' : '0']; }) };
    });
  }
  if (op === OP_MSG_FULL) {
    var fullText = lastFullById[id] || '(message unavailable)';
    var chunks = chunkText(fullText, 100);
    return Promise.resolve({ records: chunks });
  }
  return Promise.resolve({ records: [] });
}

function sendPage(op, page) {
  var rows = cacheBatches[page] || '';
  var more = page < cacheBatches.length - 1 ? 1 : 0;
  Pebble.sendAppMessage({ OP: op, PAGE: page, MORE: more, ROWS: rows });
}

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload || {};
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

  var key = op + ':' + id;
  if (page === 0 || key !== cacheKey) {
    buildRecords(op, id).then(function (res) {
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
