// src/pkjs/lib/color.js
// Vibrant subset of the Pebble 64-color palette for server dots. Picked to be
// bright and saturated (no dark/dull entries) while keeping white initials legible,
// and to read well on both the dark and light themes.
var PALETTE = [
  '0xFF0000', // red
  '0xFF5500', // orange
  '0xFFAA00', // amber
  '0x00AA00', // green
  '0x00AA55', // jade
  '0x00AAAA', // teal
  '0x00AAFF', // sky
  '0x0055FF', // blue
  '0x5555FF', // blurple
  '0xAA00FF', // purple
  '0xFF00AA', // pink
  '0xFF0055', // rose
  '0xFF55AA', // light pink
  '0x55AAFF'  // light blue
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
