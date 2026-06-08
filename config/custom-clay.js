// config/custom-clay.js
// Consent gate: hide the gated items until the CONSENT toggle is on.
// IMPORTANT: Clay 1.0.4 events are BEFORE_BUILD/AFTER_BUILD/BEFORE_DESTROY/AFTER_DESTROY
// (there is NO AFTER_RENDER — using it makes .on() throw and the whole form fails to
// build, i.e. a BLANK config page). Gate individual ITEMS by id (sections aren't
// retrievable via getItemById). The whole thing is wrapped defensively so a config-page
// error can never blank the form.
module.exports = function (minified) {
  var clayConfig = this;
  try {
    var GATED = ['g_token', 'g_theme', 'g_accent', 'g_poll'];
    var apply = function () {
      try {
        var consent = clayConfig.getItemByMessageKey('CONSENT');
        var on = consent && consent.get();
        GATED.forEach(function (id) {
          var item = clayConfig.getItemById(id);
          if (item) { if (on) { item.show(); } else { item.hide(); } }
        });
      } catch (e) {}
    };
    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function () {
      apply();
      var consent = clayConfig.getItemByMessageKey('CONSENT');
      if (consent) consent.on('change', apply);
    });
  } catch (e) {}
};
