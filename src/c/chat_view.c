// src/c/chat_view.c
#include "chat_view.h"
#include "rows.h"
#include "ui_util.h"
#include "compose.h"
#include "msg_view.h"
#include "readstate.h"

#define OP_MESSAGES 3
#define WC_MAX_MSGS 40    // we only fetch ~20 messages; small cap keeps Emery virtual_size (uint16) headroom

typedef struct {
  char  author[24];
  GColor color;
  char  time[8];
  char  text[140];
  char  id[20];
  bool  truncated;
} Msg;

typedef enum { ST_LOADING, ST_READY, ST_EMPTY, ST_ERROR } LoadState;

static Window         *s_window;
static MenuLayer      *s_menu;
static TextLayer      *s_titlebar;
static WristcordSettings *s_settings;
static char            s_channel_id[20];
static char            s_channel_name[28];
static Msg             s_msgs[WC_MAX_MSGS];
static int             s_count;
static int             s_width = 200;   // updated on window_load from bounds
static LoadState       s_state;
static int             s_err_code;

// ── ActionMenu state ──────────────────────────────────────────────────────────

static ActionMenuLevel *s_level;
static char             s_sel_id[20];
static bool             s_sel_truncated;

static void am_read_full(ActionMenu *m, const ActionMenuItem *item, void *ctx) {
  (void)m; (void)item; (void)ctx;
  msg_view_window_push(s_settings, s_sel_id);
}

static void am_soon(ActionMenu *m, const ActionMenuItem *item, void *ctx) {
  (void)m; (void)item; (void)ctx;
  APP_LOG(APP_LOG_LEVEL_INFO, "action coming soon");
}

static void am_did_close(ActionMenu *m, const ActionMenuItem *performed, void *ctx) {
  (void)m; (void)performed; (void)ctx;
  if (s_level) { action_menu_hierarchy_destroy(s_level, NULL, NULL); s_level = NULL; }
}

static void open_action_menu(void) {
  uint16_t n = (s_sel_truncated ? 1 : 0) + 2;
  s_level = action_menu_level_create(n);
  if (s_sel_truncated) {
    action_menu_level_add_action(s_level, "Read full message", am_read_full, NULL);
  }
  action_menu_level_add_action(s_level, "Reply (soon)", am_soon, NULL);
  action_menu_level_add_action(s_level, "React (soon)", am_soon, NULL);
  ActionMenuConfig config = (ActionMenuConfig){
    .root_level = s_level,
    .colors = {
      .background = s_settings->accent,
      .foreground = GColorWhite,
    },
    .align = ActionMenuAlignCenter,
    .did_close = am_did_close,
  };
  action_menu_open(&config);
}

// ── rows callbacks ────────────────────────────────────────────────────────────

static void on_rows_done(WcRow *rows, int count) {
  if (!s_menu) return;
  s_count = 0;
  for (int i = 0; i < count && s_count < WC_MAX_MSGS; i++) {
    WcRow *w = &rows[i];
    if (w->n_fields < 6) continue;
    Msg *m = &s_msgs[s_count];
    strncpy(m->author, w->fields[0], sizeof(m->author) - 1); m->author[sizeof(m->author) - 1] = '\0';
    m->color = wc_hex_to_color(w->fields[1]);
    strncpy(m->time,   w->fields[2], sizeof(m->time)   - 1); m->time[sizeof(m->time) - 1] = '\0';
    strncpy(m->text,   w->fields[3], sizeof(m->text)   - 1); m->text[sizeof(m->text) - 1] = '\0';
    strncpy(m->id,     w->fields[4], sizeof(m->id)     - 1); m->id[sizeof(m->id) - 1] = '\0';
    m->truncated = (w->fields[5][0] == '1');
    s_count++;
  }
  s_state = (s_count == 0) ? ST_EMPTY : ST_READY;
  menu_layer_reload_data(s_menu);
  if (s_count > 0) {
    menu_layer_set_selected_index(s_menu, (MenuIndex){ .section = 0, .row = s_count - 1 },
                                  MenuRowAlignBottom, false);   // newest at bottom
    wc_readstate_mark(s_channel_id, s_msgs[s_count - 1].id);   // mark channel read up to newest message
  }
}

static void on_rows_err(int code) {
  if (!s_menu) return;
  s_err_code = code;
  s_state = ST_ERROR;
  menu_layer_reload_data(s_menu);
}

static void start_fetch(void) {
  s_state = ST_LOADING;
  if (s_menu) menu_layer_reload_data(s_menu);
  wc_rows_fetch(OP_MESSAGES, s_channel_id, on_rows_done, on_rows_err);
}

// ── MenuLayer callbacks ───────────────────────────────────────────────────────

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
  else if (s_state == ST_ERROR) msg =
    (s_err_code == 1) ? "Sign-in failed." :
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
    GRect(b.origin.x + 6, b.origin.y + 1, b.size.w - 58, 16),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_context_set_text_color(ctx, wc_theme_muted(s_settings));
  graphics_draw_text(ctx, msg->time, fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(b.size.w - 52, b.origin.y + 1, 46, 16),
    GTextOverflowModeFill, GTextAlignmentRight, NULL);
  graphics_context_set_text_color(ctx, wc_theme_fg(s_settings));
  graphics_draw_text(ctx, msg->text, fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(b.origin.x + 6, b.origin.y + 18, b.size.w - 12, b.size.h - 18),
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

static void select_click(struct MenuLayer *m, MenuIndex *ci, void *ctx) {
  (void)m; (void)ci; (void)ctx;
  if (s_state != ST_READY && s_state != ST_EMPTY) return;
  chat_compose_start(s_settings, s_channel_id, s_channel_name);
}

static void select_long_click(struct MenuLayer *m, MenuIndex *ci, void *ctx) {
  (void)m; (void)ctx;
  if (s_state != ST_READY) return;
  Msg *msg = &s_msgs[ci->row];
  strncpy(s_sel_id, msg->id, sizeof(s_sel_id) - 1); s_sel_id[sizeof(s_sel_id) - 1] = '\0';
  s_sel_truncated = msg->truncated;
  open_action_menu();
}

// ── window lifecycle ──────────────────────────────────────────────────────────

static char s_title[30];

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_width = b.size.w;

  s_menu = menu_layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, b.size.w, b.size.h - STATUS_BAR_LAYER_HEIGHT));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows      = get_num_rows,
    .get_cell_height   = get_cell_height,
    .draw_row          = draw_row,
    .select_click      = select_click,
    .select_long_click = select_long_click,
  });

  GColor bg = wc_theme_bg(s_settings);
  menu_layer_set_normal_colors(s_menu, bg, wc_theme_fg(s_settings));
  menu_layer_set_highlight_colors(s_menu, s_settings->accent, GColorWhite);   // visible selection

  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  snprintf(s_title, sizeof(s_title), "#%s", s_channel_name);
  s_titlebar = wc_titlebar_create(root, b, s_title, s_settings);

  start_fetch();
}

static void window_unload(Window *w) {
  (void)w;
  wc_rows_cancel();   // drop any in-flight fetch -> no stale callback into this window
  menu_layer_destroy(s_menu); s_menu = NULL;
  text_layer_destroy(s_titlebar); s_titlebar = NULL;
  window_destroy(s_window); s_window = NULL;
}

// ── public interface ──────────────────────────────────────────────────────────

void chat_view_refresh(void) {
  if (s_menu) start_fetch();
}

// ── public entry point ────────────────────────────────────────────────────────

void chat_view_window_push(WristcordSettings *settings, const char *channel_id, const char *channel_name) {
  s_settings = settings;
  strncpy(s_channel_id, channel_id ? channel_id : "", sizeof(s_channel_id) - 1);
  s_channel_id[sizeof(s_channel_id) - 1] = '\0';
  strncpy(s_channel_name, channel_name ? channel_name : "", sizeof(s_channel_name) - 1);
  s_channel_name[sizeof(s_channel_name) - 1] = '\0';

  s_window = window_create();
  window_set_background_color(s_window, wc_theme_bg(settings));
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}
