// src/c/channel_list.c
#include "channel_list.h"
#include "chat_view.h"
#include "rows.h"
#include "ui_util.h"
#include "readstate.h"

#define OP_CHANNELS 2
#define PK_COLLAPSED_CATS 201

typedef struct { char kind; char id[20]; char name[28]; int parent; char last_message_id[20]; } CRow;
typedef enum { ST_LOADING, ST_READY, ST_EMPTY, ST_ERROR } LoadState;

static Window *s_window;
static MenuLayer *s_menu;
static TextLayer *s_titlebar;
static WristcordSettings *s_settings;
static char s_guild_id[20];
static char s_guild_name[28];
static CRow s_all[WC_MAX_ROWS];
static int s_all_count;
static int s_visible[WC_MAX_ROWS];
static int s_visible_count;
static LoadState s_state;
static int s_err_code;
static char s_collapsed[256];

static void rebuild_visible(void) {
  s_visible_count = 0;
  for (int i = 0; i < s_all_count; i++) {
    CRow *r = &s_all[i];
    bool show;
    if (r->kind == 'c' || r->parent < 0) show = true;                 // categories + uncategorized channels always shown
    else show = !wc_csv_contains(s_collapsed, s_all[r->parent].id);   // channel hidden if its category is collapsed
    if (show) s_visible[s_visible_count++] = i;
  }
}

static void on_rows_done(WcRow *rows, int count) {
  if (!s_menu) return;
  s_all_count = 0;
  for (int i = 0; i < count && s_all_count < WC_MAX_ROWS; i++) {
    WcRow *w = &rows[i];
    if (w->n_fields < 4) continue;
    CRow *r = &s_all[s_all_count];
    r->kind = w->fields[0][0];
    strncpy(r->id, w->fields[1], sizeof(r->id) - 1); r->id[sizeof(r->id) - 1] = '\0';
    wc_utf8_copy(r->name, w->fields[2], sizeof(r->name));
    const char *par = w->fields[3];
    r->parent = (par && par[0]) ? wc_atoi(par) : -1;
    if (r->parent >= s_all_count) r->parent = -1;   // guard: parent must precede child (no OOB on s_all)
    r->last_message_id[0] = '\0';
    if (w->n_fields >= 5 && w->fields[4][0]) {
      strncpy(r->last_message_id, w->fields[4], sizeof(r->last_message_id) - 1);
      r->last_message_id[sizeof(r->last_message_id) - 1] = '\0';
      if (r->kind == 't') wc_readstate_seed_if_absent(r->id, r->last_message_id);  // baseline on first sight
    }
    s_all_count++;
  }
  s_state = (s_all_count == 0) ? ST_EMPTY : ST_READY;
  rebuild_visible();
  menu_layer_reload_data(s_menu);
}
static void on_rows_err(int code) { if (!s_menu) return; s_err_code = code; s_state = ST_ERROR; menu_layer_reload_data(s_menu); }

static void start_fetch(void) {
  s_state = ST_LOADING;
  if (s_menu) menu_layer_reload_data(s_menu);
  wc_rows_fetch(OP_CHANNELS, s_guild_id, on_rows_done, on_rows_err);
}

static uint16_t get_num_rows(MenuLayer *m, uint16_t section, void *ctx) {
  (void)m; (void)section; (void)ctx;
  return s_state == ST_READY ? (uint16_t)s_visible_count : 1;
}
static int16_t get_cell_height(struct MenuLayer *m, MenuIndex *ci, void *ctx) {
  (void)m; (void)ctx;
  if (s_state != ST_READY) return 140;
  CRow *r = &s_all[s_visible[ci->row]];
  return r->kind == 'c' ? 28 : 36;
}
static void draw_status(GContext *ctx, const GRect b) {
  const char *msg = "Loading\xe2\x80\xa6";
  if (s_state == ST_EMPTY) msg = "No channels.";
  else if (s_state == ST_ERROR) msg = (s_err_code == 1) ? "Sign-in failed." :
                                      (s_err_code == 2) ? "Rate limited.\nTry again soon." :
                                                          "Couldn't load channels.";
  graphics_context_set_text_color(ctx, wc_theme_fg(s_settings));
  graphics_draw_text(ctx, msg, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(b.origin.x + 6, b.origin.y + 8, b.size.w - 12, b.size.h - 12),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}
static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *ci, void *ctx2) {
  (void)ctx2;
  GRect b = layer_get_bounds(cell_layer);
  if (s_state != ST_READY) { draw_status(ctx, b); return; }
  CRow *r = &s_all[s_visible[ci->row]];
  bool selected = menu_cell_layer_is_highlighted(cell_layer);
  GColor fg = selected ? GColorWhite : wc_theme_fg(s_settings);
  if (r->kind == 'c') {
    graphics_context_set_text_color(ctx, fg);
    graphics_draw_text(ctx, r->name, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
      GRect(b.origin.x + 8, b.origin.y + 4, b.size.w - 8 - 16, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    bool expanded = !wc_csv_contains(s_collapsed, r->id);
    wc_draw_chevron(ctx, GRect(b.size.w - 16, b.origin.y, 14, b.size.h), expanded, fg);
  } else {
    int indent = (r->parent >= 0) ? 16 : 0;
    // v1: unread indicators are intentionally hidden because pure-REST tracking
    // can't agree with Discord's canonical read state (reads on phone/desktop
    // can't be detected without a gateway connection). v1.1 will re-enable this
    // backed by a Discord gateway WebSocket in pkjs; the wc_readstate_* helpers
    // still maintain local state so we can replace the source with one line.
    GColor hash = selected ? GColorWhite : wc_theme_muted(s_settings);
    graphics_context_set_text_color(ctx, hash);
    graphics_draw_text(ctx, "#", fonts_get_system_font(FONT_KEY_GOTHIC_18),
      GRect(b.origin.x + 14 + indent, b.origin.y + 7, 14, 22), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(ctx, fg);
    graphics_draw_text(ctx, r->name, fonts_get_system_font(FONT_KEY_GOTHIC_18),
      GRect(b.origin.x + 14 + indent + 16, b.origin.y + 6, b.size.w - indent - 36, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}
static void select_click(struct MenuLayer *m, MenuIndex *ci, void *ctx) {
  (void)ctx;
  if (s_state != ST_READY) return;
  CRow *r = &s_all[s_visible[ci->row]];
  if (r->kind == 'c') {
    if (wc_csv_contains(s_collapsed, r->id)) wc_csv_remove(s_collapsed, r->id);
    else wc_csv_add(s_collapsed, sizeof(s_collapsed), r->id);
    persist_write_string(PK_COLLAPSED_CATS, s_collapsed);
    rebuild_visible();
    menu_layer_reload_data(m);
  } else {
    chat_view_window_push(s_settings, r->id, r->name);
  }
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
  });
  menu_layer_set_normal_colors(s_menu, wc_theme_bg(s_settings), wc_theme_fg(s_settings));
  menu_layer_set_highlight_colors(s_menu, s_settings->accent, GColorWhite);
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  s_titlebar = wc_titlebar_create(root, b, s_guild_name, s_settings);

  start_fetch();
}
static void window_unload(Window *w) {
  (void)w;
  wc_rows_cancel();                 // drop any in-flight fetch -> no stale callback into this window
  menu_layer_destroy(s_menu); s_menu = NULL;
  text_layer_destroy(s_titlebar); s_titlebar = NULL;
  window_destroy(s_window); s_window = NULL;
}

void channel_list_window_push(WristcordSettings *settings, const char *guild_id, const char *guild_name) {
  s_settings = settings;
  strncpy(s_guild_id, guild_id ? guild_id : "", sizeof(s_guild_id) - 1); s_guild_id[sizeof(s_guild_id) - 1] = '\0';
  wc_utf8_copy(s_guild_name, guild_name ? guild_name : "", sizeof(s_guild_name));
  s_collapsed[0] = '\0';
  if (persist_exists(PK_COLLAPSED_CATS)) persist_read_string(PK_COLLAPSED_CATS, s_collapsed, sizeof(s_collapsed));
  s_window = window_create();
  window_set_background_color(s_window, wc_theme_bg(settings));
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);
}
