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
// Hand-rolled hex parse — avoids the firmware-provided strtol(), which faults on
// the new Core Devices PebbleOS (QEMU's strtol works, hence emulator-only success).
// Parses an optional 0x prefix then hex digits, stopping at the first non-hex char
// (so it also reads one CSV segment of memberColors without a separate split).
GColor wc_hex_to_color(const char *s) {
  uint32_t v = 0;
  if (s) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (; *s; s++) {
      char c = *s; int d;
      if (c >= '0' && c <= '9') d = c - '0';
      else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
      else break;
      v = (v << 4) | (uint32_t)d;
    }
  }
  return GColorFromHEX(v);
}

// Hand-rolled atoi — same rationale (avoid firmware libc on the parse path).
int wc_atoi(const char *s) {
  int v = 0; bool neg = false;
  if (!s) return 0;
  while (*s == ' ') s++;
  if (*s == '-') { neg = true; s++; } else if (*s == '+') s++;
  for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
  return neg ? -v : v;
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

void wc_draw_unread_indicator(GContext *ctx, GRect box, bool unread,
                              int mention_count, bool selected,
                              WristcordSettings *settings) {
  if (mention_count <= 0 && !unread) return;
  int cx = box.origin.x + box.size.w / 2;
  int cy = box.origin.y + box.size.h / 2;
  if (mention_count > 0) {
    int r = 9;                                       // 18px diameter badge
    graphics_context_set_fill_color(ctx, GColorRed); // Discord-ish red, contrasts on every theme
    graphics_fill_circle(ctx, GPoint(cx, cy), r);
    char buf[5];
    if (mention_count > 99) {
      // "99+" doesn't fit in the 18px circle; clamp to "99". Discord caps the
      // visible badge similarly past 99 (with a "+" they have room for; we don't).
      buf[0] = '9'; buf[1] = '9'; buf[2] = '\0';
    } else if (mention_count > 9) {
      buf[0] = '0' + (mention_count / 10);
      buf[1] = '0' + (mention_count % 10);
      buf[2] = '\0';
    } else {
      buf[0] = '0' + mention_count; buf[1] = '\0';
    }
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
      GRect(cx - r, cy - 10, r * 2, 18),
      GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  } else {
    // Plain unread dot: small filled circle, theme-aware so it disappears
    // against the accent highlight on a selected row only when the accent is
    // bright (white-on-white). Use white on dark themes, accent_fg on selected.
    GColor color = selected ? GColorWhite : wc_theme_fg(settings);
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_circle(ctx, GPoint(cx, cy), 3);
  }
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
