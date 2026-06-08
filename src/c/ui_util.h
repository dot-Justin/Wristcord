// src/c/ui_util.h
#pragma once
#include <pebble.h>
#include "settings.h"

bool   wc_csv_contains(const char *csv, const char *id);
void   wc_csv_add(char *csv, size_t cap, const char *id);
void   wc_csv_remove(char *csv, const char *id);
GColor wc_hex_to_color(const char *s);
void   wc_make_initials(const char *name, char *out);   // out must hold >=3 bytes
void   wc_draw_dot(GContext *ctx, GPoint center, int radius, GColor color, const char *initials);
void   wc_draw_chevron(GContext *ctx, GRect box, bool expanded, GColor color);

// Create a 20px themed title bar (surface bg + bold title) at the top of `root`.
// Returns the TextLayer; caller destroys it on unload. `title` must remain valid
// for the lifetime of the layer (static/const string or persistent static buffer).
TextLayer *wc_titlebar_create(Layer *root, GRect bounds, const char *title, WristcordSettings *s);
