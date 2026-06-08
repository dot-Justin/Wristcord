// src/c/wristcord.c
#include <pebble.h>
#include "settings.h"
#include "rows.h"
#include "server_list.h"
#include "compose.h"

static WristcordSettings s_settings;

static void inbox_dropped(AppMessageResult reason, void *ctx) {
  (void)ctx;
  APP_LOG(APP_LOG_LEVEL_WARNING, "inbox dropped: %d", (int)reason);
}

static void inbox_received(DictionaryIterator *it, void *ctx) {
  (void)ctx;
  wc_rows_handle_inbox(it);
  wc_compose_handle_inbox(it);
  if (wc_settings_apply_from_msg(it, &s_settings)) {   // only on actual settings pushes, not row data
    wc_settings_save(&s_settings);
    server_list_handle_settings();
  }
}

static void init(void) {
  wc_settings_load(&s_settings);
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_open(2048, 256);
  server_list_window_push(&s_settings);
}

static void deinit(void) {
  // server_list owns its window; OS tears down on exit
}

int main(void) { init(); app_event_loop(); deinit(); }
