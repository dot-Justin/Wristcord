// src/c/chat_view.c
#include "chat_view.h"
#include "rows.h"
#include "ui_util.h"
#include "compose.h"
#include "msg_view.h"
#include "readstate.h"

#define OP_MESSAGES        3
#define OP_ACK             6
#define OP_MESSAGES_BEFORE 9
#define WC_MAX_MSGS 60    // sliding window: 20 initial + up to ~40 older loaded via scroll-up

typedef struct {
  char  author[24];
  GColor color;
  char  time[8];
  char  text[140];
  char  id[20];
  bool  truncated;
  bool  mentions_me;       // v1.2: this message @mentions the current user
} Msg;

typedef enum { ST_LOADING, ST_READY, ST_EMPTY, ST_ERROR } LoadState;

static Window         *s_window;
static MenuLayer      *s_menu;
static TextLayer      *s_titlebar;
static WristcordSettings *s_settings;
static char            s_channel_id[20];
static char            s_channel_name[28];
// Heap-allocated to keep this off the loaded-image BSS (64KB image-size cap;
// see HANDOFF gotcha #3). Allocated in window_load, freed in window_unload.
static Msg            *s_msgs;
static int             s_count;
static int             s_width = 200;   // updated on window_load from bounds
static LoadState       s_state;
static int             s_err_code;
static bool            s_fetching;   // guards against overlapping fetches (initial vs poll)
static AppTimer       *s_poll;       // ambient refresh timer
static char            s_acked_id[20];  // last message id we've ACK'd this session
// Load-older paging state. Set when chat_view kicks off OP_MESSAGES_BEFORE so
// on_rows_done can tell "prepend" from "wholesale replace". s_at_oldest goes
// true when a load-older fetch returns zero rows.
static bool            s_loading_older;
static bool            s_at_oldest;
// Race: if the user scrolls past row 2 while an ambient poll is in flight,
// fetch_older would early-return on s_fetching. We instead remember the request
// and fire it as soon as the poll completes.
static bool            s_pending_older;

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

// Forward decl — on_rows_done calls fetch_older to drain a queued load-older
// request that arrived while a wholesale-replace fetch was in flight.
static void fetch_older(void);

// ── rows callbacks ────────────────────────────────────────────────────────────

// Parse one WcRow into a Msg slot. Returns true if the row was a valid message.
static bool parse_message_row(WcRow *w, Msg *m) {
  if (w->n_fields < 6) return false;
  wc_utf8_copy(m->author, w->fields[0], sizeof(m->author));
  m->color = wc_hex_to_color(w->fields[1]);
  strncpy(m->time, w->fields[2], sizeof(m->time) - 1); m->time[sizeof(m->time) - 1] = '\0';
  wc_utf8_copy(m->text, w->fields[3], sizeof(m->text));
  strncpy(m->id, w->fields[4], sizeof(m->id) - 1); m->id[sizeof(m->id) - 1] = '\0';
  m->truncated = (w->fields[5][0] == '1');
  m->mentions_me = (w->n_fields >= 7) && (w->fields[6][0] == '1');
  return true;
}

static void on_rows_done(WcRow *rows, int count) {
  s_fetching = false;
  if (!s_menu) return;

  // ─── Load-older (OP_MESSAGES_BEFORE) path ────────────────────────────────
  // Prepend the returned messages to s_msgs and keep the cursor visually on
  // the same message the user was reading. If the response is empty, we've
  // hit the start of history and won't ask again this session.
  if (s_loading_older) {
    s_loading_older = false;
    if (count <= 0 || !s_msgs) { s_at_oldest = true; return; }
    // Anchor preservation: the user is looking at row prev_sel.row right now;
    // after the prepend that message must still be on screen. Keep at least
    // (prev_sel.row + 1) of the existing buffer so the anchor isn't dropped
    // off the tail. Worst case we ingest fewer older messages than Discord
    // returned, which is fine — the next scroll-up triggers another fetch.
    MenuIndex prev_sel_pre = menu_layer_get_selected_index(s_menu);
    int anchor_keep_min = prev_sel_pre.row + 1;
    if (anchor_keep_min > s_count) anchor_keep_min = s_count;
    int n_in_max = WC_MAX_MSGS - anchor_keep_min;
    if (n_in_max > count) n_in_max = count;
    if (n_in_max < 1) {
      // Buffer is already full of messages the user is reading; can't fit any
      // older without dropping the anchor. Treat as "no more for now" — they
      // can re-trigger after scrolling down (which would drop the tail) and
      // back up.
      return;
    }
    // Drop newest from the tail if combining would exceed the cap. The user is
    // reading history right now; the bottom of the buffer is the least useful.
    int keep_existing = s_count;
    if (n_in_max + keep_existing > WC_MAX_MSGS) keep_existing = WC_MAX_MSGS - n_in_max;
    if (keep_existing < anchor_keep_min) keep_existing = anchor_keep_min;
    if (keep_existing < 0) keep_existing = 0;
    // Shift existing forward into [n_in_max, n_in_max+keep_existing) BEFORE
    // we write the new messages — otherwise we'd clobber them.
    if (keep_existing > 0) {
      memmove(&s_msgs[n_in_max], &s_msgs[0], sizeof(Msg) * keep_existing);
    }
    // Parse incoming straight into [0, n_in_max).
    int n_in = 0;
    for (int i = 0; i < count && n_in < n_in_max; i++) {
      if (parse_message_row(&rows[i], &s_msgs[n_in])) n_in++;
    }
    if (n_in == 0) {
      // Shift everything back — nothing parsed.
      if (keep_existing > 0) memmove(&s_msgs[0], &s_msgs[n_in_max], sizeof(Msg) * keep_existing);
      s_at_oldest = true;
      return;
    }
    // If we parsed fewer than reserved, compact.
    if (n_in < n_in_max && keep_existing > 0) {
      memmove(&s_msgs[n_in], &s_msgs[n_in_max], sizeof(Msg) * keep_existing);
    }
    s_count = n_in + keep_existing;

    MenuIndex prev_sel = menu_layer_get_selected_index(s_menu);
    menu_layer_reload_data(s_menu);
    // Keep cursor on the same message the user was looking at (it just moved
    // down by n_in rows).
    int new_row = prev_sel.row + n_in;
    if (new_row >= s_count) new_row = s_count - 1;
    menu_layer_set_selected_index(s_menu, (MenuIndex){ .section = 0, .row = new_row },
                                  MenuRowAlignCenter, false);
    s_state = ST_READY;
    return;
  }

  // ─── Initial / poll path (wholesale replace) ────────────────────────────
  // Was this an explicit (Loading) fetch, and was the user already at the newest msg?
  // We only auto-scroll / disturb the view when appropriate, so ambient polls don't
  // yank a user who has scrolled up to read history.
  bool was_loading = (s_state != ST_READY);
  bool at_bottom = was_loading;
  if (!was_loading && s_count > 0) {
    MenuIndex sel = menu_layer_get_selected_index(s_menu);
    at_bottom = (sel.row >= s_count - 1);
  }
  char prev_newest[20];
  strncpy(prev_newest, s_count > 0 ? s_msgs[s_count - 1].id : "", sizeof(prev_newest) - 1);
  prev_newest[sizeof(prev_newest) - 1] = '\0';

  s_count = 0;
  for (int i = 0; i < count && s_count < WC_MAX_MSGS; i++) {
    if (parse_message_row(&rows[i], &s_msgs[s_count])) s_count++;
  }
  s_state = (s_count == 0) ? ST_EMPTY : ST_READY;
  // A fresh initial/poll batch means there could be older history beyond what
  // we showed; allow another load-older attempt on scroll-up.
  s_at_oldest = false;

  const char *new_newest = s_count > 0 ? s_msgs[s_count - 1].id : "";
  bool changed = (strncmp(prev_newest, new_newest, sizeof(prev_newest)) != 0);

  // Always reload so the visible rows refresh — fields like mentions_me can
  // change without the newest_id changing (e.g., gateway becomes READY mid-
  // session and the next poll repopulates mention bits). Reload preserves the
  // current scroll position; we only auto-scroll to newest below when the
  // newest message actually moved.
  menu_layer_reload_data(s_menu);
  if ((was_loading || changed) && s_count > 0 && at_bottom) {
    menu_layer_set_selected_index(s_menu, (MenuIndex){ .section = 0, .row = s_count - 1 },
                                  MenuRowAlignBottom, false);   // newest at bottom
  }
  if (s_count > 0 && at_bottom) {
    const char *newest = s_msgs[s_count - 1].id;
    wc_readstate_mark(s_channel_id, newest);
    // Fire-and-forget gateway ACK so other Discord clients (phone/desktop)
    // reflect the read. Best-effort: outbox-busy/network failures are silent;
    // the next gateway READY will reassert canonical truth either way. Only
    // ACK each newest-id once per chat session to avoid spamming.
    if (newest[0] && strncmp(newest, s_acked_id, sizeof(s_acked_id)) != 0) {
      DictionaryIterator *out;
      if (app_message_outbox_begin(&out) == APP_MSG_OK && out) {
        dict_write_uint8(out,   MESSAGE_KEY_OP,   OP_ACK);
        dict_write_cstring(out, MESSAGE_KEY_ID,   s_channel_id);
        dict_write_cstring(out, MESSAGE_KEY_TEXT, newest);
        if (app_message_outbox_send() == APP_MSG_OK) {
          strncpy(s_acked_id, newest, sizeof(s_acked_id) - 1);
          s_acked_id[sizeof(s_acked_id) - 1] = '\0';
        }
      }
    }
  }
  // If selection_changed wanted to fetch older while we were busy, satisfy it
  // now. Single-shot — fetch_older itself sets s_pending_older=true if needed.
  if (s_pending_older) {
    s_pending_older = false;
    fetch_older();
  }
}

static void on_rows_err(int code) {
  s_fetching = false;
  if (s_loading_older) {
    // Don't surface load-older errors as a screen-replacing ST_ERROR. The user
    // still has the messages they had; just stop trying for this session.
    s_loading_older = false;
    s_at_oldest = true;
    return;
  }
  if (!s_menu) return;
  if (s_state != ST_READY) {           // keep showing existing messages if a poll just failed
    s_err_code = code;
    s_state = ST_ERROR;
    menu_layer_reload_data(s_menu);
  }
}

static void fetch_older(void) {
  if (s_loading_older || s_at_oldest) return;
  if (s_state != ST_READY || s_count == 0) return;
  if (s_count >= WC_MAX_MSGS) return;  // buffer full; user must scroll down to drop
  if (s_fetching) {
    // A wholesale-replace fetch (initial or poll) is in flight; queue ourselves
    // for as-soon-as it finishes. Without this, scrolling up while a poll's
    // request is in the air loses the load-older trigger until the user
    // scrolls down and back up.
    s_pending_older = true;
    return;
  }
  s_loading_older = true;
  s_fetching = true;
  // Oldest currently loaded; Discord returns 30 messages older than this.
  wc_rows_fetch_with_text(OP_MESSAGES_BEFORE, s_channel_id, s_msgs[0].id,
                          on_rows_done, on_rows_err);
}

// show_loading=true for the initial/explicit fetch (shows the Loading screen);
// false for ambient polls (silent — keeps the current messages on screen).
static void do_fetch(bool show_loading) {
  if (s_fetching) return;              // never overlap fetches on the shared rows module
  s_fetching = true;
  if (show_loading) {
    s_state = ST_LOADING;
    if (s_menu) menu_layer_reload_data(s_menu);
  }
  wc_rows_fetch(OP_MESSAGES, s_channel_id, on_rows_done, on_rows_err);
}
static void start_fetch(void) { do_fetch(true); }

static void poll_cb(void *data) {
  (void)data;
  s_poll = NULL;
  if (!s_window) return;
  // Only fetch when the chat is on top AND the user is at the bottom of the
  // list. A poll is a wholesale replace; if the user has scrolled up to read
  // (or just loaded older history via OP_MESSAGES_BEFORE) the replace would
  // throw away their current view. They'll get fresh data when they scroll
  // back down to the newest and the next poll fires.
  if (window_stack_get_top_window() == s_window && s_menu) {
    MenuIndex sel = menu_layer_get_selected_index(s_menu);
    bool at_bottom = (s_count > 0 && sel.row >= s_count - 1);
    if (at_bottom) do_fetch(false);
  }
  int sec = s_settings->poll_seconds;
  if (sec > 0) s_poll = app_timer_register((uint32_t)sec * 1000, poll_cb, NULL);
}
static void schedule_poll(void) {
  if (s_poll) { app_timer_cancel(s_poll); s_poll = NULL; }
  int sec = s_settings->poll_seconds;
  if (sec > 0) s_poll = app_timer_register((uint32_t)sec * 1000, poll_cb, NULL);
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
  if (s_state != ST_READY) {
    graphics_context_set_fill_color(ctx, wc_theme_bg(s_settings));   // cover the accent highlight on the lone status cell
    graphics_fill_rect(ctx, b, 0, GCornerNone);
    draw_status(ctx, b);
    return;
  }
  Msg *msg = &s_msgs[ci->row];
  // v1.2: gold-ish background tint for messages that @mention me. Discord uses
  // a warm yellow; Pebble's GColorChromeYellow is close enough to read on both
  // dark and light themes.
  bool selected = menu_cell_layer_is_highlighted(cell_layer);
  if (msg->mentions_me && !selected) {
    graphics_context_set_fill_color(ctx, GColorChromeYellow);
    graphics_fill_rect(ctx, b, 0, GCornerNone);
  }
  graphics_context_set_text_color(ctx, msg->color);
  graphics_draw_text(ctx, msg->author, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(b.origin.x + 6, b.origin.y + 1, b.size.w - 58, 16),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  GColor time_color = msg->mentions_me && !selected ? GColorBlack : wc_theme_muted(s_settings);
  graphics_context_set_text_color(ctx, time_color);
  graphics_draw_text(ctx, msg->time, fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(b.size.w - 52, b.origin.y + 1, 46, 16),
    GTextOverflowModeFill, GTextAlignmentRight, NULL);
  GColor body_color = msg->mentions_me && !selected ? GColorBlack : wc_theme_fg(s_settings);
  graphics_context_set_text_color(ctx, body_color);
  graphics_draw_text(ctx, msg->text, fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(b.origin.x + 6, b.origin.y + 18, b.size.w - 12, b.size.h - 18),
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

// When the user scrolls to the top of the chat, kick off OP_MESSAGES_BEFORE to
// drop the next 30 older messages into s_msgs. The selection_changed callback
// fires after every cursor move; we only act when row 0 is the new position.
static void selection_changed(struct MenuLayer *m, MenuIndex new_index,
                              MenuIndex old_index, void *callback_context) {
  (void)m; (void)callback_context;
  // Pre-fetch older when the user is approaching the top — by row 2 we want
  // the older batch ready so they don't see a stall when they push past row 0.
  // (MenuLayer doesn't always fire a callback at row 0 itself.)
  if (s_state == ST_READY && new_index.row <= 2 && new_index.row < old_index.row) {
    fetch_older();
  }
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

  s_msgs = (Msg*)malloc(sizeof(Msg) * WC_MAX_MSGS);
  if (!s_msgs) { s_state = ST_ERROR; s_err_code = 3; }
  s_menu = menu_layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, b.size.w, b.size.h - STATUS_BAR_LAYER_HEIGHT));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows         = get_num_rows,
    .get_cell_height      = get_cell_height,
    .draw_row             = draw_row,
    .select_click         = select_click,
    .select_long_click    = select_long_click,
    .selection_changed    = selection_changed,
  });

  GColor bg = wc_theme_bg(s_settings);
  menu_layer_set_normal_colors(s_menu, bg, wc_theme_fg(s_settings));
  menu_layer_set_highlight_colors(s_menu, s_settings->accent, GColorWhite);   // visible selection

  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  snprintf(s_title, sizeof(s_title), "#%s", s_channel_name);
  s_titlebar = wc_titlebar_create(root, b, s_title, s_settings);

  start_fetch();
  schedule_poll();    // ambient refresh while the channel is open (poll_seconds; 0 = off)
}

static void window_unload(Window *w) {
  (void)w;
  if (s_poll) { app_timer_cancel(s_poll); s_poll = NULL; }
  wc_rows_cancel();   // drop any in-flight fetch -> no stale callback into this window
  s_fetching = false;
  menu_layer_destroy(s_menu); s_menu = NULL;
  text_layer_destroy(s_titlebar); s_titlebar = NULL;
  window_destroy(s_window); s_window = NULL;
  free(s_msgs); s_msgs = NULL;
  s_count = 0;
}

// ── public interface ──────────────────────────────────────────────────────────

void chat_view_refresh(void) {
  if (s_menu) do_fetch(false);   // silent — the just-sent message appends without a Loading flash
}

// ── public entry point ────────────────────────────────────────────────────────

void chat_view_window_push(WristcordSettings *settings, const char *channel_id, const char *channel_name) {
  s_settings = settings;
  strncpy(s_channel_id, channel_id ? channel_id : "", sizeof(s_channel_id) - 1);
  s_channel_id[sizeof(s_channel_id) - 1] = '\0';
  wc_utf8_copy(s_channel_name, channel_name ? channel_name : "", sizeof(s_channel_name));
  s_acked_id[0] = '\0';
  s_loading_older = false;
  s_at_oldest = false;

  s_window = window_create();
  window_set_background_color(s_window, wc_theme_bg(settings));
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}
