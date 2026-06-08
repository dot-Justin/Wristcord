// config/index.js — Clay configuration page
// NOTE: the consent gate hides the gated ITEMS by id (Clay sections are NOT
// retrievable via getItemById, so we don't wrap them in a section). See custom-clay.js.
module.exports = [
  { "type": "heading", "defaultValue": "Wristcord" },
  {
    "type": "text",
    "defaultValue":
      "Wristcord signs in with your personal Discord account token (selfbot). " +
      "Automating a user account is AGAINST Discord's Terms of Service and CAN GET " +
      "YOUR ACCOUNT BANNED. You use it entirely at your own risk; the author is not " +
      "responsible for anything that happens to your account or data."
  },
  {
    "type": "toggle",
    "messageKey": "CONSENT",
    "label": "I understand and accept the risk",
    "defaultValue": false
  },
  { "type": "input", "id": "g_token", "messageKey": "token", "label": "Discord token",
    "attributes": { "type": "password", "placeholder": "paste token" } },
  { "type": "select", "id": "g_theme", "messageKey": "theme", "label": "Theme", "defaultValue": "midnight",
    "options": [ { "label": "Midnight", "value": "midnight" }, { "label": "Dark", "value": "dark" }, { "label": "Light", "value": "light" } ] },
  { "type": "color", "id": "g_accent", "messageKey": "accent", "label": "Accent color", "defaultValue": "0x5555FF", "sunlight": false },
  { "type": "select", "id": "g_poll", "messageKey": "pollSeconds", "label": "Refresh interval", "defaultValue": "10",
    "options": [ { "label": "Off", "value": "off" }, { "label": "5 seconds", "value": "5" }, { "label": "10 seconds", "value": "10" }, { "label": "30 seconds", "value": "30" } ] },
  { "type": "submit", "defaultValue": "Save" }
];
