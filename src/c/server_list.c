// src/c/server_list.c
#include "server_list.h"
#include "rows.h"
#include "ui_util.h"

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
static StatusBarLayer *s_status_bar;
static WristcordSettings *s_settings;
static SRow s_all[WC_MAX_ROWS];
static int s_all_count;
static int s_visible[WC_MAX_ROWS];
static int s_visible_count;
static LoadState s_state;
static int s_err_code;
static char s_expanded[256];


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
    strncpy(r->name, w->fields[2], sizeof(r->name) - 1); r->name[sizeof(r->name) - 1] = '\0';
    r->color = wc_hex_to_color(w->fields[3]);
    const char *par = w->fields[4];
    r->parent = (par && par[0]) ? atoi(par) : -1;
    r->n_members = 0;
    if (w->n_fields >= 6 && w->fields[5][0]) {
      char tmp[80]; strncpy(tmp, w->fields[5], sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = '\0';
      char *tok = strtok(tmp, ",");
      while (tok && r->n_members < 4) { r->members[r->n_members++] = wc_hex_to_color(tok); tok = strtok(NULL, ","); }
    }
    s_all_count++;
  }
  s_state = (s_all_count == 0) ? ST_EMPTY : ST_READY;
  rebuild_visible();
  if (s_menu) menu_layer_reload_data(s_menu);
}
static void on_rows_err(int code) {
  s_err_code = code; s_state = ST_ERROR;
  if (s_menu) menu_layer_reload_data(s_menu);
}

static void start_fetch(void) {
  s_state = ST_LOADING;
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
  if (s_state != ST_READY) return 140;
  SRow *r = &s_all[s_visible[ci->row]];
  return r->kind == 'f' ? 30 : 42;
}
static void draw_status(GContext *ctx, const GRect b) {
  const char *msg = "Loading\xe2\x80\xa6";
  if (s_state == ST_EMPTY) msg = "No servers found.";
  else if (s_state == ST_NOTOKEN) msg = "No token set.\nOpen Wristcord settings\nin the Pebble app.";
  else if (s_state == ST_ERROR) msg = (s_err_code == 1) ? "Sign-in failed.\nCheck your token." :
                                      (s_err_code == 2) ? "Rate limited.\nTry again soon." :
                                                          "Couldn't load servers.";
  graphics_context_set_text_color(ctx, wc_theme_fg(s_settings));
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
  if (s_state != ST_READY) { draw_status(ctx, b); return; }
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
    char ini[3]; wc_make_initials(r->name, ini);
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
    APP_LOG(APP_LOG_LEVEL_INFO, "open guild %s (%s) -> channels in M4", r->name, r->id);
    // M4: push channel list for r->id
  }
}

static void window_load(Window *w) {
  (void)w;
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_status_bar = status_bar_layer_create();
  status_bar_layer_set_colors(s_status_bar, GColorBlack, wc_theme_fg(s_settings));
  status_bar_layer_set_separator_mode(s_status_bar, StatusBarLayerSeparatorModeNone);

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
  layer_add_child(root, status_bar_layer_get_layer(s_status_bar));

  if (s_settings->has_token) start_fetch();
  else { s_state = ST_NOTOKEN; menu_layer_reload_data(s_menu); }
}
static void window_unload(Window *w) {
  (void)w;
  menu_layer_destroy(s_menu); s_menu = NULL;
  status_bar_layer_destroy(s_status_bar);
}

void server_list_window_push(WristcordSettings *settings) {
  s_settings = settings;
  s_expanded[0] = '\0';
  if (persist_exists(PK_EXPANDED)) persist_read_string(PK_EXPANDED, s_expanded, sizeof(s_expanded));
  s_window = window_create();
  window_set_background_color(s_window, wc_theme_bg(settings));
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
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
