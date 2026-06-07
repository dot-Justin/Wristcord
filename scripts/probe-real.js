// scripts/probe-real.js
// Read-only validation of the M2 data layer against the REAL Discord account.
// Token is read from .secrets/discord_token (gitignored) and NEVER printed.
// Usage: node scripts/probe-real.js [guildNameSubstring]
'use strict';

var fs = require('fs');
var path = require('path');
var https = require('https');
var { makeClient } = require('../src/pkjs/lib/discord');
var { buildServerList, buildChannelTree, packMessages } = require('../src/pkjs/lib/model');

var TOKEN_PATH = path.join(__dirname, '..', '.secrets', 'discord_token');
var token;
try { token = fs.readFileSync(TOKEN_PATH, 'utf8').trim(); }
catch (e) { console.error('No token at .secrets/discord_token'); process.exit(1); }
if (!token) { console.error('Empty token file'); process.exit(1); }

function makeRequest(tok) {
  return function (method, p, body) {
    return new Promise(function (resolve, reject) {
      var data = body ? JSON.stringify(body) : null;
      var req = https.request({
        host: 'discord.com', path: '/api/v10' + p, method: method,
        headers: { 'Authorization': tok, 'User-Agent': 'Mozilla/5.0', 'Content-Type': 'application/json' }
      }, function (res) {
        var d = ''; res.on('data', function (c) { d += c; });
        res.on('end', function () { var j = null; try { j = JSON.parse(d); } catch (e) {} resolve({ status: res.statusCode, json: j }); });
      });
      req.on('error', reject);
      if (data) req.write(data);
      req.end();
    });
  };
}

function indent(row) { return row.parentIndex === '' ? '' : '    '; }

(async function () {
  var c = makeClient(makeRequest(token));
  var guildsRes = await c.guilds();
  if (guildsRes.status !== 200) { console.error('guilds HTTP', guildsRes.status); process.exit(1); }
  var settingsRes = await c.userSettings();
  var servers = buildServerList(guildsRes.json, settingsRes.json || {});

  console.log('=== SERVER LIST (' + servers.length + ' rows) — first 18 ===');
  servers.slice(0, 18).forEach(function (r) {
    var tag = r.kind === 'f' ? '[folder]' : '       •';
    var extra = r.kind === 'f' ? ('  grid=' + r.memberColors.length + ' color=' + r.color) : ('  ' + r.color);
    console.log(indent(r) + tag + ' ' + r.name + extra);
  });

  var sub = (process.argv[2] || 'rebble').toLowerCase();
  var target = guildsRes.json.find(function (g) { return g.name.toLowerCase().indexOf(sub) >= 0; }) || guildsRes.json[0];
  var chRes = await c.channels(target.id);
  console.log('\n=== CHANNELS for "' + target.name + '" (HTTP ' + chRes.status + ') ===');
  if (chRes.status === 200) {
    var tree = buildChannelTree(chRes.json);
    console.log('(' + tree.length + ' rows) — first 20:');
    tree.slice(0, 20).forEach(function (r) {
      console.log(indent(r) + (r.kind === 'c' ? '[cat] ' + r.name : '   # ' + r.name));
    });

    var firstText = tree.find(function (r) { return r.kind === 't'; });
    if (firstText) {
      var msgRes = await c.messages(firstText.id, 8);
      console.log('\n=== MESSAGES in #' + firstText.name + ' (HTTP ' + msgRes.status + ') — oldest->newest ===');
      if (msgRes.status === 200) {
        packMessages(msgRes.json).forEach(function (m) {
          console.log('  ' + m.time + '  ' + m.author + ': ' + m.text);
        });
      } else {
        console.log('  (could not read messages: HTTP ' + msgRes.status + ')');
      }
    }
  }
  console.log('\nDONE (read-only; no messages sent; token not printed).');
})().catch(function (e) { console.error('probe error:', e.message); process.exit(1); });
