// src/c/rows.c
#include "rows.h"

#define RS '\x1E'
#define US '\x1F'

static char s_buf[WC_ROWS_BUF];
static int s_len;
static uint8_t s_op;
static char s_id[24];
static uint8_t s_page;
static bool s_active;
static WcRowsDone s_done;
static WcRowsErr s_err;
static WcRow s_rows[WC_MAX_ROWS];

static void send_request(void) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_uint8(it, MESSAGE_KEY_OP, s_op);
  dict_write_cstring(it, MESSAGE_KEY_ID, s_id);
  dict_write_uint8(it, MESSAGE_KEY_PAGE, s_page);
  app_message_outbox_send();
}

void wc_rows_fetch(uint8_t op, const char *id, WcRowsDone done, WcRowsErr err) {
  s_op = op; s_page = 0; s_len = 0; s_buf[0] = '\0';
  strncpy(s_id, id ? id : "", sizeof(s_id) - 1); s_id[sizeof(s_id) - 1] = '\0';
  s_done = done; s_err = err; s_active = true;
  send_request();
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
  if (err) { if (s_active) { s_active = false; if (s_err) s_err((int)err->value->uint8); } return; }
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
  } else {
    int c = parse_rows();
    s_active = false;
    if (s_done) s_done(s_rows, c);
  }
}
