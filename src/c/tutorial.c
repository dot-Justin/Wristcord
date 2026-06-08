// src/c/tutorial.c
// Paged first-run tutorial for Wristcord.
#include "tutorial.h"
#include "ui_util.h"

#define NUM_CARDS 4

// Card content
static const char * const s_titles[NUM_CARDS] = {
  "Wristcord",
  "Move around",
  "Send",
  "Actions",
};
static const char * const s_bodies[NUM_CARDS] = {
  "Your Discord, on your wrist.",
  "UP / DOWN to scroll.\nSELECT to open.\nBACK to go back.",
  "In a channel, click SELECT (or tap) to speak a message.",
  "Long-press a message for options (read full).\nImages show as [image].",
};

// Module state — single static tutorial window
static Window          *s_window;
static WristcordSettings *s_settings;
static int              s_card;

// Layers
static BitmapLayer     *s_logo_layer;
static GBitmap         *s_logo_bmp;
static TextLayer       *s_title_layer;
static TextLayer       *s_body_layer;
static TextLayer       *s_footer_layer;
static TextLayer       *s_page_layer;

static char s_footer_buf[20];
static char s_page_buf[16];  // GCC static-range worst case < 16

// ── helpers ───────────────────────────────────────────────────────────────────

static void finish(void) {
  persist_write_bool(PK_TUTORIAL_DONE, true);
  window_stack_remove(s_window, true);
}

static void update_card(void) {
  bool is_first = (s_card == 0);

  // Logo: only on card 0
  layer_set_hidden(bitmap_layer_get_layer(s_logo_layer), !is_first);

  // Title
  text_layer_set_text(s_title_layer, s_titles[s_card]);

  // Body
  text_layer_set_text(s_body_layer, s_bodies[s_card]);

  // Page indicator
  /* s_card is 0..NUM_CARDS-1 so output is always short (e.g. "1/4") */
  snprintf(s_page_buf, sizeof(s_page_buf), "%d/%d", (int)(s_card + 1), (int)NUM_CARDS);
  text_layer_set_text(s_page_layer, s_page_buf);

  // Footer hint
  bool last = (s_card == NUM_CARDS - 1);
  snprintf(s_footer_buf, sizeof(s_footer_buf), last ? "SELECT \xe2\x96\xb8 done" : "SELECT \xe2\x96\xb8 next");
  text_layer_set_text(s_footer_layer, s_footer_buf);
}

// ── click handlers ────────────────────────────────────────────────────────────

static void click_select(ClickRecognizerRef r, void *ctx) {
  (void)r; (void)ctx;
  if (s_card < NUM_CARDS - 1) {
    s_card++;
    update_card();
  } else {
    finish();
  }
}

static void click_down(ClickRecognizerRef r, void *ctx) {
  (void)r; (void)ctx;
  if (s_card < NUM_CARDS - 1) {
    s_card++;
    update_card();
  }
}

static void click_up(ClickRecognizerRef r, void *ctx) {
  (void)r; (void)ctx;
  if (s_card > 0) {
    s_card--;
    update_card();
  }
}

static void click_back(ClickRecognizerRef r, void *ctx) {
  (void)r; (void)ctx;
  finish();
}

static void click_config_provider(void *ctx) {
  (void)ctx;
  window_single_click_subscribe(BUTTON_ID_SELECT, click_select);
  window_single_click_subscribe(BUTTON_ID_DOWN,   click_down);
  window_single_click_subscribe(BUTTON_ID_UP,     click_up);
  window_single_click_subscribe(BUTTON_ID_BACK,   click_back);
}

// ── window lifecycle ──────────────────────────────────────────────────────────

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);

  GColor fg = wc_theme_fg(s_settings);
  GColor accent = s_settings->accent;

  // ── Discord logo (card 0 only, 80x80, centred near top) ──────────────────
  int logo_w = 64, logo_h = 64;
  int logo_x = (b.size.w - logo_w) / 2;
  int logo_y = 18;
  s_logo_bmp = gbitmap_create_with_resource(RESOURCE_ID_DISCORD_LOGO);
  s_logo_layer = bitmap_layer_create(GRect(logo_x, logo_y, logo_w, logo_h));
  bitmap_layer_set_bitmap(s_logo_layer, s_logo_bmp);
  bitmap_layer_set_compositing_mode(s_logo_layer, GCompOpSet);
  bitmap_layer_set_background_color(s_logo_layer, GColorClear);
  layer_add_child(root, bitmap_layer_get_layer(s_logo_layer));

  // ── Title ─────────────────────────────────────────────────────────────────
  // When logo is shown (card 0): title sits below logo (~logo_y + logo_h + 4)
  // When no logo: title starts higher (card 1+). We position for card 0;
  // update_card() will reposition implicitly via static layout — title y is
  // fixed; body has enough room on all cards because card 0 body is short.
  int title_y = logo_y + logo_h + 4;  // 86 on 144px wide Emery (180px tall)
  s_title_layer = text_layer_create(GRect(4, title_y, b.size.w - 8, 22));
  text_layer_set_background_color(s_title_layer, GColorClear);
  text_layer_set_text_color(s_title_layer, accent);
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_title_layer));

  // ── Body ─────────────────────────────────────────────────────────────────
  int body_y = title_y + 24;
  int footer_h = 16;
  int body_h = b.size.h - body_y - footer_h - 4;
  s_body_layer = text_layer_create(GRect(6, body_y, b.size.w - 12, body_h));
  text_layer_set_background_color(s_body_layer, GColorClear);
  text_layer_set_text_color(s_body_layer, fg);
  text_layer_set_font(s_body_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_body_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_body_layer, GTextOverflowModeWordWrap);
  layer_add_child(root, text_layer_get_layer(s_body_layer));

  // ── Footer hint (bottom-left) ─────────────────────────────────────────────
  int footer_y = b.size.h - footer_h - 2;
  s_footer_layer = text_layer_create(GRect(4, footer_y, b.size.w - 50, footer_h));
  text_layer_set_background_color(s_footer_layer, GColorClear);
  text_layer_set_text_color(s_footer_layer, wc_theme_muted(s_settings));
  text_layer_set_font(s_footer_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_footer_layer, GTextAlignmentLeft);
  layer_add_child(root, text_layer_get_layer(s_footer_layer));

  // ── Page counter (bottom-right) ───────────────────────────────────────────
  s_page_layer = text_layer_create(GRect(b.size.w - 46, footer_y, 42, footer_h));
  text_layer_set_background_color(s_page_layer, GColorClear);
  text_layer_set_text_color(s_page_layer, wc_theme_muted(s_settings));
  text_layer_set_font(s_page_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_page_layer, GTextAlignmentRight);
  layer_add_child(root, text_layer_get_layer(s_page_layer));

  // Populate first card
  update_card();

  // Click config
  window_set_click_config_provider(w, click_config_provider);
}

static void window_unload(Window *w) {
  (void)w;
  persist_write_bool(PK_TUTORIAL_DONE, true);

  text_layer_destroy(s_page_layer);    s_page_layer    = NULL;
  text_layer_destroy(s_footer_layer);  s_footer_layer  = NULL;
  text_layer_destroy(s_body_layer);    s_body_layer    = NULL;
  text_layer_destroy(s_title_layer);   s_title_layer   = NULL;
  bitmap_layer_destroy(s_logo_layer);  s_logo_layer    = NULL;
  gbitmap_destroy(s_logo_bmp);         s_logo_bmp      = NULL;
  window_destroy(s_window);            s_window        = NULL;
}

// ── public entry point ────────────────────────────────────────────────────────

void tutorial_window_push(WristcordSettings *settings) {
  s_settings = settings;
  s_card = 0;

  if (s_window) {
    // Already open — just bring to front
    window_stack_push(s_window, true);
    return;
  }

  s_window = window_create();
  window_set_background_color(s_window, wc_theme_bg(settings));
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}
