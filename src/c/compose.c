// src/c/compose.c  — M6-T3: dictation -> confirm -> OP_SEND -> refresh
#include "compose.h"
#include "chat_view.h"
#include "ui_util.h"

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

static Window    *s_win;
static TextLayer *s_body;

static void start_dictation(void);

static char s_disp[256];

static void set_body(void) {
  if (!s_body) return;
  const char *t = s_text;
  if (s_mode == 0) {                       // result/confirm: show the text + what the buttons do
    snprintf(s_disp, sizeof(s_disp), "%s\n\nSELECT = Send\nUP = Redo   DOWN = Cancel", s_text);
    t = s_disp;
  }
  else if (s_mode == 2) t = "Sending...";
  else if (s_mode == 3) t = "Send failed!\nSel=Retry Dn=Cancel";
  else if (s_mode == 1) {
#if defined(PBL_MICROPHONE)
    switch ((DictationSessionStatus)s_fail_status) {
      case DictationSessionStatusFailureNoSpeechDetected:
        t = "Didn't catch that.\nSel=Retry Dn=Cancel"; break;
      case DictationSessionStatusFailureConnectivityError:
        t = "No connection.\nSel=Retry Dn=Cancel"; break;
      case DictationSessionStatusFailureDisabled:
        t = "Voice disabled.\nSel=Retry Dn=Cancel"; break;
      default:
        t = "Dictation N/A.\nSel=Retry Dn=Cancel"; break;
    }
#else
    t = "No microphone.\nSel=Retry Dn=Cancel";
#endif
  }
  text_layer_set_text(s_body, t);
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
  s_body = text_layer_create(GRect(4, 4, b.size.w-8, b.size.h-8));
  text_layer_set_background_color(s_body, bg);
  text_layer_set_text_color(s_body, wc_theme_fg(s_settings));
  text_layer_set_font(s_body, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_overflow_mode(s_body, GTextOverflowModeWordWrap);
  set_body();
  layer_add_child(root, text_layer_get_layer(s_body));
  window_set_click_config_provider(w, click_config);
}
static void win_unload(Window *w) {
  (void)w;
  if (s_send_timeout) { app_timer_cancel(s_send_timeout); s_send_timeout = NULL; }
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
  start_dictation();
}
