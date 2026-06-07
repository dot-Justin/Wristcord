// config/index.js
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
  {
    "type": "section",
    "id": "gated",
    "items": [
      { "type": "heading", "defaultValue": "Account" },
      { "type": "input", "messageKey": "token", "label": "Discord token", "attributes": { "type": "password", "placeholder": "paste token" } },
      { "type": "heading", "defaultValue": "Appearance" },
      { "type": "select", "messageKey": "theme", "label": "Theme", "defaultValue": "midnight",
        "options": [ { "label": "Midnight", "value": "midnight" }, { "label": "Dark", "value": "dark" }, { "label": "Light", "value": "light" } ] },
      { "type": "color", "messageKey": "accent", "label": "Accent color", "defaultValue": "0x5555FF", "sunlight": false },
      { "type": "select", "messageKey": "pollSeconds", "label": "Refresh interval", "defaultValue": "10",
        "options": [ { "label": "Off", "value": "off" }, { "label": "5 seconds", "value": "5" }, { "label": "10 seconds", "value": "10" }, { "label": "30 seconds", "value": "30" } ] }
    ]
  },
  { "type": "submit", "defaultValue": "Save" }
];
