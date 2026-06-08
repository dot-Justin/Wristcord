// src/c/compose.c  — M6-T3: dictation -> confirm -> OP_SEND -> refresh
#include "compose.h"
#include "chat_view.h"
#include "ui_util.h"

// WC_DEMO: bypass dictation and jump straight to the confirm screen with a fixed
// sample message. Used only by scripts/capture-store-shots.sh to grab the
// confirm-screen marketing shot (no STT in the emulator). Kept 0 for prod.
#define WC_DEMO 0

#define OP_SEND  4
// AppMessage keys are the SDK-generated MESSAGE_KEY_* (from package.json messageKeys),
// NOT raw indices — using 0/1/2/3 silently misroutes the message so pkjs never sees it.

static WristcordSettings *s_settings;
static char s_channel_id[20];
static char s_channel_name[28];
static char s_text[200];
static bool s_sending;
static AppTimer *s_send_timeout;

// 0=result, 1=fallback, 2=sending, 3=error
static uint8_t s_mode;
static int     s_fail_status;

#define HINTW 54   // right-edge column reserved for per-button hint labels

static Window    *s_win;
static TextLayer *s_body;
static Layer     *s_hints;             // right column: labels aligned to the 3 buttons
static char s_hu[10], s_hs[10], s_hd[10];   // UP / SELECT / DOWN hint text ("" = none)

static void start_dictation(void);

// Draw the button hints next to where each physical button sits (right edge):
// UP ~ upper third, SELECT ~ middle, DOWN ~ lower third. SELECT (primary) in accent.
static void hints_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GColor fg = wc_theme_fg(s_settings);
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  graphics_context_set_stroke_color(ctx, wc_theme_muted(s_settings));
  graphics_draw_line(ctx, GPoint(b.origin.x, 12), GPoint(b.origin.x, b.size.h - 12));
  int h = b.size.h;
  const char *labels[3] = { s_hu, s_hs, s_hd };
  int ys[3] = { h * 18 / 100, h / 2, h * 82 / 100 };
  for (int i = 0; i < 3; i++) {
    if (!labels[i][0]) continue;
    graphics_context_set_text_color(ctx, i == 1 ? s_settings->accent : fg);
    graphics_draw_text(ctx, labels[i], f, GRect(b.origin.x + 2, ys[i] - 9, b.size.w - 4, 18),
      GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
}

static void set_body(void) {
  if (!s_body) return;
  const char *t = s_text;
  s_hu[0] = s_hs[0] = s_hd[0] = '\0';
  if (s_mode == 0) {                       // confirm: show the detected text + button hints
    t = s_text[0] ? s_text : "(no text)";
    strcpy(s_hu, "Redo"); strcpy(s_hs, "Send"); strcpy(s_hd, "Cancel");
  } else if (s_mode == 2) {
    t = "Sending\xe2\x80\xa6";              // in flight — no actions
  } else if (s_mode == 3) {
    t = "Send failed.";
    strcpy(s_hs, "Retry"); strcpy(s_hd, "Cancel");
  } else if (s_mode == 1) {
    strcpy(s_hs, "Retry"); strcpy(s_hd, "Cancel");
#if defined(PBL_MICROPHONE)
    switch ((DictationSessionStatus)s_fail_status) {
      case DictationSessionStatusFailureNoSpeechDetected: t = "Didn't catch that."; break;
      case DictationSessionStatusFailureConnectivityError: t = "No connection.";    break;
      case DictationSessionStatusFailureDisabled:          t = "Voice disabled.";   break;
      default:                                             t = "Dictation N/A.";    break;
    }
#else
    t = "No microphone.";
#endif
  }
  text_layer_set_text(s_body, t);
  if (s_hints) layer_mark_dirty(s_hints);
}

static void send_timeout_cb(void *data) {
  (void)data;
  s_send_timeout = NULL;
  if (s_sending) { s_sending = false; s_mode = 3; set_body(); }  // no ack -> show failure, don't hang on "Sending..."
}

static void do_send(void) {
  s_sending = true; s_mode = 2; set_body();
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK || !out) {
    s_sending = false; s_mode = 3; set_body(); return;
  }
  dict_write_uint8(out,   MESSAGE_KEY_OP,   OP_SEND);
  dict_write_cstring(out, MESSAGE_KEY_ID,   s_channel_id);
  DictionaryResult dr = dict_write_cstring(out, MESSAGE_KEY_TEXT, s_text);
  if (dr != DICT_OK || app_message_outbox_send() != APP_MSG_OK) {
    s_sending = false; s_mode = 3; set_body(); return;       // outbox full / send failed
  }
  if (s_send_timeout) app_timer_cancel(s_send_timeout);
  s_send_timeout = app_timer_register(8000, send_timeout_cb, NULL);  // bail if ack never arrives
}

static void on_select(ClickRecognizerRef r, void *ctx) {
  (void)r; (void)ctx;
  if (s_mode == 2) return;
  if (s_mode == 1) { window_stack_pop(true); start_dictation(); return; }
  do_send();
}
static void on_up(ClickRecognizerRef r, void *ctx) {
  (void)r; (void)ctx;
  if (s_mode == 1) return;
  window_stack_pop(true); start_dictation();
}
static void on_cancel(ClickRecognizerRef r, void *ctx) {
  (void)r; (void)ctx; window_stack_pop(true);
}
static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, on_select);
  window_single_click_subscribe(BUTTON_ID_UP,     on_up);
  window_single_click_subscribe(BUTTON_ID_DOWN,   on_cancel);
  window_single_click_subscribe(BUTTON_ID_BACK,   on_cancel);
}

static void win_load(Window *w) {
  GColor bg = wc_theme_bg(s_settings);
  window_set_background_color(w, bg);
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_body = text_layer_create(GRect(5, 6, b.size.w - HINTW - 8, b.size.h - 12));
  text_layer_set_background_color(s_body, GColorClear);
  text_layer_set_text_color(s_body, wc_theme_fg(s_settings));
  text_layer_set_font(s_body, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_overflow_mode(s_body, GTextOverflowModeWordWrap);
  layer_add_child(root, text_layer_get_layer(s_body));

  s_hints = layer_create(GRect(b.size.w - HINTW, 0, HINTW, b.size.h));
  layer_set_update_proc(s_hints, hints_update);
  layer_add_child(root, s_hints);

  set_body();
  window_set_click_config_provider(w, click_config);
}
static void win_unload(Window *w) {
  (void)w;
  if (s_send_timeout) { app_timer_cancel(s_send_timeout); s_send_timeout = NULL; }
  layer_destroy(s_hints); s_hints = NULL;
  text_layer_destroy(s_body); s_body = NULL;
  window_destroy(s_win); s_win = NULL;
}

static void push_compose(uint8_t mode) {
  s_mode = mode; s_sending = false;
  if (s_win) window_stack_remove(s_win, false);
  s_win = window_create();
  window_set_window_handlers(s_win, (WindowHandlers){ .load=win_load, .unload=win_unload });
  window_stack_push(s_win, true);
}

#if defined(PBL_MICROPHONE)
static DictationSession *s_dictation;
static void dictation_cb(DictationSession *session, DictationSessionStatus status,
                         char *transcription, void *context) {
  (void)session; (void)context;
  if (status == DictationSessionStatusSuccess) {
    wc_utf8_copy(s_text, transcription, sizeof(s_text));
    push_compose(0);
  } else {
    s_fail_status = (int)status; push_compose(1);
  }
}
static void start_dictation(void) {
  if (!s_dictation) {
    s_dictation = dictation_session_create(sizeof(s_text), dictation_cb, NULL);
    if (s_dictation) {
      dictation_session_enable_confirmation(s_dictation, false);
      dictation_session_enable_error_dialogs(s_dictation, false);
    }
  }
  if (s_dictation) { dictation_session_start(s_dictation); }
  else { s_fail_status = (int)DictationSessionStatusFailureSystemAborted; push_compose(1); }
}
#else
static void start_dictation(void) { s_fail_status = 0; push_compose(1); }
#endif

void wc_compose_handle_inbox(DictionaryIterator *it) {
  Tuple *op_t = dict_find(it, MESSAGE_KEY_OP);
  if (!op_t || op_t->value->uint8 != OP_SEND || !s_sending) return;
  if (s_send_timeout) { app_timer_cancel(s_send_timeout); s_send_timeout = NULL; }
  Tuple *err_t = dict_find(it, MESSAGE_KEY_ERR);
  s_sending = false;
  if (err_t && err_t->value->uint8 == 0) {
    vibes_short_pulse();
    if (s_win) window_stack_pop(true);
    chat_view_refresh();
  } else {
    s_mode = 3; set_body();
  }
}

void chat_compose_start(WristcordSettings *settings, const char *channel_id,
                        const char *channel_name) {
  s_settings = settings;
  strncpy(s_channel_id,   channel_id   ? channel_id   : "", sizeof(s_channel_id)-1);
  s_channel_id[sizeof(s_channel_id)-1] = '\0';
  wc_utf8_copy(s_channel_name, channel_name ? channel_name : "", sizeof(s_channel_name));
  s_text[0] = '\0'; s_sending = false;
#if WC_DEMO
  wc_utf8_copy(s_text, "Sounds great, see you at 7!", sizeof(s_text));
  push_compose(0);
#else
  start_dictation();
#endif
}
