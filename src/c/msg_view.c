// src/c/msg_view.c — Read-full-message screen
#include "msg_view.h"
#include "rows.h"
#include "ui_util.h"

#define OP_MSG_FULL 5
#define MSG_VIEW_PADDING 4
#define MSG_VIEW_TEXT_BUF 2100

static Window         *s_window;
static ScrollLayer    *s_scroll;
static TextLayer      *s_text;
static TextLayer      *s_titlebar;
static WristcordSettings *s_settings;
static char            s_msg_id[20];
static char            s_full[MSG_VIEW_TEXT_BUF];

// ── rows callbacks ────────────────────────────────────────────────────────────

static void on_done(WcRow *rows, int count) {
  if (!s_window) return;

  s_full[0] = '\0';
  size_t pos = 0;
  for (int i = 0; i < count; i++) {
    if (rows[i].n_fields < 1) continue;
    const char *chunk = rows[i].fields[0];
    size_t chunk_len = strlen(chunk);
    if (pos + chunk_len >= MSG_VIEW_TEXT_BUF - 1) {
      chunk_len = MSG_VIEW_TEXT_BUF - 1 - pos;
    }
    memcpy(s_full + pos, chunk, chunk_len);
    pos += chunk_len;
    s_full[pos] = '\0';
    if (pos >= MSG_VIEW_TEXT_BUF - 1) break;
  }

  text_layer_set_text(s_text, s_full);

  // measure required height
  GRect scroll_bounds = layer_get_bounds(scroll_layer_get_layer(s_scroll));
  int text_w = scroll_bounds.size.w - MSG_VIEW_PADDING * 2;
  GSize content_size = graphics_text_layout_get_content_size(
    s_full,
    fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(0, 0, text_w, 10000),
    GTextOverflowModeWordWrap,
    GTextAlignmentLeft);
  int text_h = content_size.h + MSG_VIEW_PADDING * 2;
  if (text_h < scroll_bounds.size.h) text_h = scroll_bounds.size.h;

  layer_set_frame(text_layer_get_layer(s_text),
    GRect(MSG_VIEW_PADDING, MSG_VIEW_PADDING, text_w, text_h));
  scroll_layer_set_content_size(s_scroll, GSize(scroll_bounds.size.w, text_h + MSG_VIEW_PADDING));
}

static void on_err(int code) {
  if (!s_window) return;
  const char *msg =
    (code == 1) ? "Sign-in failed." :
    (code == 2) ? "Rate limited.\nTry again soon." :
                  "Couldn't load message.";
  text_layer_set_text(s_text, msg);
}

static const char *s_titlebar_text = "Message";

// ── window lifecycle ──────────────────────────────────────────────────────────

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);

  // scroll area below title bar
  GRect scroll_frame = GRect(0, STATUS_BAR_LAYER_HEIGHT,
                             b.size.w, b.size.h - STATUS_BAR_LAYER_HEIGHT);
  s_scroll = scroll_layer_create(scroll_frame);
  scroll_layer_set_shadow_hidden(s_scroll, true);

  // text layer — initial "Loading…", sized to scroll area
  int text_w = scroll_frame.size.w - MSG_VIEW_PADDING * 2;
  s_text = text_layer_create(
    GRect(MSG_VIEW_PADDING, MSG_VIEW_PADDING, text_w, scroll_frame.size.h - MSG_VIEW_PADDING * 2));
  text_layer_set_font(s_text, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_background_color(s_text, GColorClear);
  text_layer_set_text_color(s_text, wc_theme_fg(s_settings));
  text_layer_set_overflow_mode(s_text, GTextOverflowModeWordWrap);
  text_layer_set_text(s_text, "Loading\xe2\x80\xa6");

  scroll_layer_add_child(s_scroll, text_layer_get_layer(s_text));

  // wire buttons: UP/DOWN scroll, BACK pops
  scroll_layer_set_click_config_onto_window(s_scroll, w);

  layer_add_child(root, scroll_layer_get_layer(s_scroll));
  s_titlebar = wc_titlebar_create(root, b, s_titlebar_text, s_settings);

  // fetch full message text
  wc_rows_fetch(OP_MSG_FULL, s_msg_id, on_done, on_err);
}

static void window_unload(Window *w) {
  (void)w;
  wc_rows_cancel();
  text_layer_destroy(s_text);  s_text = NULL;
  scroll_layer_destroy(s_scroll); s_scroll = NULL;
  text_layer_destroy(s_titlebar); s_titlebar = NULL;
  window_destroy(s_window); s_window = NULL;
}

// ── public entry point ────────────────────────────────────────────────────────

void msg_view_window_push(WristcordSettings *settings, const char *message_id) {
  s_settings = settings;
  strncpy(s_msg_id, message_id ? message_id : "", sizeof(s_msg_id) - 1);
  s_msg_id[sizeof(s_msg_id) - 1] = '\0';

  s_window = window_create();
  window_set_background_color(s_window, wc_theme_bg(settings));
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}
