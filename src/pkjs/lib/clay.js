// src/pkjs/lib/clay.js
// Unwraps Clay's getSettings(response, false) shape, where each item is {value: ...}.
function pick(dict, k) {
  return (dict[k] && typeof dict[k] === 'object') ? dict[k].value : dict[k];
}
// IMPORTANT: every field that appears in config/index.js as a `messageKey` must
// be listed here; otherwise the user's Clay save silently drops it and
// normalize() resets it to default. Easy to forget when adding a new Clay
// field — re-check this list AND test/clay-unwrap.test.js when you touch the
// form.
function unwrap(dict) {
  dict = dict || {};
  return {
    token:        pick(dict, 'token'),
    theme:        pick(dict, 'theme'),
    accent:       pick(dict, 'accent'),
    pollSeconds:  pick(dict, 'pollSeconds'),
    dmCount:      pick(dict, 'dmCount'),
    serverCount:  pick(dict, 'serverCount'),
    sortMode:     pick(dict, 'sortMode')
  };
}
module.exports = { unwrap: unwrap, pick: pick };
