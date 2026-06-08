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

// ---- crash-localization breadcrumbs ----
#define PK_DBG_STAGE 250
static int s_dbg_prev = -1;
int wc_dbg_begin(void) {
  s_dbg_prev = persist_exists(PK_DBG_STAGE) ? persist_read_int(PK_DBG_STAGE) : 0;
  persist_write_int(PK_DBG_STAGE, 0);
  return s_dbg_prev;
}
void wc_dbg_stage(int n) { persist_write_int(PK_DBG_STAGE, n); }
int wc_dbg_prev(void) { return s_dbg_prev; }

// ---- rendering helpers ----
GColor wc_hex_to_color(const char *s) {
  uint32_t v = 0;
  if (s && s[0]) v = (uint32_t)strtol(s, NULL, 16);
  return GColorFromHEX(v);
}
// Bytes in the UTF-8 code point that starts with lead byte c (1 for a
// continuation/invalid lead, so callers always make forward progress).
static int wc_utf8_cp_len(unsigned char c) {
  if (c < 0x80)         return 1;   // 0xxxxxxx
  if ((c >> 5) == 0x6)  return 2;   // 110xxxxx
  if ((c >> 4) == 0xE)  return 3;   // 1110xxxx
  if ((c >> 3) == 0x1E) return 4;   // 11110xxx
  return 1;                         // continuation byte or invalid lead
}

void wc_utf8_copy(char *dst, const char *src, size_t cap) {
  if (!cap) return;
  if (!src) { dst[0] = '\0'; return; }
  size_t max = cap - 1;
  size_t n = 0;
  while (src[n] && n < max) n++;
  if (src[n]) {                                  // truncated mid-string
    size_t i = n;                                // find the lead byte of the last code point
    while (i > 0 && ((unsigned char)src[i - 1] & 0xC0) == 0x80) i--;
    if (i > 0) {
      size_t lead = i - 1;
      int len = wc_utf8_cp_len((unsigned char)src[lead]);
      if (lead + (size_t)len > n) n = lead;      // last code point is incomplete -> drop it
    }
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

void wc_make_initials(const char *name, char *out, size_t cap) {  // out should hold >=5 bytes
  if (cap) out[0] = '\0';
  if (!name || !name[0] || cap < 2) return;
  unsigned char c0 = (unsigned char)name[0];
  if (c0 >= 0x80) {
    // Leading multibyte code point (emoji/flag): copy it whole so initials stay
    // valid UTF-8 (a lone lead byte crashes graphics_draw_text on hardware).
    int len = wc_utf8_cp_len(c0);
    int o = 0;
    for (int i = 0; i < len && name[i] && (size_t)(o + 1) < cap; i++) out[o++] = name[i];
    out[o] = '\0';
    return;
  }
  // ASCII: first letter + first letter after a space (both uppercased).
  int o = 0;
  char a = name[0]; if (a >= 'a' && a <= 'z') a -= 32;
  if ((size_t)(o + 1) < cap) out[o++] = a;
  for (const char *p = name; *p && o < 2; p++) {
    if (*p == ' ' && *(p + 1)) {
      unsigned char b = (unsigned char)*(p + 1);
      if (b < 0x80) {                            // only take an ASCII second initial
        char bc = *(p + 1); if (bc >= 'a' && bc <= 'z') bc -= 32;
        if ((size_t)(o + 1) < cap) out[o++] = bc;
      }
      break;
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
