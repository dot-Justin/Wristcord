// src/c/all_dms.c — full DM list (all 1:1 + group DMs, sorted newest-first).
#include "all_dms.h"
#include "rows.h"
#include "ui_util.h"
#include "chat_view.h"

#define OP_DMS_ALL 8

typedef struct {
  char  id[20];
  char  name[28];
  GColor color;
  int   mention_count;
  bool  unread;
} DRow;

typedef enum { ST_LOADING, ST_READY, ST_EMPTY, ST_ERROR } LoadState;

static Window *s_window;
static MenuLayer *s_menu;
static TextLayer *s_titlebar;
static WristcordSettings *s_settings;
static DRow *s_all;          // heap-allocated; see HANDOFF gotcha #3 (image-size cap)
static int s_count;
static LoadState s_state;
static int s_err_code;

static void on_rows_done(WcRow *rows, int count) {
  if (!s_all) return;
  s_count = 0;
  for (int i = 0; i < count && s_count < WC_MAX_ROWS; i++) {
    WcRow *w = &rows[i];
    if (w->n_fields < 6) continue;
    DRow *r = &s_all[s_count];
    strncpy(r->id, w->fields[1], sizeof(r->id) - 1); r->id[sizeof(r->id) - 1] = '\0';
    wc_utf8_copy(r->name, w->fields[2], sizeof(r->name));
    r->color = wc_hex_to_color(w->fields[3]);
    r->mention_count = wc_atoi(w->fields[4]);
    if (r->mention_count < 0) r->mention_count = 0;
    r->unread = (w->fields[5][0] == '1');
    s_count++;
  }
  s_state = (s_count == 0) ? ST_EMPTY : ST_READY;
  if (s_menu) menu_layer_reload_data(s_menu);
}
static void on_rows_err(int code) {
  s_err_code = code; s_state = ST_ERROR;
  if (s_menu) menu_layer_reload_data(s_menu);
}

static void start_fetch(void) {
  s_state = ST_LOADING;
  if (s_menu) menu_layer_reload_data(s_menu);
  wc_rows_fetch(OP_DMS_ALL, "", on_rows_done, on_rows_err);
}

static uint16_t get_num_rows(MenuLayer *m, uint16_t section, void *ctx) {
  (void)m; (void)section; (void)ctx;
  return s_state == ST_READY ? (uint16_t)s_count : 1;
}
static int16_t get_cell_height(struct MenuLayer *m, MenuIndex *ci, void *ctx) {
  (void)m; (void)ci; (void)ctx;
  return s_state == ST_READY ? 42 : 140;
}

static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *ci, void *ctx2) {
  (void)ctx2;
  GRect b = layer_get_bounds(cell_layer);
  if (s_state != ST_READY) {
    graphics_context_set_fill_color(ctx, wc_theme_bg(s_settings));
    graphics_fill_rect(ctx, b, 0, GCornerNone);
    const char *msg = "Loading\xe2\x80\xa6";
    if (s_state == ST_EMPTY) msg = "No DMs.";
    else if (s_state == ST_ERROR) msg = "Couldn't load DMs.";
    graphics_context_set_text_color(ctx, wc_theme_fg(s_settings));
    graphics_draw_text(ctx, msg, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
      GRect(b.origin.x + 6, b.origin.y + 8, b.size.w - 12, b.size.h - 12),
      GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }
  DRow *r = &s_all[ci->row];
  bool selected = menu_cell_layer_is_highlighted(cell_layer);
  GColor fg = selected ? GColorWhite : wc_theme_fg(s_settings);
  char ini[8]; wc_make_initials(r->name, ini, sizeof(ini));
  GPoint disc_c = GPoint(b.origin.x + 6 + 11, b.origin.y + b.size.h / 2);
  wc_draw_dot(ctx, disc_c, 11, r->color, ini);
  // Ping marker on bottom-right of the disc.
  int badge_count = r->mention_count > 0 ? r->mention_count : (r->unread ? 1 : 0);
  wc_draw_ping_marker(ctx, disc_c, 11, badge_count, s_settings);
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, r->name, fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(b.origin.x + 34, b.origin.y + 9, b.size.w - 38, 24),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void select_click(struct MenuLayer *m, MenuIndex *ci, void *ctx) {
  (void)m; (void)ctx;
  if (s_state != ST_READY) return;
  DRow *r = &s_all[ci->row];
  chat_view_window_push(s_settings, r->id, r->name);
}

static void window_load(Window *w) {
  s_all = (DRow*)malloc(sizeof(DRow) * WC_MAX_ROWS);
  if (!s_all) { s_state = ST_ERROR; s_err_code = 3; }
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_menu = menu_layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, b.size.w, b.size.h - STATUS_BAR_LAYER_HEIGHT));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = get_num_rows,
    .get_cell_height = get_cell_height,
    .draw_row = draw_row,
    .select_click = select_click,
  });
  menu_layer_set_normal_colors(s_menu, wc_theme_bg(s_settings), wc_theme_fg(s_settings));
  menu_layer_set_highlight_colors(s_menu, s_settings->accent, GColorWhite);
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  s_titlebar = wc_titlebar_create(root, b, "Direct Messages", s_settings);
  start_fetch();
}
static void window_unload(Window *w) {
  (void)w;
  wc_rows_cancel();
  menu_layer_destroy(s_menu); s_menu = NULL;
  text_layer_destroy(s_titlebar); s_titlebar = NULL;
  window_destroy(s_window); s_window = NULL;
  free(s_all); s_all = NULL;
}

void all_dms_window_push(WristcordSettings *settings) {
  s_settings = settings;
  s_window = window_create();
  window_set_background_color(s_window, wc_theme_bg(settings));
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);
}
