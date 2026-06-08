// src/c/server_list.c
#include "server_list.h"
#include "rows.h"
#include "ui_util.h"
#include "channel_list.h"
#include "tutorial.h"

#define OP_GUILDS 1
#define PK_EXPANDED 200

typedef struct {
  char kind;            // 'f' folder, 'g' guild
  char id[20];
  char name[28];
  GColor color;
  int parent;           // index into s_all of owning folder, or -1
  GColor members[4];
  int n_members;
} SRow;

typedef enum { ST_LOADING, ST_READY, ST_EMPTY, ST_ERROR, ST_NOTOKEN } LoadState;

static Window *s_window;
static MenuLayer *s_menu;
static TextLayer *s_titlebar;
static WristcordSettings *s_settings;
static SRow s_all[WC_MAX_ROWS];
static int s_all_count;
static int s_visible[WC_MAX_ROWS];
static int s_visible_count;
static LoadState s_state;
static int s_err_code;
static char s_expanded[256];

// Animated loading screen (logo + cycling accent dots).
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

// ── Help/Settings action menu ─────────────────────────────────────────────────
static ActionMenuLevel *s_am_level;

// Tiny "Settings info" window shown from Help/Settings → Settings
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
    "Change your token, theme, accent, and refresh rate in the Wristcord settings in the Pebble phone app.");
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

static void am_did_close(ActionMenu *m, const ActionMenuItem *performed, void *ctx) {
  (void)m; (void)performed; (void)ctx;
  if (s_am_level) { action_menu_hierarchy_destroy(s_am_level, NULL, NULL); s_am_level = NULL; }
}

static void open_help_menu(void) {
  s_am_level = action_menu_level_create(2);
  action_menu_level_add_action(s_am_level, "Help",     am_help,          NULL);
  action_menu_level_add_action(s_am_level, "Settings", am_settings_info, NULL);
  ActionMenuConfig config = (ActionMenuConfig){
    .root_level = s_am_level,
    .colors = {
      .background = s_settings->accent,
      .foreground = GColorWhite,
    },
    .align    = ActionMenuAlignCenter,
    .did_close = am_did_close,
  };
  action_menu_open(&config);
}

static void rebuild_visible(void) {
  s_visible_count = 0;
  for (int i = 0; i < s_all_count; i++) {
    SRow *r = &s_all[i];
    bool show;
    if (r->parent < 0) show = true;                                  // folders + top-level guilds
    else show = wc_csv_contains(s_expanded, s_all[r->parent].id);       // child guild only if folder expanded
    if (show) s_visible[s_visible_count++] = i;
  }
}

// ---- rows callbacks ----
static void on_rows_done(WcRow *rows, int count) {
  s_all_count = 0;
  for (int i = 0; i < count && s_all_count < WC_MAX_ROWS; i++) {
    WcRow *w = &rows[i];
    if (w->n_fields < 5) continue;
    SRow *r = &s_all[s_all_count];
    r->kind = w->fields[0][0];
    strncpy(r->id, w->fields[1], sizeof(r->id) - 1); r->id[sizeof(r->id) - 1] = '\0';
    wc_utf8_copy(r->name, w->fields[2], sizeof(r->name));
    r->color = wc_hex_to_color(w->fields[3]);
    const char *par = w->fields[4];
    r->parent = (par && par[0]) ? wc_atoi(par) : -1;
    if (r->parent >= s_all_count) r->parent = -1;   // guard: parent must precede child (no OOB on s_all)
    r->n_members = 0;
    if (w->n_fields >= 6 && w->fields[5][0]) {
      // Parse the comma-joined hex list without strtok/strncpy (libc-free; see ui_util).
      const char *p = w->fields[5];
      while (*p && r->n_members < 4) {
        r->members[r->n_members++] = wc_hex_to_color(p);   // stops at the comma
        while (*p && *p != ',') p++;
        if (*p == ',') p++; else break;
      }
    }
    s_all_count++;
  }
  s_state = (s_all_count == 0) ? ST_EMPTY : ST_READY;
  stop_loading_anim();
  rebuild_visible();
  if (s_menu) menu_layer_reload_data(s_menu);
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
  wc_rows_fetch(OP_GUILDS, "", on_rows_done, on_rows_err);
}

// ---- menu callbacks ----
static uint16_t get_num_rows(MenuLayer *m, uint16_t section, void *ctx) {
  (void)m; (void)section; (void)ctx;
  return s_state == ST_READY ? (uint16_t)s_visible_count : 1;
}
static int16_t get_cell_height(struct MenuLayer *m, MenuIndex *ci, void *ctx) {
  (void)m; (void)ctx;
  if (s_state != ST_READY) return 200;   // full-height status cell so content centers
  SRow *r = &s_all[s_visible[ci->row]];
  return r->kind == 'f' ? 30 : 42;
}
static void draw_status(GContext *ctx, const GRect b) {
  GColor fg = wc_theme_fg(s_settings);

  if (s_state == ST_LOADING) {
    int cx = b.origin.x + b.size.w / 2;
    bool show_logo = (s_logo && s_settings->theme != THEME_LIGHT);  // white logo invisible on light
    int block_h = (show_logo ? 56 + 14 : 0) + 12 + 14 + 22;         // logo + gap + dots + gap + label
    int top = b.origin.y + (b.size.h - block_h) / 2;
    if (show_logo) {
      graphics_context_set_compositing_mode(ctx, GCompOpSet);
      graphics_draw_bitmap_in_rect(ctx, s_logo, GRect(cx - 28, top, 56, 56));
      top += 56 + 14;
    }
    int gap = 16, dot_y = top + 6;                                  // 3 cycling dots
    for (int i = 0; i < 3; i++) {
      bool active = (i == (s_anim_frame % 3));
      graphics_context_set_fill_color(ctx, active ? s_settings->accent : wc_theme_muted(s_settings));
      graphics_fill_circle(ctx, GPoint(cx - gap + i * gap, dot_y), active ? 5 : 3);
    }
    graphics_context_set_text_color(ctx, fg);
    graphics_draw_text(ctx, "Loading your servers\xe2\x80\xa6", fonts_get_system_font(FONT_KEY_GOTHIC_18),
      GRect(b.origin.x + 6, dot_y + 12, b.size.w - 12, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }

  const char *msg = (s_state == ST_NOTOKEN) ? "No token set.\nOpen Wristcord settings\nin the Pebble app." :
                    (s_state == ST_ERROR && s_err_code == 1) ? "Sign-in failed.\nCheck your token." :
                    (s_state == ST_ERROR && s_err_code == 2) ? "Rate limited.\nTry again soon." :
                    (s_state == ST_ERROR) ? "Couldn't load servers." :
                                            "No servers found.";
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, msg, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(b.origin.x + 6, b.origin.y + 8, b.size.w - 12, b.size.h - 12),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}
static void draw_grid(GContext *ctx, GRect box, SRow *r) {
  int d = 22, gap = 2, cell = (d - gap) / 2;
  int x0 = box.origin.x, y0 = box.origin.y + (box.size.h - d) / 2;
  for (int i = 0; i < 4; i++) {
    GColor col = (i < r->n_members) ? r->members[i] : GColorLightGray;
    int cx = x0 + (i % 2) * (cell + gap) + cell / 2;
    int cy = y0 + (i / 2) * (cell + gap) + cell / 2;
    graphics_context_set_fill_color(ctx, col);
    graphics_fill_circle(ctx, GPoint(cx, cy), cell / 2);
  }
}
static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *ci, void *ctx2) {
  (void)ctx2;
  GRect b = layer_get_bounds(cell_layer);
  if (s_state != ST_READY) {
    // The lone status cell is "selected" -> MenuLayer paints it with the accent
    // highlight; repaint the theme bg so the status art sits on a clean backdrop.
    graphics_context_set_fill_color(ctx, wc_theme_bg(s_settings));
    graphics_fill_rect(ctx, b, 0, GCornerNone);
    draw_status(ctx, b);
    return;
  }
  SRow *r = &s_all[s_visible[ci->row]];
  bool selected = menu_cell_layer_is_highlighted(cell_layer);
  GColor fg = selected ? GColorWhite : wc_theme_fg(s_settings);
  if (r->kind == 'f') {
    draw_grid(ctx, GRect(b.origin.x + 6, b.origin.y, 22, b.size.h), r);
    graphics_context_set_text_color(ctx, fg);
    graphics_draw_text(ctx, r->name, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
      GRect(b.origin.x + 34, b.origin.y + 4, b.size.w - 34 - 18, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    wc_draw_chevron(ctx, GRect(b.size.w - 18, b.origin.y, 14, b.size.h), wc_csv_contains(s_expanded, r->id), fg);
  } else {
    int indent = (r->parent >= 0) ? 14 : 0;
    char ini[8]; wc_make_initials(r->name, ini, sizeof(ini));
    wc_draw_dot(ctx, GPoint(b.origin.x + 6 + indent + 11, b.origin.y + b.size.h / 2), 11, r->color, ini);
    graphics_context_set_text_color(ctx, fg);
    graphics_draw_text(ctx, r->name, fonts_get_system_font(FONT_KEY_GOTHIC_18),
      GRect(b.origin.x + indent + 34, b.origin.y + 9, b.size.w - indent - 38, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}
static void select_click(struct MenuLayer *m, MenuIndex *ci, void *ctx) {
  (void)ctx;
  if (s_state != ST_READY) return;
  SRow *r = &s_all[s_visible[ci->row]];
  if (r->kind == 'f') {
    if (wc_csv_contains(s_expanded, r->id)) wc_csv_remove(s_expanded, r->id);
    else wc_csv_add(s_expanded, sizeof(s_expanded), r->id);
    persist_write_string(PK_EXPANDED, s_expanded);
    rebuild_visible();
    menu_layer_reload_data(m);
  } else {
    channel_list_window_push(s_settings, r->id, r->name);
  }
}

static void select_long_click(struct MenuLayer *m, MenuIndex *ci, void *ctx) {
  (void)m; (void)ci; (void)ctx;
  open_help_menu();
}

static const char *s_titlebar_text = "Wristcord";
static bool s_tut_checked;

static void tut_timer_cb(void *data) {
  (void)data;
  if (!persist_exists(PK_TUTORIAL_DONE)) tutorial_window_push(s_settings);
}

static void window_appear(Window *w) {
  (void)w;
  if (!s_tut_checked) {
    s_tut_checked = true;
    // Defer the push: pushing a window *during* this appear transition leaves the
    // new window's click handlers uninstalled (onboarding then can't be advanced).
    if (!persist_exists(PK_TUTORIAL_DONE)) app_timer_register(350, tut_timer_cb, NULL);
  }
}

static void window_load(Window *w) {
  (void)w;
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);

  s_menu = menu_layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, b.size.w, b.size.h - STATUS_BAR_LAYER_HEIGHT));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows      = get_num_rows,
    .get_cell_height   = get_cell_height,
    .draw_row          = draw_row,
    .select_click      = select_click,
    .select_long_click = select_long_click,
  });
  menu_layer_set_normal_colors(s_menu, wc_theme_bg(s_settings), wc_theme_fg(s_settings));
  menu_layer_set_highlight_colors(s_menu, s_settings->accent, GColorWhite);
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  s_titlebar = wc_titlebar_create(root, b, s_titlebar_text, s_settings);
  if (!s_logo) s_logo = gbitmap_create_with_resource(RESOURCE_ID_DISCORD_LOGO);

  if (s_settings->has_token) start_fetch();
  else { s_state = ST_NOTOKEN; menu_layer_reload_data(s_menu); }
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
  s_expanded[0] = '\0';
  if (persist_exists(PK_EXPANDED)) persist_read_string(PK_EXPANDED, s_expanded, sizeof(s_expanded));
  s_window = window_create();
  window_set_background_color(s_window, wc_theme_bg(settings));
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .appear = window_appear,
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
  else if (s_menu) menu_layer_reload_data(s_menu);
}
