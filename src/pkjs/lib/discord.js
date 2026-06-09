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
    // 1:1 and group DM channels. Each entry has type, recipients[] (with full
    // user objects inline), last_message_id, name (for group DMs), etc.
    dmChannels: function () { return request('GET', '/users/@me/channels'); },
    messages: function (channelId, limit) {
      return request('GET', '/channels/' + channelId + '/messages?limit=' + (limit || 20));
    },
    messagesBefore: function (channelId, beforeId, limit) {
      return request('GET', '/channels/' + channelId + '/messages?limit=' + (limit || 30) +
                            '&before=' + beforeId);
    },
    sendMessage: function (channelId, content) {
      return request('POST', '/channels/' + channelId + '/messages', { content: content });
    },
    ack: function (channelId, messageId) {
      // Discord disabled this for bot tokens but it still works for user (selfbot) tokens.
      // body {manual:true} matches the official client's request shape.
      return request('POST', '/channels/' + channelId + '/messages/' + messageId + '/ack',
        { manual: true });
    }
  };
}

module.exports = { makeClient: makeClient };
