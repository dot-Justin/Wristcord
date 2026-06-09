// src/c/rows.h
#pragma once
#include <pebble.h>

#define WC_MAX_ROWS 128
#define WC_MAX_FIELDS 10
// Heap-allocated; sized to fit OP_HOME with the user's full guild list plus
// DMs and section headers. ~80B per row × 128 rows = 10240 bytes max.
#define WC_ROWS_BUF 10240

typedef struct { const char *fields[WC_MAX_FIELDS]; int n_fields; } WcRow;
typedef void (*WcRowsDone)(WcRow *rows, int count);
typedef void (*WcRowsErr)(int err_code);

// Begin a paged fetch (id may be "" e.g. for GUILDS). Exactly one of the
// callbacks fires when the whole paged response is complete or fails.
void wc_rows_fetch(uint8_t op, const char *id, WcRowsDone done, WcRowsErr err);

// Same as wc_rows_fetch but also sets MESSAGE_KEY_TEXT on the request. Used by
// chat_view's load-older path which needs to pass a `before=<message_id>`.
void wc_rows_fetch_with_text(uint8_t op, const char *id, const char *text,
                             WcRowsDone done, WcRowsErr err);

// Feed EVERY inbox dictionary here; it ignores messages that aren't row responses.
void wc_rows_handle_inbox(DictionaryIterator *it);

// Abort the in-flight fetch (cancels timers, drops callbacks) so a late or
// retried response never fires into a destroyed window. Call from window unload.
void wc_rows_cancel(void);
