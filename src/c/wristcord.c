// src/c/wristcord.c
#include <pebble.h>
#include "settings.h"

static Window *s_window;
static TextLayer *s_title, *s_state;
static WristcordSettings s_settings;

static void render_state(void) {
  window_set_background_color(s_window, wc_theme_bg(&s_settings));
  text_layer_set_text_color(s_title, s_settings.accent);
  text_layer_set_text_color(s_state, wc_theme_fg(&s_settings));
  text_layer_set_text(s_state, s_settings.has_token
    ? "Connected.\nServers load in M3."
    : "No token set.\nOpen Wristcord settings\nin the Pebble app.");
}

static void inbox_dropped(AppMessageResult reason, void *ctx) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "inbox dropped: %d", (int)reason);
}

static void inbox_received(DictionaryIterator *it, void *ctx) {
  wc_settings_apply_from_msg(it, &s_settings);
  wc_settings_save(&s_settings);
  render_state();
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_title = text_layer_create(GRect(0, 20, b.size.w, 30));
  text_layer_set_text(s_title, "Wristcord");
  text_layer_set_font(s_title, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_title, GTextAlignmentCenter);
  text_layer_set_background_color(s_title, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_title));

  s_state = text_layer_create(GRect(8, 70, b.size.w - 16, b.size.h - 80));
  text_layer_set_font(s_state, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_state, GTextAlignmentCenter);
  text_layer_set_background_color(s_state, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_state));

  render_state();
}
static void window_unload(Window *w) {
  text_layer_destroy(s_title);
  text_layer_destroy(s_state);
}

static void init(void) {
  wc_settings_load(&s_settings);
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_open(2048, 256);
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);
}
static void deinit(void) { window_destroy(s_window); }

int main(void) { init(); app_event_loop(); deinit(); }
