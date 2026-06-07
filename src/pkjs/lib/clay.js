// src/pkjs/lib/clay.js
// Unwraps Clay's getSettings(response, false) shape, where each item is {value: ...}.
function pick(dict, k) {
  return (dict[k] && typeof dict[k] === 'object') ? dict[k].value : dict[k];
}
function unwrap(dict) {
  dict = dict || {};
  return { token: pick(dict, 'token'), theme: pick(dict, 'theme'), accent: pick(dict, 'accent'), pollSeconds: pick(dict, 'pollSeconds') };
}
module.exports = { unwrap: unwrap, pick: pick };
