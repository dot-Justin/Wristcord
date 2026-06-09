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

static void outbox_failed(DictionaryIterator *it, AppMessageResult reason, void *ctx) {
  (void)it; (void)ctx;
  APP_LOG(APP_LOG_LEVEL_WARNING, "outbox failed: %d", (int)reason);
}

static void inbox_received(DictionaryIterator *it, void *ctx) {
  (void)ctx;
  wc_rows_handle_inbox(it);
  wc_compose_handle_inbox(it);
  if (wc_settings_apply_from_msg(it, &s_settings)) {   // only on actual settings pushes, not row data
    wc_settings_save(&s_settings);
    server_list_handle_settings();
  }
  // pkjs nudges us when the gateway reaches READY — the home page's initial
  // fetch lands during IDENTIFYING with no DM data, so re-fetch when we get
  // this nudge so the DM section finally populates without the user having to
  // navigate away and back.
  Tuple *t = dict_find(it, MESSAGE_KEY_HOME_REFRESH);
  if (t) server_list_handle_home_refresh();
}

static void init(void) {
  wc_settings_load(&s_settings);
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  // Request the largest buffers the firmware allows (don't exceed the max, or open can fail).
  uint32_t in_max = app_message_inbox_size_maximum();
  uint32_t out_max = app_message_outbox_size_maximum();
  uint32_t in_sz = in_max < 2048 ? in_max : 2048;
  uint32_t out_sz = out_max < 256 ? out_max : 256;
  app_message_open(in_sz, out_sz);
  server_list_window_push(&s_settings);
}

static void deinit(void) {
  // server_list owns its window; OS tears down on exit
}

int main(void) { init(); app_event_loop(); deinit(); }
