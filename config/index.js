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
  { "type": "slider", "id": "g_dm_count", "messageKey": "dmCount",
    "label": "DMs on home page", "defaultValue": 3, "min": 3, "max": 20, "step": 1, "description": "How many recent DMs to preview before the 'Show all' row." },
  { "type": "slider", "id": "g_server_count", "messageKey": "serverCount",
    "label": "Servers on home page", "defaultValue": 3, "min": 3, "max": 20, "step": 1, "description": "How many servers to preview before the 'Show all' row." },
  { "type": "select", "id": "g_sort_mode", "messageKey": "sortMode",
    "label": "Sort servers + DMs", "defaultValue": "mostUsed",
    "description": "How the home-page previews are ordered. 'Most used' tracks how often you open each server/DM on this watch.",
    "options": [
      { "label": "Most used (recommended)", "value": "mostUsed" },
      { "label": "Discord order",           "value": "discordOrder" },
      { "label": "Alphabetical",            "value": "alphabetical" },
      { "label": "Most recent activity",    "value": "recentActivity" }
    ]
  },
  { "type": "submit", "defaultValue": "Save" }
];
