// test/discord.test.js
const test = require('node:test');
const assert = require('node:assert');
const { makeClient } = require('../src/pkjs/lib/discord');

function mock() {
  const calls = [];
  const request = (method, path, body) => {
    calls.push({ method, path, body });
    return Promise.resolve({ status: 200, json: { ok: true } });
  };
  return { calls, request };
}

test('me() calls GET /users/@me', async () => {
  const m = mock();
  const c = makeClient(m.request);
  const r = await c.me();
  assert.deepStrictEqual(m.calls[0], { method: 'GET', path: '/users/@me', body: undefined });
  assert.deepStrictEqual(r, { status: 200, json: { ok: true } });
});

test('guilds() calls GET /users/@me/guilds', async () => {
  const m = mock();
  const c = makeClient(m.request);
  const r = await c.guilds();
  assert.deepStrictEqual(m.calls[0], { method: 'GET', path: '/users/@me/guilds', body: undefined });
  assert.deepStrictEqual(r, { status: 200, json: { ok: true } });
});

test('userSettings() calls GET /users/@me/settings', async () => {
  const m = mock();
  const c = makeClient(m.request);
  const r = await c.userSettings();
  assert.deepStrictEqual(m.calls[0], { method: 'GET', path: '/users/@me/settings', body: undefined });
  assert.deepStrictEqual(r, { status: 200, json: { ok: true } });
});

test('channels(guildId) calls GET /guilds/{guildId}/channels', async () => {
  const m = mock();
  const c = makeClient(m.request);
  const r = await c.channels('123');
  assert.deepStrictEqual(m.calls[0], { method: 'GET', path: '/guilds/123/channels', body: undefined });
  assert.deepStrictEqual(r, { status: 200, json: { ok: true } });
});

test('messages(channelId) calls GET /channels/{channelId}/messages?limit=20 (default)', async () => {
  const m = mock();
  const c = makeClient(m.request);
  const r = await c.messages('456');
  assert.deepStrictEqual(m.calls[0], { method: 'GET', path: '/channels/456/messages?limit=20', body: undefined });
  assert.deepStrictEqual(r, { status: 200, json: { ok: true } });
});

test('messages(channelId, limit) calls GET with custom limit', async () => {
  const m = mock();
  const c = makeClient(m.request);
  const r = await c.messages('456', 5);
  assert.deepStrictEqual(m.calls[0], { method: 'GET', path: '/channels/456/messages?limit=5', body: undefined });
  assert.deepStrictEqual(r, { status: 200, json: { ok: true } });
});

test('sendMessage(channelId, content) calls POST /channels/{channelId}/messages with body', async () => {
  const m = mock();
  const c = makeClient(m.request);
  const r = await c.sendMessage('789', 'hi');
  assert.deepStrictEqual(m.calls[0], { method: 'POST', path: '/channels/789/messages', body: { content: 'hi' } });
  assert.deepStrictEqual(r, { status: 200, json: { ok: true } });
});

test('ack(channelId, messageId) calls POST /channels/{cid}/messages/{mid}/ack with {manual:true}', async () => {
  const m = mock();
  const c = makeClient(m.request);
  const r = await c.ack('cid', 'mid');
  assert.deepStrictEqual(m.calls[0], { method: 'POST', path: '/channels/cid/messages/mid/ack', body: { manual: true } });
  assert.deepStrictEqual(r, { status: 200, json: { ok: true } });
});

test('all methods return the promise from request', async () => {
  const m = mock();
  const c = makeClient(m.request);

  const resp1 = await c.me();
  assert.strictEqual(resp1.status, 200);
  assert.deepStrictEqual(resp1.json, { ok: true });

  const resp2 = await c.guilds();
  assert.strictEqual(resp2.status, 200);
  assert.deepStrictEqual(resp2.json, { ok: true });
});
