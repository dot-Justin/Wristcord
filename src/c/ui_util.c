// src/c/ui_util.c
#include "ui_util.h"
#include "settings.h"

// ---- CSV collapse-set helpers ----
bool wc_csv_contains(const char *csv, const char *id) {
  size_t idlen = strlen(id);
  const char *p = csv;
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t seg = comma ? (size_t)(comma - p) : strlen(p);
    if (seg == idlen && strncmp(p, id, idlen) == 0) return true;
    if (!comma) break;
    p = comma + 1;
  }
  return false;
}
void wc_csv_add(char *csv, size_t cap, const char *id) {
  if (wc_csv_contains(csv, id)) return;
  size_t len = strlen(csv);
  if (len + (len ? 1 : 0) + strlen(id) + 1 > cap) return;
  if (len) strcat(csv, ",");
  strcat(csv, id);
}
void wc_csv_remove(char *csv, const char *id) {
  size_t idlen = strlen(id);
  char out[256]; out[0] = '\0';
  const char *p = csv;
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t seg = comma ? (size_t)(comma - p) : strlen(p);
    bool match = (seg == idlen && strncmp(p, id, idlen) == 0);
    if (!match && seg > 0) {
      if (out[0]) strncat(out, ",", sizeof(out) - strlen(out) - 1);
      strncat(out, p, seg);
    }
    if (!comma) break;
    p = comma + 1;
  }
  strcpy(csv, out);
}

// ---- rendering helpers ----
GColor wc_hex_to_color(const char *s) {
  uint32_t v = 0;
  if (s && s[0]) v = (uint32_t)strtol(s, NULL, 16);
  return GColorFromHEX(v);
}
void wc_make_initials(const char *name, char *out) {  // out[3]
  out[0] = out[1] = out[2] = '\0';
  if (!name) return;
  int o = 0;
  char c0 = name[0];
  if (c0 >= 'a' && c0 <= 'z') c0 -= 32;
  if (c0) out[o++] = c0;
  for (const char *p = name; *p && o < 2; p++) {
    if (*p == ' ' && *(p + 1)) {
      char c = *(p + 1);
      if (c >= 'a' && c <= 'z') c -= 32;
      out[o++] = c; break;
    }
  }
  out[o] = '\0';
}
void wc_draw_dot(GContext *ctx, GPoint center, int radius, GColor color, const char *initials) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, center, radius);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, initials, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(center.x - radius, center.y - 9, radius * 2, 18), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}
TextLayer *wc_titlebar_create(Layer *root, GRect bounds, const char *title, WristcordSettings *s) {
  TextLayer *t = text_layer_create(GRect(0, 0, bounds.size.w, STATUS_BAR_LAYER_HEIGHT));
  text_layer_set_background_color(t, GColorBlack);
  text_layer_set_text_color(t, s->accent);
  text_layer_set_font(t, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(t, GTextAlignmentCenter);
  text_layer_set_overflow_mode(t, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text(t, title);
  layer_add_child(root, text_layer_get_layer(t));
  return t;
}

void wc_draw_chevron(GContext *ctx, GRect box, bool expanded, GColor color) {
  // Drawn triangle (font-independent; ▸/▾ glyphs aren't in the system font).
  int cx = box.origin.x + box.size.w / 2;
  int cy = box.origin.y + box.size.h / 2;
  GPoint pts[3];
  if (expanded) {                       // ▼ points down
    pts[0] = GPoint(cx - 4, cy - 2); pts[1] = GPoint(cx + 4, cy - 2); pts[2] = GPoint(cx, cy + 3);
  } else {                              // ▶ points right
    pts[0] = GPoint(cx - 2, cy - 4); pts[1] = GPoint(cx - 2, cy + 4); pts[2] = GPoint(cx + 3, cy);
  }
  GPathInfo info = { .num_points = 3, .points = pts };
  GPath *path = gpath_create(&info);
  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}
