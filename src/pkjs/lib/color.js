// src/pkjs/lib/color.js
// Curated subset of the Pebble 64-color palette that reads well as a server dot
// on the Midnight theme (avoids near-black / near-white that vanish on dark bg).
var PALETTE = [
  '0x5555FF', '0x00AAFF', '0x00AA55', '0x55AA00', '0xAAAA00',
  '0xFF5500', '0xFF0055', '0xAA00FF', '0xFF55AA', '0x00AAAA',
  '0xFFAA00', '0x55AAFF'
];

function nameToAccentHex(name) {
  var s = String(name || 'wristcord');
  var h = 5381;
  for (var i = 0; i < s.length; i++) {
    h = ((h << 5) + h + s.charCodeAt(i)) >>> 0; // djb2, unsigned
  }
  return PALETTE[h % PALETTE.length];
}

module.exports = { nameToAccentHex: nameToAccentHex, PALETTE: PALETTE };
