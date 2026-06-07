const test = require('node:test');
const assert = require('node:assert');
const { unwrap } = require('../src/pkjs/lib/clay');
const { normalize, watchSubset } = require('../src/pkjs/lib/settings');

test('unwraps Clay {value:...} response into a normalizable object', () => {
  const clayResponse = { token: {value:'abc.def'}, theme: {value:'light'}, accent: {value:0x00AA55}, pollSeconds: {value:'30'} };
  const s = normalize(unwrap(clayResponse));
  assert.strictEqual(s.token, 'abc.def');
  assert.strictEqual(s.theme, 'light');
  assert.strictEqual(s.accent, '0x00AA55');
  assert.strictEqual(s.pollSeconds, 30);
  const sub = watchSubset(s);
  assert.strictEqual(sub.HAS_TOKEN, 1);          // the bug made this 0
  assert.strictEqual(sub.SET_ACCENT, 0x00AA55);
  assert.strictEqual('token' in sub, false);     // still never sent to watch
});

test('handles already-unwrapped primitives too', () => {
  const s = normalize(unwrap({ token:'x', theme:'dark', accent:'0x5555FF', pollSeconds:'off' }));
  assert.strictEqual(s.token, 'x');
  assert.strictEqual(s.theme, 'dark');
  assert.strictEqual(s.pollSeconds, 0);
});

test('empty response yields safe defaults', () => {
  const s = normalize(unwrap({}));
  assert.strictEqual(s.token, '');
  assert.strictEqual(s.theme, 'midnight');
});
