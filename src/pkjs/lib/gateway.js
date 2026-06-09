// src/pkjs/lib/gateway.js
//
// Discord user-account gateway client for Wristcord v1.1.
// Owns one WebSocket connection to wss://gateway.discord.gg, maintains a
// per-channel read-state map (last_read_message_id + mention_count), and
// answers `isUnread(channelId, lastMsgId)` for the channel-list UI.
//
// pkjs only runs while the watchapp is open, so this connection lives only
// for that time. Designed for the pypkjs WebSocket polyfill (which matches
// the browser API) and the official Pebble mobile JS runtime (repebble.com).
//
// All IO is dependency-injected (WebSocket ctor, setTimeout, clearTimeout,
// setInterval, clearInterval) so the state machine can be unit-tested with a
// fake socket. The factory uses real globals by default.
'use strict';

var DEFAULT_GATEWAY_URL = 'wss://gateway.discord.gg/?v=10&encoding=json';

// Close codes Discord uses to mean "auth/intents broken, give up".
// (4004 = auth failed, 4010-4014 = sharding/intents/version errors.)
var FATAL_CLOSE_CODES = { 4004: 1, 4010: 1, 4011: 1, 4012: 1, 4013: 1, 4014: 1 };

// Backoff schedule (seconds) when reconnecting.
var BACKOFF = [1, 2, 4, 8, 15, 30];

// Snowflake compare without BigInt (pkjs is ES5-ish): IDs are decimal strings,
// always non-negative, so length wins; same length -> lexicographic.
function snowflakeGt(a, b) {
  if (!a) return false;
  if (!b) return true;
  if (a.length !== b.length) return a.length > b.length;
  return a > b;
}

function noop() {}

function create(opts) {
  opts = opts || {};
  var getToken = opts.getToken || function () { return ''; };
  var WS = opts.WebSocket || (typeof WebSocket !== 'undefined' ? WebSocket : null);
  var setTimeoutFn = opts.setTimeout || setTimeout;
  var clearTimeoutFn = opts.clearTimeout || clearTimeout;
  var setIntervalFn = opts.setInterval || setInterval;
  var clearIntervalFn = opts.clearInterval || clearInterval;
  var nowFn = opts.now || function () { return Date.now(); };
  var log = opts.log || function (msg) { try { console.log('[gw] ' + msg); } catch (e) {} };
  var onStateChange = opts.onStateChange || noop;
  // Mimic the real iOS Discord client. Generic strings flag the connection.
  var properties = opts.properties || {
    os: 'iOS',
    browser: 'Discord iOS',
    device: 'iPhone'
  };

  // Read-state map: channelId -> { lastReadId, mentionCount }
  var readMap = {};

  var state = 'DISCONNECTED';
  var ws = null;
  var seq = null;
  var sessionId = null;
  var resumeUrl = null;
  var heartbeatInterval = null;     // ms
  var heartbeatTimer = null;        // interval handle
  var lastHeartbeatAckedAt = 0;
  var lastHeartbeatSentAt = 0;
  var reconnectTimer = null;
  var attempt = 0;                  // reconnect attempt counter
  var stopped = true;               // .start() must be called to wake us up
  var wantResume = false;

  function setState(next) {
    if (state === next) return;
    log('state ' + state + ' -> ' + next);
    state = next;
    onStateChange(next);
  }

  function send(payload) {
    if (!ws || ws.readyState !== 1 /* OPEN */) return false;
    try {
      ws.send(JSON.stringify(payload));
      return true;
    } catch (e) {
      log('send threw: ' + (e && e.message));
      return false;
    }
  }

  function heartbeat() {
    if (!ws) return;
    // If the previous heartbeat wasn't acked before the next one was due, the
    // connection is dead. Force a reconnect with RESUME.
    if (lastHeartbeatSentAt > lastHeartbeatAckedAt &&
        lastHeartbeatSentAt > 0 &&
        nowFn() - lastHeartbeatSentAt > heartbeatInterval) {
      log('heartbeat not acked; forcing reconnect');
      forceReconnect(true);
      return;
    }
    lastHeartbeatSentAt = nowFn();
    send({ op: 1, d: seq });
  }

  function startHeartbeat() {
    stopHeartbeat();
    if (!heartbeatInterval) return;
    heartbeatTimer = setIntervalFn(heartbeat, heartbeatInterval);
  }
  function stopHeartbeat() {
    if (heartbeatTimer) { clearIntervalFn(heartbeatTimer); heartbeatTimer = null; }
  }

  function sendIdentify() {
    var token = getToken();
    if (!token) { log('no token; refusing to IDENTIFY'); return; }
    setState('IDENTIFYING');
    send({
      op: 2,
      d: {
        token: token,
        capabilities: 16381,
        properties: properties,
        presence: { status: 'online', afk: false, since: 0, activities: [] },
        compress: false,
        client_state: { guild_versions: {} }
      }
    });
  }

  function sendResume() {
    var token = getToken();
    if (!token || !sessionId) { sendIdentify(); return; }
    setState('RESUMING');
    send({
      op: 6,
      d: { token: token, session_id: sessionId, seq: seq }
    });
  }

  function ingestReadyReadState(d) {
    var entries = (d && d.read_state && d.read_state.entries) || d.read_state || [];
    if (!entries || !entries.length) return;
    for (var i = 0; i < entries.length; i++) {
      var e = entries[i];
      if (!e || !e.id) continue;
      readMap[String(e.id)] = {
        lastReadId: String(e.last_message_id || e.last_acked_id || '0'),
        mentionCount: e.mention_count | 0
      };
    }
    log('READY read_state loaded: ' + entries.length + ' channels');
  }

  function ingestMessageAck(d) {
    if (!d || !d.channel_id) return;
    var key = String(d.channel_id);
    var existing = readMap[key] || { lastReadId: '0', mentionCount: 0 };
    var msgId = String(d.message_id || '0');
    if (snowflakeGt(msgId, existing.lastReadId)) existing.lastReadId = msgId;
    if (typeof d.mention_count === 'number') existing.mentionCount = d.mention_count | 0;
    else existing.mentionCount = 0;   // ACK on a message clears mentions for that channel
    readMap[key] = existing;
  }

  function ingestMessageCreate(d) {
    // A new message in a channel increments potential unread state. If it's
    // our own message we should also advance lastReadId (effectively self-ACK).
    if (!d || !d.channel_id || !d.id) return;
    // We don't know "our user id" reliably here; the safe move is just to
    // *not* touch lastReadId on MESSAGE_CREATE. The channel will appear unread
    // when its `last_message_id` (from REST) advances past `lastReadId`.
    // mention_count updates come through MESSAGE_ACK + READY only.
    var key = String(d.channel_id);
    // If we have no prior entry, seed one so future ACKs land correctly.
    if (!readMap[key]) readMap[key] = { lastReadId: '0', mentionCount: 0 };
  }

  function handleDispatch(t, d) {
    if (t === 'READY') {
      sessionId = (d && d.session_id) || sessionId;
      resumeUrl = (d && d.resume_gateway_url) || resumeUrl;
      ingestReadyReadState(d);
      attempt = 0;             // success -> reset backoff
      setState('READY');
    } else if (t === 'READY_SUPPLEMENTAL') {
      ingestReadyReadState(d);
    } else if (t === 'RESUMED') {
      attempt = 0;
      setState('READY');
    } else if (t === 'MESSAGE_ACK') {
      ingestMessageAck(d);
    } else if (t === 'MESSAGE_CREATE') {
      ingestMessageCreate(d);
    }
    // ignore everything else
  }

  function handleMessage(raw) {
    var msg;
    try { msg = JSON.parse(raw); } catch (e) { log('bad json'); return; }
    if (msg.s != null) seq = msg.s;
    var op = msg.op;
    if (op === 10) {
      heartbeatInterval = (msg.d && msg.d.heartbeat_interval) || 41250;
      lastHeartbeatAckedAt = nowFn();
      lastHeartbeatSentAt = 0;
      startHeartbeat();
      if (wantResume && sessionId && seq != null) sendResume();
      else sendIdentify();
    } else if (op === 11) {
      lastHeartbeatAckedAt = nowFn();
    } else if (op === 1) {
      // Discord asked for an immediate heartbeat.
      heartbeat();
    } else if (op === 7) {
      // Reconnect: clean close, then try RESUME.
      log('Op 7 RECONNECT -> reconnecting w/ resume');
      forceReconnect(true);
    } else if (op === 9) {
      // INVALID_SESSION. d=true means session was resumable; d=false means not.
      var resumable = !!msg.d;
      log('Op 9 INVALID_SESSION (resumable=' + resumable + ')');
      if (!resumable) { sessionId = null; seq = null; }
      // Discord wants us to wait 1-5s before re-IDENTIFYing.
      forceReconnect(resumable, 1000 + Math.floor(Math.random() * 4000));
    } else if (op === 0) {
      handleDispatch(msg.t, msg.d);
    }
  }

  function clearWs() {
    if (ws) {
      try { ws.onopen = ws.onmessage = ws.onerror = ws.onclose = null; } catch (e) {}
      try { if (ws.readyState !== 3 /* CLOSED */) ws.close(); } catch (e) {}
      ws = null;
    }
    stopHeartbeat();
  }

  function forceReconnect(tryResume, delayMs) {
    wantResume = !!tryResume && sessionId != null;
    clearWs();
    if (stopped) { setState('DISCONNECTED'); return; }
    setState('CONNECTING');
    if (reconnectTimer) clearTimeoutFn(reconnectTimer);
    var d = delayMs;
    if (d == null) {
      var idx = Math.min(attempt, BACKOFF.length - 1);
      d = BACKOFF[idx] * 1000;
      attempt++;
    }
    reconnectTimer = setTimeoutFn(connect, d);
  }

  function connect() {
    reconnectTimer = null;
    if (stopped) { setState('DISCONNECTED'); return; }
    if (!WS) { log('no WebSocket polyfill; gateway disabled'); setState('DISCONNECTED'); return; }
    if (!getToken()) { log('no token; not connecting'); setState('DISCONNECTED'); return; }
    var url = (wantResume && resumeUrl) ? (resumeUrl + '/?v=10&encoding=json') : DEFAULT_GATEWAY_URL;
    setState('CONNECTING');
    try { ws = new WS(url); } catch (e) {
      log('WS ctor threw: ' + (e && e.message));
      ws = null;
      forceReconnect(false);
      return;
    }
    ws.onopen = function () { setState('AWAITING_HELLO'); };
    ws.onmessage = function (ev) { handleMessage(ev && ev.data); };
    ws.onerror = function () { log('ws error'); };
    ws.onclose = function (ev) {
      var code = ev && ev.code;
      log('close code=' + code);
      stopHeartbeat();
      if (code && FATAL_CLOSE_CODES[code]) {
        setState('DISCONNECTED');
        stopped = true;        // user must call start() again (e.g. after re-auth)
        return;
      }
      forceReconnect(true);
    };
  }

  return {
    start: function () {
      stopped = false;
      attempt = 0;
      wantResume = false;
      sessionId = null;
      seq = null;
      resumeUrl = null;
      connect();
    },
    stop: function () {
      stopped = true;
      if (reconnectTimer) { clearTimeoutFn(reconnectTimer); reconnectTimer = null; }
      clearWs();
      setState('DISCONNECTED');
    },
    getState: function () { return state; },
    getReadState: function (channelId) {
      return readMap[String(channelId)] || null;
    },
    isUnread: function (channelId, lastMessageId) {
      var entry = readMap[String(channelId)];
      if (!entry) return null;            // null = no info, caller decides
      if (!lastMessageId) return false;
      return snowflakeGt(String(lastMessageId), entry.lastReadId);
    },
    markRead: function (channelId, messageId) {
      // Optimistic local update for the watch's own ACK; the gateway will
      // confirm via MESSAGE_ACK shortly.
      if (!channelId || !messageId) return;
      var key = String(channelId);
      var e = readMap[key] || { lastReadId: '0', mentionCount: 0 };
      var m = String(messageId);
      if (snowflakeGt(m, e.lastReadId)) e.lastReadId = m;
      e.mentionCount = 0;
      readMap[key] = e;
    },
    // Test seam: directly inject a parsed dispatch (e.g. simulate MESSAGE_ACK).
    _handleMessage: handleMessage,
    _snowflakeGt: snowflakeGt
  };
}

module.exports = { create: create, _snowflakeGt: snowflakeGt };
