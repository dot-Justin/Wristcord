// config/custom-clay.js
module.exports = function(minified) {
  var clayConfig = this;
  function toggleGated() {
    var consent = clayConfig.getItemByMessageKey('CONSENT').get();
    var gated = clayConfig.getItemById('gated');
    if (consent) { gated.show(); } else { gated.hide(); }
  }
  clayConfig.on(clayConfig.EVENTS.AFTER_RENDER, function() {
    toggleGated();
    clayConfig.getItemByMessageKey('CONSENT').on('change', toggleGated);
  });
};
