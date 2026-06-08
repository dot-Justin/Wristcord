// src/c/settings.c
#include "settings.h"

#define PK_THEME  100
#define PK_ACCENT 101   // stores the raw 0xRRGGBB hex int
#define PK_POLL   102
#define PK_HASTOK 103

#define DEFAULT_ACCENT_HEX 0x5555FF

void wc_settings_load(WristcordSettings *out) {
  out->theme        = persist_exists(PK_THEME)  ? (WcTheme)persist_read_int(PK_THEME) : THEME_MIDNIGHT;
  out->accent_hex   = persist_exists(PK_ACCENT) ? (uint32_t)persist_read_int(PK_ACCENT) : DEFAULT_ACCENT_HEX;
  out->accent       = GColorFromHEX(out->accent_hex);
  out->poll_seconds = persist_exists(PK_POLL)   ? persist_read_int(PK_POLL) : 10;
  out->has_token    = persist_exists(PK_HASTOK) ? (bool)persist_read_int(PK_HASTOK) : false;
}

void wc_settings_save(const WristcordSettings *s) {
  persist_write_int(PK_THEME, s->theme);
  persist_write_int(PK_ACCENT, (int32_t)s->accent_hex);
  persist_write_int(PK_POLL, s->poll_seconds);
  persist_write_int(PK_HASTOK, s->has_token ? 1 : 0);
}

bool wc_settings_apply_from_msg(DictionaryIterator *it, WristcordSettings *s) {
  Tuple *t;
  bool changed = false;
  if ((t = dict_find(it, MESSAGE_KEY_SET_THEME)))  { s->theme = (WcTheme)t->value->int32; changed = true; }
  if ((t = dict_find(it, MESSAGE_KEY_SET_ACCENT))) {
    s->accent_hex = (uint32_t)t->value->int32;
    s->accent = GColorFromHEX(s->accent_hex);
    changed = true;
  }
  if ((t = dict_find(it, MESSAGE_KEY_SET_POLL)))   { s->poll_seconds = t->value->int32; changed = true; }
  if ((t = dict_find(it, MESSAGE_KEY_HAS_TOKEN)))  { s->has_token = (t->value->int32 != 0); changed = true; }
  return changed;
}

GColor wc_theme_bg(const WristcordSettings *s) {
  switch (s->theme) {
    case THEME_LIGHT: return GColorWhite;
    case THEME_DARK:  return GColorBlack;
    default:          return GColorFromHEX(0x0B0D1A); // midnight base (quantizes near-black; refined in M7)
  }
}
GColor wc_theme_fg(const WristcordSettings *s) {
  return s->theme == THEME_LIGHT ? GColorBlack : GColorWhite;
}
GColor wc_theme_muted(const WristcordSettings *s) {
  // Dark/Midnight: light-gray on near-black is readable; Light: dark-gray on white is readable
  return s->theme == THEME_LIGHT ? GColorDarkGray : GColorLightGray;
}
