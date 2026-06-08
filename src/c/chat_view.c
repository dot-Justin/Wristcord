// src/c/chat_view.c
#include "chat_view.h"
#include "rows.h"
#include "ui_util.h"

#define OP_MESSAGES 3

typedef struct { char author[24]; GColor color; char time[8]; char text[140]; } Msg;
typedef enum { ST_LOADING, ST_READY, ST_EMPTY, ST_ERROR } LoadState;

static Window *s_window;
static MenuLayer *s_menu;
static StatusBarLayer *s_status_bar;
static WristcordSettings *s_settings;
static char s_channel_id[20];
static char s_channel_name[28];
static Msg s_msgs[WC_MAX_ROWS];
static int s_count;
static int s_width = 200;     // updated on window_load from bounds
static LoadState s_state;
static int s_err_code;

static void on_rows_done(WcRow *rows, int count) {
  if (!s_menu) return;
  s_count = 0;
  for (int i = 0; i < count && s_count < WC_MAX_ROWS; i++) {
    WcRow *w = &rows[i];
    if (w->n_fields < 4) continue;
    Msg *m = &s_msgs[s_count];
    strncpy(m->author, w->fields[0], sizeof(m->author) - 1); m->author[sizeof(m->author) - 1] = '\0';
    m->color = wc_hex_to_color(w->fields[1]);
    strncpy(m->time, w->fields[2], sizeof(m->time) - 1); m->time[sizeof(m->time) - 1] = '\0';
    strncpy(m->text, w->fields[3], sizeof(m->text) - 1); m->text[sizeof(m->text) - 1] = '\0';
    s_count++;
  }
  s_state = (s_count == 0) ? ST_EMPTY : ST_READY;
  menu_layer_reload_data(s_menu);
  if (s_count > 0) {
    menu_layer_set_selected_index(s_menu, (MenuIndex){ .section = 0, .row = s_count - 1 },
                                  MenuRowAlignBottom, false);   // newest at bottom
  }
}
static void on_rows_err(int code) { if (!s_menu) return; s_err_code = code; s_state = ST_ERROR; menu_layer_reload_data(s_menu); }

static void start_fetch(void) {
  s_state = ST_LOADING;
  if (s_menu) menu_layer_reload_data(s_menu);
  wc_rows_fetch(OP_MESSAGES, s_channel_id, on_rows_done, on_rows_err);
}

static uint16_t get_num_rows(MenuLayer *m, uint16_t section, void *ctx) {
  (void)m; (void)section; (void)ctx;
  return s_state == ST_READY ? (uint16_t)s_count : 1;
}
static int16_t get_cell_height(struct MenuLayer *m, MenuIndex *ci, void *ctx) {
  (void)m; (void)ctx;
  if (s_state != ST_READY) return 140;
  Msg *msg = &s_msgs[ci->row];
  GSize cs = graphics_text_layout_get_content_size(
    msg->text, fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(0, 0, s_width - 12, 2000), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  int h = 18 + cs.h + 6;
  return (int16_t)(h < 40 ? 40 : h);
}
static void draw_status(GContext *ctx, const GRect b) {
  const char *msg = "Loading\xe2\x80\xa6";
  if (s_state == ST_EMPTY) msg = "No messages yet.";
  else if (s_state == ST_ERROR) msg = (s_err_code == 1) ? "Sign-in failed." :
                                      (s_err_code == 2) ? "Rate limited.\nTry again soon." :
                                                          "Couldn't load messages.";
  graphics_context_set_text_color(ctx, wc_theme_fg(s_settings));
  graphics_draw_text(ctx, msg, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(b.origin.x + 6, b.origin.y + 8, b.size.w - 12, b.size.h - 12),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}
static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *ci, void *ctx2) {
  (void)ctx2;
  GRect b = layer_get_bounds(cell_layer);
  if (s_state != ST_READY) { draw_status(ctx, b); return; }
  Msg *msg = &s_msgs[ci->row];
  graphics_context_set_text_color(ctx, msg->color);
  graphics_draw_text(ctx, msg->author, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(b.origin.x + 6, b.origin.y + 1, b.size.w - 58, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, msg->time, fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(b.size.w - 52, b.origin.y + 1, 46, 16), GTextOverflowModeFill, GTextAlignmentRight, NULL);
  graphics_context_set_text_color(ctx, wc_theme_fg(s_settings));
  graphics_draw_text(ctx, msg->text, fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(b.origin.x + 6, b.origin.y + 18, b.size.w - 12, b.size.h - 18), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}
static void select_click(struct MenuLayer *m, MenuIndex *ci, void *ctx) {
  (void)m; (void)ci; (void)ctx;
  if (s_state != ST_READY && s_state != ST_EMPTY) return;
  APP_LOG(APP_LOG_LEVEL_INFO, "compose in #%s -> dictation in M6", s_channel_name);
  // M6: start dictation -> send to s_channel_id
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_width = b.size.w;
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
  GColor bg = wc_theme_bg(s_settings), fg = wc_theme_fg(s_settings);
  menu_layer_set_normal_colors(s_menu, bg, fg);
  menu_layer_set_highlight_colors(s_menu, bg, fg);   // invisible selection -> feed feel
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  layer_add_child(root, status_bar_layer_get_layer(s_status_bar));

  start_fetch();
}
static void window_unload(Window *w) {
  (void)w;
  wc_rows_cancel();                 // drop any in-flight fetch -> no stale callback into this window
  menu_layer_destroy(s_menu); s_menu = NULL;
  status_bar_layer_destroy(s_status_bar);
  window_destroy(s_window); s_window = NULL;
}

void chat_view_window_push(WristcordSettings *settings, const char *channel_id, const char *channel_name) {
  s_settings = settings;
  strncpy(s_channel_id, channel_id ? channel_id : "", sizeof(s_channel_id) - 1); s_channel_id[sizeof(s_channel_id) - 1] = '\0';
  strncpy(s_channel_name, channel_name ? channel_name : "", sizeof(s_channel_name) - 1); s_channel_name[sizeof(s_channel_name) - 1] = '\0';
  s_window = window_create();
  window_set_background_color(s_window, wc_theme_bg(settings));
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);
}
