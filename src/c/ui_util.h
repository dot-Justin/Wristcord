// src/c/ui_util.h
#pragma once
#include <pebble.h>
#include "settings.h"

bool   wc_csv_contains(const char *csv, const char *id);
void   wc_csv_add(char *csv, size_t cap, const char *id);
void   wc_csv_remove(char *csv, const char *id);
GColor wc_hex_to_color(const char *s);
int    wc_atoi(const char *s);   // libc-free atoi (firmware atoi faults on new PebbleOS)

// UTF-8-safe bounded copy: copies src into dst (capacity `cap` incl. NUL) and
// NEVER leaves a partial trailing multibyte sequence. graphics_draw_text
// hard-faults on malformed UTF-8 on real hardware (QEMU tolerates it), so every
// Discord-supplied string that gets truncated then drawn MUST go through this.
void   wc_utf8_copy(char *dst, const char *src, size_t cap);

void   wc_make_initials(const char *name, char *out, size_t cap);   // out should hold >=5 bytes
void   wc_draw_dot(GContext *ctx, GPoint center, int radius, GColor color, const char *initials);
void   wc_draw_chevron(GContext *ctx, GRect box, bool expanded, GColor color);

// Channel-row unread indicator. Renders into `box` (right column):
//   - mention_count > 0: red filled circle + white centered number ("3", "12", "99+")
//   - else if unread:    small white filled dot
//   - else:              nothing
// `selected` flips the dot to the highlight foreground so it stays visible on
// the accent-highlighted row.
void   wc_draw_unread_indicator(GContext *ctx, GRect box, bool unread,
                                int mention_count, bool selected,
                                WristcordSettings *settings);

// Create a 20px themed title bar (surface bg + bold title) at the top of `root`.
// Returns the TextLayer; caller destroys it on unload. `title` must remain valid
// for the lifetime of the layer (static/const string or persistent static buffer).
TextLayer *wc_titlebar_create(Layer *root, GRect bounds, const char *title, WristcordSettings *s);
