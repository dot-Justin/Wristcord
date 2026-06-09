// test/gateway.test.js — covers the v1.1 gateway state machine
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { create, _snowflakeGt } = require('../src/pkjs/lib/gateway');

// Fake WebSocket factory: holds the last instance globally so tests can drive it.
function makeWsHarness() {
  const harness = { instances: [], sent: [], last: null };
  function FakeWS(url) {
    const inst = {
      url, readyState: 0, sent: [],
      onopen: null, onmessage: null, onclose: null, onerror: null,
      send(s) { this.sent.push(s); harness.sent.push(s); },
      close(code, reason) {
        this.readyState = 3;
        if (this.onclose) this.onclose({ code: code || 1000, reason: reason || '' });
      },
      _open() { this.readyState = 1; if (this.onopen) this.onopen(); },
      _recv(obj) {
        if (this.onmessage) this.onmessage({ data: typeof obj === 'string' ? obj : JSON.stringify(obj) });
      },
      _close(code) { this.readyState = 3; if (this.onclose) this.onclose({ code: code }); },
    };
    harness.instances.push(inst);
    harness.last = inst;
    return inst;
  }
  return { harness, FakeWS };
}

// A flushable fake clock so the backoff/timeouts don't have to be real.
function makeClock() {
  let now = 0;
  const timers = [];
  function setTimeoutFn(fn, ms) { const t = { fn, due: now + ms, kind: 't' }; timers.push(t); return t; }
  function clearTimeoutFn(t) { if (t) t.fn = null; }
  function setIntervalFn(fn, ms) { const t = { fn, due: now + ms, ms, kind: 'i' }; timers.push(t); return t; }
  function clearIntervalFn(t) { if (t) t.fn = null; }
  function advance(ms) {
    now += ms;
    while (true) {
      let next = null;
      for (const t of timers) if (t.fn && t.due <= now && (!next || t.due < next.due)) next = t;
      if (!next) break;
      const fn = next.fn;
      if (next.kind === 'i') next.due = now + next.ms;     // re-arm interval
      else next.fn = null;                                  // one-shot
      fn();
    }
  }
  return { setTimeoutFn, clearTimeoutFn, setIntervalFn, clearIntervalFn, now: () => now, advance };
}

function setupGateway(opts) {
  opts = opts || {};
  const { harness, FakeWS } = makeWsHarness();
  const clock = makeClock();
  const gw = create({
    getToken: opts.getToken || (() => 'TEST_TOKEN'),
    WebSocket: FakeWS,
    setTimeout: clock.setTimeoutFn,
    clearTimeout: clock.clearTimeoutFn,
    setInterval: clock.setIntervalFn,
    clearInterval: clock.clearIntervalFn,
    now: clock.now,
    log: () => {},
  });
  return { gw, harness, clock };
}

function helloAndReady(gw, harness, opts) {
  opts = opts || {};
  harness.last._open();
  harness.last._recv({ op: 10, d: { heartbeat_interval: 41250 } });
  harness.last._recv({
    op: 0, t: 'READY', s: 1,
    d: {
      session_id: 'SID',
      resume_gateway_url: opts.resumeUrl || 'wss://resume.gateway.discord.gg',
      read_state: {
        entries: opts.entries || [
          { id: '111', last_message_id: '900', mention_count: 0 },
          { id: '222', last_message_id: '800', mention_count: 3 },
        ],
      },
    },
  });
}

// ---------------------------------------------------------------------------

test('snowflakeGt: length-then-lex comparison', () => {
  assert.equal(_snowflakeGt('999', '1000'), false);
  assert.equal(_snowflakeGt('1000', '999'), true);
  assert.equal(_snowflakeGt('1234567890123456789', '1234567890123456788'), true);
  assert.equal(_snowflakeGt('100', '100'), false);
  assert.equal(_snowflakeGt('', '100'), false);
  assert.equal(_snowflakeGt('100', ''), true);
});

test('start() → CONNECTING → AWAITING_HELLO → IDENTIFYING on HELLO', () => {
  const { gw, harness } = setupGateway();
  gw.start();
  assert.equal(gw.getState(), 'CONNECTING');
  harness.last._open();
  assert.equal(gw.getState(), 'AWAITING_HELLO');
  harness.last._recv({ op: 10, d: { heartbeat_interval: 41250 } });
  assert.equal(gw.getState(), 'IDENTIFYING');
  const idPayload = JSON.parse(harness.last.sent[0]);
  assert.equal(idPayload.op, 2);
  assert.equal(idPayload.d.token, 'TEST_TOKEN');
  assert.equal(idPayload.d.properties.browser, 'Discord iOS');
});

test('READY ingests read_state entries and transitions to READY', () => {
  const { gw, harness } = setupGateway();
  gw.start();
  helloAndReady(gw, harness);
  assert.equal(gw.getState(), 'READY');
  assert.deepEqual(gw.getReadState('111'), { lastReadId: '900', mentionCount: 0 });
  assert.deepEqual(gw.getReadState('222'), { lastReadId: '800', mentionCount: 3 });
  assert.equal(gw.getReadState('333'), null);
});

test('isUnread compares channel last_message_id against read map', () => {
  const { gw, harness } = setupGateway();
  gw.start();
  helloAndReady(gw, harness);
  assert.equal(gw.isUnread('111', '900'), false);   // same
  assert.equal(gw.isUnread('111', '901'), true);    // newer
  assert.equal(gw.isUnread('222', '801'), true);
  assert.equal(gw.isUnread('999', '901'), null);    // unknown channel -> null
});

test('MESSAGE_ACK updates read map and clears mention_count when omitted', () => {
  const { gw, harness } = setupGateway();
  gw.start();
  helloAndReady(gw, harness);
  harness.last._recv({ op: 0, t: 'MESSAGE_ACK', s: 2,
    d: { channel_id: '222', message_id: '850' } });
  assert.deepEqual(gw.getReadState('222'), { lastReadId: '850', mentionCount: 0 });
  // ACK with explicit mention_count
  harness.last._recv({ op: 0, t: 'MESSAGE_ACK', s: 3,
    d: { channel_id: '222', message_id: '900', mention_count: 7 } });
  assert.deepEqual(gw.getReadState('222'), { lastReadId: '900', mentionCount: 7 });
});

test('MESSAGE_ACK for unknown channel creates a new entry', () => {
  const { gw, harness } = setupGateway();
  gw.start();
  helloAndReady(gw, harness);
  harness.last._recv({ op: 0, t: 'MESSAGE_ACK', s: 2,
    d: { channel_id: '555', message_id: '7777', mention_count: 1 } });
  assert.deepEqual(gw.getReadState('555'), { lastReadId: '7777', mentionCount: 1 });
});

test('heartbeat sent at interval with last seq', () => {
  const { gw, harness, clock } = setupGateway();
  gw.start();
  helloAndReady(gw, harness);
  // drain identify+resume traffic
  harness.last.sent = [];
  clock.advance(41250);
  const hb = JSON.parse(harness.last.sent[0]);
  assert.equal(hb.op, 1);
  assert.equal(hb.d, 1);                            // seq from READY
});

test('Op 11 HEARTBEAT_ACK keeps the connection alive (no force reconnect)', () => {
  const { gw, harness, clock } = setupGateway();
  gw.start();
  helloAndReady(gw, harness);
  harness.last.sent = [];
  clock.advance(41250);                             // sends heartbeat
  harness.last._recv({ op: 11 });
  clock.advance(41250);                             // sends another heartbeat
  assert.equal(harness.instances.length, 1);        // still on the original socket
  assert.equal(gw.getState(), 'READY');
});

test('Op 1 server-requested heartbeat fires immediately', () => {
  const { gw, harness } = setupGateway();
  gw.start();
  helloAndReady(gw, harness);
  harness.last.sent = [];
  harness.last._recv({ op: 1 });
  const hb = JSON.parse(harness.last.sent[0]);
  assert.equal(hb.op, 1);
});

test('Op 7 RECONNECT closes and reconnects with RESUME', () => {
  const { gw, harness, clock } = setupGateway();
  gw.start();
  helloAndReady(gw, harness);
  const sid = 'SID';
  harness.last._recv({ op: 7 });
  clock.advance(1000);                              // first backoff = 1s
  assert.equal(harness.instances.length, 2);
  assert.match(harness.last.url, /resume\.gateway\.discord\.gg/);
  harness.last._open();
  harness.last._recv({ op: 10, d: { heartbeat_interval: 41250 } });
  const resume = JSON.parse(harness.last.sent[0]);
  assert.equal(resume.op, 6);
  assert.equal(resume.d.session_id, sid);
  assert.equal(resume.d.seq, 1);
});

test('Op 9 INVALID_SESSION (resumable=false) clears session and re-IDENTIFIES', () => {
  const { gw, harness, clock } = setupGateway();
  gw.start();
  helloAndReady(gw, harness);
  harness.last._recv({ op: 9, d: false });
  // INVALID_SESSION waits 1-5s before reconnecting
  clock.advance(6000);
  assert.equal(harness.instances.length, 2);
  // Reconnect uses default URL since session was cleared (resumable=false)
  assert.match(harness.last.url, /^wss:\/\/gateway\.discord\.gg/);
  harness.last._open();
  harness.last._recv({ op: 10, d: { heartbeat_interval: 41250 } });
  const sent = JSON.parse(harness.last.sent[0]);
  assert.equal(sent.op, 2);                         // IDENTIFY, not RESUME
});

test('Fatal close code 4004 stops reconnect attempts', () => {
  const { gw, harness, clock } = setupGateway();
  gw.start();
  helloAndReady(gw, harness);
  harness.last._close(4004);
  clock.advance(60000);
  assert.equal(harness.instances.length, 1);        // never reconnected
  assert.equal(gw.getState(), 'DISCONNECTED');
});

test('Non-fatal close triggers reconnect on the backoff schedule', () => {
  const { gw, harness, clock } = setupGateway();
  gw.start();
  helloAndReady(gw, harness);
  harness.last._close(1006);                        // abnormal closure
  clock.advance(1000);
  assert.equal(harness.instances.length, 2);
});

test('stop() halts pending reconnects', () => {
  const { gw, harness, clock } = setupGateway();
  gw.start();
  harness.last._open();
  harness.last._close(1006);
  gw.stop();
  clock.advance(60000);
  assert.equal(harness.instances.length, 1);
  assert.equal(gw.getState(), 'DISCONNECTED');
});

test('READY_SUPPLEMENTAL merges additional read_state entries', () => {
  const { gw, harness } = setupGateway();
  gw.start();
  helloAndReady(gw, harness, { entries: [] });
  harness.last._recv({ op: 0, t: 'READY_SUPPLEMENTAL', s: 2,
    d: { read_state: { entries: [{ id: '777', last_message_id: '50', mention_count: 0 }] } } });
  assert.deepEqual(gw.getReadState('777'), { lastReadId: '50', mentionCount: 0 });
});

test('markRead optimistically updates the map and clears mention_count', () => {
  const { gw, harness } = setupGateway();
  gw.start();
  helloAndReady(gw, harness);
  gw.markRead('222', '850');
  assert.deepEqual(gw.getReadState('222'), { lastReadId: '850', mentionCount: 0 });
  gw.markRead('222', '700');                        // older id is a no-op for lastReadId
  assert.equal(gw.getReadState('222').lastReadId, '850');
});

test('no token: start() does not open a socket', () => {
  const { gw, harness } = setupGateway({ getToken: () => '' });
  gw.start();
  assert.equal(harness.instances.length, 0);
  assert.equal(gw.getState(), 'DISCONNECTED');
});
