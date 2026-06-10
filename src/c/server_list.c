// src/c/server_list.c — v1.2 home page (Settings + DMs preview + Servers preview).
// The full server list (with folder support) lives in `all_servers.c`, pushed
// from the "Show all servers" row at the bottom of the Servers section. Same
// pattern for DMs via `all_dms.c`.
#include "server_list.h"
#include "rows.h"
#include "ui_util.h"
#include "channel_list.h"
#include "chat_view.h"
#include "all_dms.h"
#include "all_servers.h"
#include "tutorial.h"
#include "viewstats.h"

#define OP_HOME 7

// Row kinds (first field from OP_HOME):
//   S = Settings entry (single row, top of list)
//   H = section header (DMs or Servers)
//   D = DM row
//   M = "Show all" row (followed by section id 'dm' or 'server')
//   g = guild preview row
typedef enum {
  WC_HROW_SETTINGS = 0,
  WC_HROW_HEADER,
  WC_HROW_DM,
  WC_HROW_SHOWALL,
  WC_HROW_GUILD
} WcHomeKind;

typedef enum {
  WC_SEC_NONE = 0,
  WC_SEC_DM = 1,
  WC_SEC_SERVER = 2
} WcSection;

typedef struct {
  WcHomeKind kind;
  char id[20];
  char name[28];
  GColor color;
  int  ping_count;
  bool unread;
  WcSection section;        // for H and M rows
  // Sort criteria (guild rows only; ignored elsewhere)
  char mostrecent[20];      // last_message_id across all channels in the guild
  int  discord_pos;         // 0..n-1, Discord folder order
  int  use_count;           // local persist counter from viewstats
} HRow;

typedef enum { ST_LOADING, ST_READY, ST_EMPTY, ST_ERROR, ST_NOTOKEN } LoadState;

static Window *s_window;
static MenuLayer *s_menu;
static TextLayer *s_titlebar;
static WristcordSettings *s_settings;
static HRow s_rows[WC_MAX_ROWS];
static int s_count;
static LoadState s_state;
static int s_err_code;
// True until we've placed the cursor on the first DM row after the very first
// successful fetch of this window's lifetime. Refetches (window_appear silent
// reload, HOME_REFRESH from pkjs) must NOT yank selection back to the top.
static bool s_needs_initial_focus;

// Animated loading screen
static GBitmap  *s_logo;
static AppTimer *s_anim;
static int       s_anim_frame;
static void stop_loading_anim(void) { if (s_anim) { app_timer_cancel(s_anim); s_anim = NULL; } }
static void anim_cb(void *d) {
  (void)d; s_anim = NULL;
  if (s_state == ST_LOADING) {
    s_anim_frame++;
    if (s_menu) layer_mark_dirty(menu_layer_get_layer(s_menu));
    s_anim = app_timer_register(300, anim_cb, NULL);
  }
}
static void start_loading_anim(void) { s_anim_frame = 0; stop_loading_anim(); s_anim = app_timer_register(300, anim_cb, NULL); }

// Forward declarations so the action-menu callback (defined first) can refer
// to the sort+trim and refetch functions (defined later, near on_rows_done).
static void apply_server_sort_and_trim(void);
static void on_rows_done(WcRow *rows, int count);
static void on_rows_err(int code);

// ── Help / Settings action menu (long-press anywhere) ────────────────────────
static ActionMenuLevel *s_am_level;
static ActionMenuLevel *s_am_sort;
static Window    *s_info_window;
static TextLayer *s_info_text;

static void info_window_unload(Window *w) {
  (void)w;
  text_layer_destroy(s_info_text); s_info_text = NULL;
  window_destroy(s_info_window);   s_info_window = NULL;
}
static void push_info_window(void) {
  if (s_info_window) { window_stack_push(s_info_window, true); return; }
  s_info_window = window_create();
  window_set_background_color(s_info_window, wc_theme_bg(s_settings));
  Layer *root = window_get_root_layer(s_info_window);
  GRect b = layer_get_bounds(root);
  s_info_text = text_layer_create(GRect(6, 8, b.size.w - 12, b.size.h - 16));
  text_layer_set_background_color(s_info_text, GColorClear);
  text_layer_set_text_color(s_info_text, wc_theme_fg(s_settings));
  text_layer_set_font(s_info_text, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_overflow_mode(s_info_text, GTextOverflowModeWordWrap);
  text_layer_set_text(s_info_text,
    "Token, theme, accent, refresh interval, home-page counts, and other settings live in the Wristcord configuration page in the Pebble phone app.");
  layer_add_child(root, text_layer_get_layer(s_info_text));
  window_set_window_handlers(s_info_window, (WindowHandlers){ .unload = info_window_unload });
  window_stack_push(s_info_window, true);
}
static void am_help(ActionMenu *m, const ActionMenuItem *item, void *ctx) {
  (void)m; (void)item; (void)ctx;
  tutorial_window_push(s_settings);
}
static void am_settings_info(ActionMenu *m, const ActionMenuItem *item, void *ctx) {
  (void)m; (void)item; (void)ctx;
  push_info_window();
}
// Sort-mode picker. Selecting a row persists the new mode, re-fetches so the
// full guild list is on hand again, and pushes the change up to pkjs so the
// Clay config page reflects it next time the phone opens it.
static void am_pick_sort(ActionMenu *m, const ActionMenuItem *item, void *ctx) {
  (void)m; (void)ctx;
  WcSortMode picked = (WcSortMode)(uintptr_t)action_menu_item_get_action_data(item);
  if (picked == s_settings->sort_mode) return;
  s_settings->sort_mode = picked;
  wc_settings_save(s_settings);
  // Push up to pkjs so the next Clay open shows the same value.
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) == APP_MSG_OK && out) {
    dict_write_uint8(out, MESSAGE_KEY_PUSH_SORT_MODE, (uint8_t)picked);
    app_message_outbox_send();
  }
  // Re-fetch so the (possibly previously trimmed) guild list is whole again
  // before the new sort is applied.
  if (s_settings->has_token) {
    wc_rows_fetch(OP_HOME, "", on_rows_done, on_rows_err);
  } else if (s_state == ST_READY) {
    apply_server_sort_and_trim();
    if (s_menu) menu_layer_reload_data(s_menu);
  }
}
static void am_did_close(ActionMenu *m, const ActionMenuItem *performed, void *ctx) {
  (void)m; (void)performed; (void)ctx;
  if (s_am_level) { action_menu_hierarchy_destroy(s_am_level, NULL, NULL); s_am_level = NULL; }
  s_am_sort = NULL;     // owned by s_am_level
}
static void open_help_menu(void) {
  // Sub-level: sort-by picker.
  s_am_sort = action_menu_level_create(4);
  action_menu_level_add_action(s_am_sort, "Most used",      am_pick_sort, (void*)(uintptr_t)WC_SORT_MOST_USED);
  action_menu_level_add_action(s_am_sort, "Discord order",  am_pick_sort, (void*)(uintptr_t)WC_SORT_DISCORD_ORDER);
  action_menu_level_add_action(s_am_sort, "Alphabetical",   am_pick_sort, (void*)(uintptr_t)WC_SORT_ALPHABETICAL);
  action_menu_level_add_action(s_am_sort, "Recent activity",am_pick_sort, (void*)(uintptr_t)WC_SORT_RECENT_ACTIVITY);
  // Root level: 3 actions, top-level Settings → Sort by → submenu.
  s_am_level = action_menu_level_create(3);
  action_menu_level_add_action(s_am_level, "Help", am_help, NULL);
  action_menu_level_add_child(s_am_level, s_am_sort, "Sort servers");
  action_menu_level_add_action(s_am_level, "Settings", am_settings_info, NULL);
  ActionMenuConfig config = (ActionMenuConfig){
    .root_level = s_am_level,
    .colors = { .background = s_settings->accent, .foreground = GColorWhite },
    .align = ActionMenuAlignCenter,
    .did_close = am_did_close,
  };
  action_menu_open(&config);
}

// ── server sort + trim ────────────────────────────────────────────────────────
// Advance past a leading "decoration" prefix on a server name: ASCII
// non-alphanumerics (`!`, `.`, `-`, etc.) AND whole multi-byte UTF-8 code
// points (emoji, flags). Users often prefix server names with these to float
// them in Discord's own UI; alphabetical mode should sort by what's after
// the decoration.
static const char *skip_alpha_decorations(const char *s) {
  while (*s) {
    unsigned char c = (unsigned char)*s;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) break;
    int len;
    if (c < 0x80)             len = 1;            // ASCII non-alphanumeric
    else if ((c >> 5) == 0x6) len = 2;            // 2-byte UTF-8 lead
    else if ((c >> 4) == 0xE) len = 3;            // 3-byte UTF-8 lead
    else if ((c >> 3) == 0x1E) len = 4;           // 4-byte UTF-8 lead (most emoji)
    else                      len = 1;            // continuation / invalid → step 1
    s += len;
  }
  return s;
}

// Returns negative if a should sort before b, positive otherwise.
static int compare_guilds(const HRow *a, const HRow *b, WcSortMode mode) {
  switch (mode) {
    case WC_SORT_MOST_USED: {
      if (a->use_count != b->use_count) return b->use_count - a->use_count;
      // Tiebreak: most recent activity, then Discord order
      int la = (int)strlen(a->mostrecent), lb = (int)strlen(b->mostrecent);
      if (la != lb) return lb - la;
      int c = strcmp(b->mostrecent, a->mostrecent);
      if (c != 0) return c;
      return a->discord_pos - b->discord_pos;
    }
    case WC_SORT_DISCORD_ORDER:
      return a->discord_pos - b->discord_pos;
    case WC_SORT_ALPHABETICAL: {
      // Case-insensitive ASCII compare, after skipping any leading decoration
      // (ASCII non-alphanumerics + UTF-8 emoji / multi-byte code points).
      const char *pa = skip_alpha_decorations(a->name);
      const char *pb = skip_alpha_decorations(b->name);
      while (*pa && *pb) {
        char ca = *pa, cb = *pb;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        pa++; pb++;
      }
      int tail = (int)(unsigned char)*pa - (int)(unsigned char)*pb;
      // Deterministic tiebreak when both stripped names are equal (e.g., two
      // emoji-only servers, or duplicate names) — fall back to Discord order.
      if (tail != 0) return tail;
      return a->discord_pos - b->discord_pos;
    }
    case WC_SORT_RECENT_ACTIVITY: {
      int la = (int)strlen(a->mostrecent), lb = (int)strlen(b->mostrecent);
      if (la != lb) return lb - la;
      return strcmp(b->mostrecent, a->mostrecent);
    }
  }
  return a->discord_pos - b->discord_pos;
}

// In-place insertion sort restricted to the contiguous guild rows. We don't
// touch Settings / headers / DMs / Show-all rows — only WC_HROW_GUILD entries
// are reordered, and only within the range they already occupy.
static void apply_server_sort_and_trim(void) {
  int start = -1, end = -1;
  for (int i = 0; i < s_count; i++) {
    if (s_rows[i].kind == WC_HROW_GUILD) {
      if (start < 0) start = i;
      end = i;
    }
  }
  if (start < 0) return;
  int n = end - start + 1;
  // Insertion sort — n is at most ~70, plenty fast.
  for (int i = start + 1; i <= end; i++) {
    HRow key = s_rows[i];
    int j = i - 1;
    while (j >= start && compare_guilds(&s_rows[j], &key, s_settings->sort_mode) > 0) {
      s_rows[j + 1] = s_rows[j];
      j--;
    }
    s_rows[j + 1] = key;
  }
  // Trim to server_count by deleting the surplus 'g' rows. Anything past the
  // preview window is reachable via "Show all servers".
  int keep = s_settings->server_count;
  if (n > keep) {
    int drop = n - keep;
    // Shift everything after the kept-range backward over the surplus.
    for (int i = start + keep; i + drop <= s_count - 1; i++) {
      s_rows[i] = s_rows[i + drop];
    }
    s_count -= drop;
  }
}

// ── rows callbacks ────────────────────────────────────────────────────────────
static void on_rows_done(WcRow *rows, int count) {
  s_count = 0;
  for (int i = 0; i < count && s_count < WC_MAX_ROWS; i++) {
    WcRow *w = &rows[i];
    if (w->n_fields < 1) continue;
    char k = w->fields[0][0];
    HRow *r = &s_rows[s_count];
    r->id[0] = '\0'; r->name[0] = '\0';
    r->color = GColorWhite; r->ping_count = 0; r->unread = false;
    r->section = WC_SEC_NONE;
    if (k == 'S') {
      r->kind = WC_HROW_SETTINGS;
    } else if (k == 'H' && w->n_fields >= 3) {
      r->kind = WC_HROW_HEADER;
      wc_utf8_copy(r->name, w->fields[1], sizeof(r->name));
      // icon code in fields[2]: 'dm' / 'server'
      const char *ic = w->fields[2];
      r->section = (ic[0] == 'd') ? WC_SEC_DM : WC_SEC_SERVER;
    } else if (k == 'D' && w->n_fields >= 6) {
      r->kind = WC_HROW_DM;
      strncpy(r->id, w->fields[1], sizeof(r->id) - 1); r->id[sizeof(r->id) - 1] = '\0';
      wc_utf8_copy(r->name, w->fields[2], sizeof(r->name));
      r->color = wc_hex_to_color(w->fields[3]);
      r->ping_count = wc_atoi(w->fields[4]); if (r->ping_count < 0) r->ping_count = 0;
      r->unread = (w->fields[5][0] == '1');
    } else if (k == 'M' && w->n_fields >= 2) {
      r->kind = WC_HROW_SHOWALL;
      r->section = (w->fields[1][0] == 'd') ? WC_SEC_DM : WC_SEC_SERVER;
    } else if (k == 'g' && w->n_fields >= 4) {
      r->kind = WC_HROW_GUILD;
      strncpy(r->id, w->fields[1], sizeof(r->id) - 1); r->id[sizeof(r->id) - 1] = '\0';
      wc_utf8_copy(r->name, w->fields[2], sizeof(r->name));
      r->color = wc_hex_to_color(w->fields[3]);
      if (w->n_fields >= 7) r->ping_count = wc_atoi(w->fields[6]);
      if (r->ping_count < 0) r->ping_count = 0;
      if (w->n_fields >= 8) r->unread = (w->fields[7][0] == '1');
      r->mostrecent[0] = '\0';
      if (w->n_fields >= 9 && w->fields[8][0]) {
        strncpy(r->mostrecent, w->fields[8], sizeof(r->mostrecent) - 1);
        r->mostrecent[sizeof(r->mostrecent) - 1] = '\0';
      }
      r->discord_pos = (w->n_fields >= 10) ? wc_atoi(w->fields[9]) : 0;
      r->use_count = wc_viewstats_guild(r->id);
    } else {
      continue;
    }
    s_count++;
  }
  // Sort + trim the guild block by the user's preference. The pkjs side sends
  // ALL guilds in Discord order; we re-arrange them per sort_mode and keep only
  // the top server_count, since this is the home page preview slice.
  apply_server_sort_and_trim();
  s_state = (s_count == 0) ? ST_EMPTY : ST_READY;
  stop_loading_anim();
  if (s_menu) {
    menu_layer_reload_data(s_menu);
    // Only place the cursor on first load. Silent refetches (window_appear
    // after returning from a sub-window, or HOME_REFRESH from pkjs once the
    // gateway hits READY) must preserve the user's current selection.
    if (s_needs_initial_focus) {
      int focus = -1;
      for (int i = 0; i < s_count; i++) {
        if (s_rows[i].kind == WC_HROW_DM) { focus = i; break; }
      }
      if (focus < 0) {
        for (int i = 0; i < s_count; i++) {
          if (s_rows[i].kind == WC_HROW_GUILD) { focus = i; break; }
        }
      }
      if (focus < 0) focus = 0;
      menu_layer_set_selected_index(s_menu, (MenuIndex){ .section = 0, .row = focus }, MenuRowAlignCenter, false);
      s_needs_initial_focus = false;
    }
  }
}
static void on_rows_err(int code) {
  stop_loading_anim();
  s_err_code = code; s_state = ST_ERROR;
  if (s_menu) menu_layer_reload_data(s_menu);
}
static void start_fetch(void) {
  s_state = ST_LOADING;
  start_loading_anim();
  if (s_menu) menu_layer_reload_data(s_menu);
  wc_rows_fetch(OP_HOME, "", on_rows_done, on_rows_err);
}

// ── menu callbacks ────────────────────────────────────────────────────────────
static uint16_t get_num_rows(MenuLayer *m, uint16_t section, void *ctx) {
  (void)m; (void)section; (void)ctx;
  return s_state == ST_READY ? (uint16_t)s_count : 1;
}
static int16_t get_cell_height(struct MenuLayer *m, MenuIndex *ci, void *ctx) {
  (void)m; (void)ctx;
  if (s_state != ST_READY) return 184;
  HRow *r = &s_rows[ci->row];
  switch (r->kind) {
    case WC_HROW_SETTINGS: return 28;
    case WC_HROW_HEADER:   return 24;
    case WC_HROW_DM:       return 42;
    case WC_HROW_SHOWALL:  return 28;
    case WC_HROW_GUILD:    return 42;
  }
  return 30;
}

static void draw_status(GContext *ctx, GRect b) {
  GColor fg = wc_theme_fg(s_settings);
  if (s_state == ST_LOADING) {
    int cx = b.origin.x + b.size.w / 2;
    bool show_logo = (s_logo && s_settings->theme != THEME_LIGHT);
    int block_h = (show_logo ? 56 + 14 : 0) + 12 + 14 + 22;
    int top = b.origin.y + (b.size.h - block_h) / 2;
    if (show_logo) {
      graphics_context_set_compositing_mode(ctx, GCompOpSet);
      graphics_draw_bitmap_in_rect(ctx, s_logo, GRect(cx - 28, top, 56, 56));
      top += 56 + 14;
    }
    int gap = 16, dot_y = top + 6;
    for (int i = 0; i < 3; i++) {
      bool active = (i == (s_anim_frame % 3));
      graphics_context_set_fill_color(ctx, active ? s_settings->accent : wc_theme_muted(s_settings));
      graphics_fill_circle(ctx, GPoint(cx - gap + i * gap, dot_y), active ? 5 : 3);
    }
    graphics_context_set_text_color(ctx, fg);
    graphics_draw_text(ctx, "Loading\xe2\x80\xa6", fonts_get_system_font(FONT_KEY_GOTHIC_18),
      GRect(b.origin.x + 6, dot_y + 12, b.size.w - 12, 24),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }
  const char *msg = (s_state == ST_NOTOKEN) ? "No token set.\nOpen Wristcord settings\nin the Pebble app." :
                    (s_state == ST_ERROR && s_err_code == 1) ? "Sign-in failed.\nCheck your token." :
                    (s_state == ST_ERROR && s_err_code == 2) ? "Rate limited.\nTry again soon." :
                    (s_state == ST_ERROR) ? "Couldn't load home." :
                                            "Nothing here yet.";
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, msg, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(b.origin.x + 6, b.origin.y + 8, b.size.w - 12, b.size.h - 12),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// Settings entry (compact, top of list).
static void draw_settings_row(GContext *ctx, GRect b, bool selected) {
  GColor fg     = wc_theme_fg(s_settings);
  GColor muted  = wc_theme_muted(s_settings);
  wc_draw_section_icon(ctx, GRect(b.origin.x + 4, b.origin.y, 18, b.size.h),
                       WC_ICON_SETTINGS, selected ? GColorWhite : s_settings->accent);
  graphics_context_set_text_color(ctx, selected ? GColorWhite : fg);
  graphics_draw_text(ctx, "Settings", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(b.origin.x + 28, b.origin.y + 4, b.size.w - 28 - 18, 22),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  wc_draw_chevron(ctx, GRect(b.size.w - 18, b.origin.y, 14, b.size.h), false, selected ? GColorWhite : muted);
}

// Section header (24px row): small icon + label in muted color, with a thin
// divider line above so the section visually starts here.
static void draw_section_header(GContext *ctx, GRect b, HRow *r, bool selected) {
  (void)selected;
  GColor muted = wc_theme_muted(s_settings);
  GColor accent = s_settings->accent;
  graphics_context_set_stroke_color(ctx, muted);
  graphics_draw_line(ctx, GPoint(b.origin.x + 4, b.origin.y),
                          GPoint(b.origin.x + b.size.w - 4, b.origin.y));
  WcSectionIcon ic = (r->section == WC_SEC_DM) ? WC_ICON_DM : WC_ICON_SERVERS;
  wc_draw_section_icon(ctx, GRect(b.origin.x + 4, b.origin.y, 18, b.size.h), ic, accent);
  graphics_context_set_text_color(ctx, muted);
  graphics_draw_text(ctx, r->name, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(b.origin.x + 26, b.origin.y + 4, b.size.w - 30, 18),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void draw_initials_row(GContext *ctx, GRect b, HRow *r, bool selected) {
  GColor fg = selected ? GColorWhite : wc_theme_fg(s_settings);
  char ini[8]; wc_make_initials(r->name, ini, sizeof(ini));
  GPoint disc_c = GPoint(b.origin.x + 6 + 11, b.origin.y + b.size.h / 2);
  wc_draw_dot(ctx, disc_c, 11, r->color, ini);
  // Discord-style: ping badge ONLY for @mentions, never for plain unread.
  // The user can still see per-channel unread dots in the channel list.
  wc_draw_ping_marker(ctx, disc_c, 11, r->ping_count, s_settings);
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, r->name, fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(b.origin.x + 34, b.origin.y + 9, b.size.w - 38, 24),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void draw_showall_row(GContext *ctx, GRect b, HRow *r, bool selected) {
  GColor fg = selected ? GColorWhite : s_settings->accent;
  const char *label = (r->section == WC_SEC_DM) ? "Show all DMs" : "Show all servers";
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(b.origin.x + 30, b.origin.y + 5, b.size.w - 40, 20),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  wc_draw_chevron(ctx, GRect(b.size.w - 16, b.origin.y, 14, b.size.h), false, fg);
}

static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *ci, void *ctx2) {
  (void)ctx2;
  GRect b = layer_get_bounds(cell_layer);
  bool selected = menu_cell_layer_is_highlighted(cell_layer);
  if (s_state != ST_READY) {
    graphics_context_set_fill_color(ctx, wc_theme_bg(s_settings));
    graphics_fill_rect(ctx, b, 0, GCornerNone);
    draw_status(ctx, b);
    return;
  }
  HRow *r = &s_rows[ci->row];
  switch (r->kind) {
    case WC_HROW_SETTINGS: draw_settings_row(ctx, b, selected); break;
    case WC_HROW_HEADER:   draw_section_header(ctx, b, r, selected); break;
    case WC_HROW_DM:
    case WC_HROW_GUILD:    draw_initials_row(ctx, b, r, selected); break;
    case WC_HROW_SHOWALL:  draw_showall_row(ctx, b, r, selected); break;
  }
}

static void select_click(struct MenuLayer *m, MenuIndex *ci, void *ctx) {
  (void)m; (void)ctx;
  if (s_state != ST_READY) {
    if (s_state == ST_NOTOKEN || s_state == ST_ERROR) open_help_menu();
    return;
  }
  HRow *r = &s_rows[ci->row];
  switch (r->kind) {
    case WC_HROW_SETTINGS: open_help_menu(); break;
    case WC_HROW_HEADER:   break;                                            // not clickable
    case WC_HROW_DM:       chat_view_window_push(s_settings, r->id, r->name); break;
    case WC_HROW_GUILD:    channel_list_window_push(s_settings, r->id, r->name); break;
    case WC_HROW_SHOWALL:
      if (r->section == WC_SEC_DM) all_dms_window_push(s_settings);
      else all_servers_window_push(s_settings);
      break;
  }
}

static void select_long_click(struct MenuLayer *m, MenuIndex *ci, void *ctx) {
  (void)m; (void)ci; (void)ctx;
  open_help_menu();
}

// MenuLayer: skip past unselectable header rows when the user is scrolling.
static void selection_will_change(struct MenuLayer *menu_layer, MenuIndex *new_index,
                                  MenuIndex old_index, void *callback_context) {
  (void)menu_layer; (void)callback_context;
  if (s_state != ST_READY) return;
  int want = new_index->row;
  if (want < 0 || want >= s_count) return;
  // No-op when the index didn't actually move (focus-only events). Without
  // this guard, hitting a header at row 0/last would still try to step in a
  // default direction, which can shove the cursor unexpectedly.
  if (new_index->row == old_index.row) return;
  int dir = (new_index->row > old_index.row) ? 1 : -1;
  // Step over consecutive header rows in the same direction.
  while (want >= 0 && want < s_count && s_rows[want].kind == WC_HROW_HEADER) {
    want += dir;
  }
  // If the search ran off the end (e.g., the user landed on a trailing
  // header with no selectable rows past it), back up the other way to find
  // the nearest selectable row instead of clamping to a header.
  if (want < 0 || want >= s_count || s_rows[want].kind == WC_HROW_HEADER) {
    want = new_index->row;
    int back = -dir;
    while (want >= 0 && want < s_count && s_rows[want].kind == WC_HROW_HEADER) {
      want += back;
    }
  }
  if (want < 0) want = 0;
  if (want >= s_count) want = s_count - 1;
  // Final guard: if even after both passes we'd land on a header (degenerate
  // home page with only headers), leave the cursor where it was.
  if (s_rows[want].kind == WC_HROW_HEADER) want = old_index.row;
  new_index->row = want;
}

// Re-fetch on appear so the ACK + gateway updates land when the user returns
// from a sub-window.
static bool s_was_hidden;
static void window_appear(Window *w) {
  (void)w;
  if (s_was_hidden && s_state == ST_READY && s_settings->has_token) {
    // Silent refetch — keep current rows on screen during the request.
    wc_rows_fetch(OP_HOME, "", on_rows_done, on_rows_err);
  }
  s_was_hidden = false;
}
static void window_disappear(Window *w) { (void)w; s_was_hidden = true; }

static bool s_tut_checked;
static void tut_timer_cb(void *data) {
  (void)data;
  if (!persist_exists(PK_TUTORIAL_DONE)) tutorial_window_push(s_settings);
}
static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_menu = menu_layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, b.size.w, b.size.h - STATUS_BAR_LAYER_HEIGHT));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = get_num_rows,
    .get_cell_height = get_cell_height,
    .draw_row = draw_row,
    .select_click = select_click,
    .select_long_click = select_long_click,
    .selection_will_change = selection_will_change,
  });
  menu_layer_set_normal_colors(s_menu, wc_theme_bg(s_settings), wc_theme_fg(s_settings));
  menu_layer_set_highlight_colors(s_menu, s_settings->accent, GColorWhite);
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  s_titlebar = wc_titlebar_create(root, b, "Wristcord", s_settings);
  if (!s_logo) s_logo = gbitmap_create_with_resource(RESOURCE_ID_DISCORD_LOGO);
  if (s_settings->has_token) start_fetch();
  else { s_state = ST_NOTOKEN; menu_layer_reload_data(s_menu); }

  if (!s_tut_checked) {
    s_tut_checked = true;
    if (!persist_exists(PK_TUTORIAL_DONE)) app_timer_register(350, tut_timer_cb, NULL);
  }
}
static void window_unload(Window *w) {
  (void)w;
  stop_loading_anim();
  if (s_logo) { gbitmap_destroy(s_logo); s_logo = NULL; }
  menu_layer_destroy(s_menu); s_menu = NULL;
  text_layer_destroy(s_titlebar); s_titlebar = NULL;
}

void server_list_window_push(WristcordSettings *settings) {
  s_settings = settings;
  s_tut_checked = false;
  s_was_hidden = false;
  s_needs_initial_focus = true;
  s_window = window_create();
  window_set_background_color(s_window, wc_theme_bg(settings));
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .appear = window_appear,
    .disappear = window_disappear,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

void server_list_handle_settings(void) {
  if (!s_window) return;
  window_set_background_color(s_window, wc_theme_bg(s_settings));
  if (s_menu) {
    menu_layer_set_normal_colors(s_menu, wc_theme_bg(s_settings), wc_theme_fg(s_settings));
    menu_layer_set_highlight_colors(s_menu, s_settings->accent, GColorWhite);
  }
  if (s_settings->has_token && s_state == ST_NOTOKEN) start_fetch();
  else if (s_state == ST_READY && s_settings->has_token) {
    // A Clay save may have changed sort_mode and/or server_count. Re-fetch so
    // the full guild list is on hand (we may have trimmed the previous one),
    // then the on_rows_done path applies the new sort.
    wc_rows_fetch(OP_HOME, "", on_rows_done, on_rows_err);
  } else if (s_menu) menu_layer_reload_data(s_menu);
}

// Called when pkjs's gateway reaches READY post-launch — the home page's first
// fetch landed during IDENTIFYING and had no DMs. Silent refetch so the new
// data drops in without flashing a Loading screen.
void server_list_handle_home_refresh(void) {
  if (!s_window || !s_settings->has_token) return;
  // Only refetch when home is actually on top — if the user is in a server, we
  // don't want to step on the channel-list fetch.
  if (window_stack_get_top_window() != s_window) return;
  if (s_state == ST_READY) {
    // Silent: keep current rows on screen while the new ones come in.
    wc_rows_fetch(OP_HOME, "", on_rows_done, on_rows_err);
  } else {
    start_fetch();
  }
}
