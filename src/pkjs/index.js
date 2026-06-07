// src/pkjs/index.js
var Clay = require('pebble-clay');
var clayConfig = require('../../config/index');
var customClay = require('../../config/custom-clay');
var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false });

var settingsLib = require('./lib/settings');

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
  var dict = clay.getSettings(e.response, false); // raw values keyed by messageKey
  var merged = settingsLib.normalize({
    token: dict.token, theme: dict.theme, accent: dict.accent, pollSeconds: dict.pollSeconds
  });
  saveSettings(merged);
  pushToWatch(merged);
});

// (M2+ will add the 'appmessage' OP router here.)
