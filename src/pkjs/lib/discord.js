// src/pkjs/lib/discord.js
// Thin Discord REST path builder. `request(method, path, body)` returns
// Promise<{status, json}>; the adapter attaches the auth token. No IO here.
'use strict';

function makeClient(request) {
  return {
    me: function () { return request('GET', '/users/@me'); },
    guilds: function () { return request('GET', '/users/@me/guilds'); },
    userSettings: function () { return request('GET', '/users/@me/settings'); },
    channels: function (guildId) { return request('GET', '/guilds/' + guildId + '/channels'); },
    messages: function (channelId, limit) {
      return request('GET', '/channels/' + channelId + '/messages?limit=' + (limit || 20));
    },
    sendMessage: function (channelId, content) {
      return request('POST', '/channels/' + channelId + '/messages', { content: content });
    }
  };
}

module.exports = { makeClient: makeClient };
