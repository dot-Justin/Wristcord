// src/c/ui_util.h
#pragma once
#include <pebble.h>
#include "settings.h"

bool   wc_csv_contains(const char *csv, const char *id);
void   wc_csv_add(char *csv, size_t cap, const char *id);
void   wc_csv_remove(char *csv, const char *id);
GColor wc_hex_to_color(const char *s);

// UTF-8-safe bounded copy: copies src into dst (capacity `cap` incl. NUL) and
// NEVER leaves a partial trailing multibyte sequence. graphics_draw_text
// hard-faults on malformed UTF-8 on real hardware (QEMU tolerates it), so every
// Discord-supplied string that gets truncated then drawn MUST go through this.
void   wc_utf8_copy(char *dst, const char *src, size_t cap);

void   wc_make_initials(const char *name, char *out, size_t cap);   // out should hold >=5 bytes

// --- Crash-localization breadcrumbs (no device logs available) ---
// Persisted stage survives an app fault. Call wc_dbg_begin() once at startup: it
// captures the PREVIOUS run's furthest stage (shown on the Loading screen) then
// resets. wc_dbg_stage(n) records progress; the highest n reached before a crash
// is what the next launch reports.
int    wc_dbg_begin(void);     // returns previous run's last stage
void   wc_dbg_stage(int n);
int    wc_dbg_prev(void);
void   wc_draw_dot(GContext *ctx, GPoint center, int radius, GColor color, const char *initials);
void   wc_draw_chevron(GContext *ctx, GRect box, bool expanded, GColor color);

// Create a 20px themed title bar (surface bg + bold title) at the top of `root`.
// Returns the TextLayer; caller destroys it on unload. `title` must remain valid
// for the lifetime of the layer (static/const string or persistent static buffer).
TextLayer *wc_titlebar_create(Layer *root, GRect bounds, const char *title, WristcordSettings *s);
