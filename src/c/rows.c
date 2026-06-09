// src/c/rows.c
#include "rows.h"

#define RS '\x1E'
#define US '\x1F'

static char s_buf[WC_ROWS_BUF];
static int s_len;
static uint8_t s_op;
static char s_id[24];
static char s_text[24];               // optional TEXT for ops like OP_MESSAGES_BEFORE
static bool s_has_text;
static uint8_t s_page;
static bool s_active;
static WcRowsDone s_done;
static WcRowsErr s_err;
static WcRow s_rows[WC_MAX_ROWS];

static AppTimer *s_watchdog;
static AppTimer *s_retry_timer;
static int s_retries;
static void send_request(void);

// If no response arrives (e.g. the request was dropped because pkjs wasn't ready
// yet at launch), retry the whole fetch a few times, then surface an error.
static void watchdog_cb(void *data) {
  s_watchdog = NULL;
  if (!s_active) return;
  if (s_retries < 4) {
    s_retries++;
    s_len = 0; s_buf[0] = '\0'; s_page = 0;
    send_request();
    s_watchdog = app_timer_register(4000, watchdog_cb, NULL);
  } else {
    s_active = false;
    if (s_err) s_err(3);
  }
}
static void arm_watchdog(void) {
  if (s_watchdog) app_timer_cancel(s_watchdog);
  s_watchdog = app_timer_register(4000, watchdog_cb, NULL);
}
static void cancel_watchdog(void) {
  if (s_watchdog) { app_timer_cancel(s_watchdog); s_watchdog = NULL; }
}

// Abort any in-flight fetch: stop timers and drop callbacks so a popped window
// never receives a stale response. Call from each window's unload.
void wc_rows_cancel(void) {
  cancel_watchdog();
  if (s_retry_timer) { app_timer_cancel(s_retry_timer); s_retry_timer = NULL; }
  s_active = false;
  s_done = NULL;
  s_err = NULL;
}

static void do_send(void *data) {
  s_retry_timer = NULL;                  // this invocation consumed the pending retry (if any)
  if (!s_active) return;
  DictionaryIterator *it;
  AppMessageResult r = app_message_outbox_begin(&it);
  if (r != APP_MSG_OK) {                 // channel not ready / busy — retry shortly
    s_retry_timer = app_timer_register(250, do_send, NULL);
    return;
  }
  dict_write_uint8(it, MESSAGE_KEY_OP, s_op);
  dict_write_cstring(it, MESSAGE_KEY_ID, s_id);
  dict_write_uint8(it, MESSAGE_KEY_PAGE, s_page);
  if (s_has_text) dict_write_cstring(it, MESSAGE_KEY_TEXT, s_text);
  app_message_outbox_send();
}
static void send_request(void) { do_send(NULL); }

void wc_rows_fetch(uint8_t op, const char *id, WcRowsDone done, WcRowsErr err) {
  wc_rows_fetch_with_text(op, id, NULL, done, err);
}

void wc_rows_fetch_with_text(uint8_t op, const char *id, const char *text,
                             WcRowsDone done, WcRowsErr err) {
  s_op = op; s_page = 0; s_len = 0; s_buf[0] = '\0';
  strncpy(s_id, id ? id : "", sizeof(s_id) - 1); s_id[sizeof(s_id) - 1] = '\0';
  if (text) {
    strncpy(s_text, text, sizeof(s_text) - 1); s_text[sizeof(s_text) - 1] = '\0';
    s_has_text = true;
  } else {
    s_text[0] = '\0';
    s_has_text = false;
  }
  s_done = done; s_err = err; s_active = true;
  s_retries = 0;
  send_request();
  arm_watchdog();
}

static int parse_rows(void) {
  int count = 0;
  char *rec = s_buf;
  bool done = false;
  while (!done && count < WC_MAX_ROWS) {
    char *rs = strchr(rec, RS);
    if (rs) { *rs = '\0'; } else { done = true; }
    if (*rec != '\0' || rs) {           // skip a truly-empty trailing record
      WcRow *row = &s_rows[count];
      row->n_fields = 0;
      char *f = rec;
      while (row->n_fields < WC_MAX_FIELDS) {
        char *us = strchr(f, US);
        if (us) *us = '\0';
        row->fields[row->n_fields++] = f;
        if (!us) break;
        f = us + 1;
      }
      count++;
    }
    if (rs) rec = rs + 1;
  }
  return count;
}

void wc_rows_handle_inbox(DictionaryIterator *it) {
  Tuple *err = dict_find(it, MESSAGE_KEY_ERR);
  if (err) { if (s_active) { cancel_watchdog(); s_active = false; if (s_err) s_err((int)err->value->uint8); } return; }
  Tuple *op = dict_find(it, MESSAGE_KEY_OP);
  Tuple *rows = dict_find(it, MESSAGE_KEY_ROWS);
  if (!op || !rows) return;             // not a rows response (e.g. a settings push)
  if (!s_active) return;

  const char *chunk = rows->value->cstring;
  int clen = chunk ? (int)strlen(chunk) : 0;
  if (s_len > 0 && s_len + 1 < WC_ROWS_BUF) { s_buf[s_len++] = RS; }
  int space = WC_ROWS_BUF - 1 - s_len;
  if (clen > space) clen = space;       // guard buffer
  if (clen > 0) { memcpy(s_buf + s_len, chunk, clen); s_len += clen; }
  s_buf[s_len] = '\0';

  Tuple *more = dict_find(it, MESSAGE_KEY_MORE);
  if (more && more->value->uint8) {
    s_page++;
    send_request();
    arm_watchdog();          // re-arm for the next page
  } else {
    cancel_watchdog();
    int c = parse_rows();
    s_active = false;
    if (s_done) s_done(s_rows, c);
  }
}
