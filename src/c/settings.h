// src/c/settings.h
#pragma once
#include <pebble.h>

typedef enum { THEME_DARK = 0, THEME_LIGHT = 1, THEME_MIDNIGHT = 2 } WcTheme;

typedef struct {
  WcTheme  theme;
  uint32_t accent_hex;   // raw 0xRRGGBB; persisted so it round-trips exactly
  GColor   accent;       // derived from accent_hex via GColorFromHEX
  int32_t  poll_seconds; // 0 = off
  bool     has_token;    // whether a token is configured on the phone
} WristcordSettings;

void wc_settings_load(WristcordSettings *out);                 // from persist, with defaults
bool wc_settings_apply_from_msg(DictionaryIterator *it,        // from pkjs AppMessage; true if any setting was present
                                WristcordSettings *s);
void wc_settings_save(const WristcordSettings *s);             // to persist
GColor wc_theme_bg(const WristcordSettings *s);                // base background per theme
GColor wc_theme_fg(const WristcordSettings *s);                // base text per theme
GColor wc_theme_muted(const WristcordSettings *s);             // dim text, legible on each theme bg
