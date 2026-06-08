// src/c/tutorial.c
// Paged first-run tutorial for Wristcord.
#include "tutorial.h"
#include "ui_util.h"

#define NUM_CARDS 4

// Cards 1..3 (card 0 is special-cased: logo + welcome).
static const char * const s_titles[NUM_CARDS] = {
  "Wristcord!",          // card 0 (shown in accent, under the logo)
  "Move around",
  "Send",
  "Actions",
};
static const char * const s_bodies[NUM_CARDS] = {
  "Press SELECT to get started.",
  "UP / DOWN to scroll.\nSELECT to open.\nBACK to go back.",
  "In a channel, click SELECT (or tap) to speak a message.",
  "Long-press a message for options (read full).\nImages show as [image].",
};

static Window           *s_window;
static WristcordSettings *s_settings;
static int               s_card;
static int               s_w, s_h;

static BitmapLayer *s_logo_layer;
static GBitmap     *s_logo_bmp;
static TextLayer   *s_welcome_layer;  // "Welcome to" (card 0 only)
static TextLayer   *s_title_layer;
static TextLayer   *s_body_layer;
static TextLayer   *s_footer_layer;
static TextLayer   *s_page_layer;

static char s_footer_buf[20];
static char s_page_buf[12];

static void finish(void) {
  persist_write_bool(PK_TUTORIAL_DONE, true);
  window_stack_remove(s_window, true);
}

static void update_card(void) {
  bool first = (s_card == 0);

  layer_set_hidden(bitmap_layer_get_layer(s_logo_layer), !first);
  layer_set_hidden(text_layer_get_layer(s_welcome_layer), !first);

  if (first) {
    layer_set_frame(text_layer_get_layer(s_title_layer), GRect(4, 100, s_w - 8, 32));
    layer_set_frame(text_layer_get_layer(s_body_layer),  GRect(10, 142, s_w - 20, s_h - 142 - 22));
  } else {
    layer_set_frame(text_layer_get_layer(s_title_layer), GRect(4, 34, s_w - 8, 60));
    layer_set_frame(text_layer_get_layer(s_body_layer),  GRect(10, 96, s_w - 20, s_h - 96 - 22));
  }

  text_layer_set_text(s_title_layer, s_titles[s_card]);
  text_layer_set_text(s_body_layer, s_bodies[s_card]);

  snprintf(s_page_buf, sizeof(s_page_buf), "%d/%d", s_card + 1, NUM_CARDS);
  text_layer_set_text(s_page_layer, s_page_buf);

  bool last = (s_card == NUM_CARDS - 1);
  snprintf(s_footer_buf, sizeof(s_footer_buf), last ? "SELECT = done" : "SELECT = next");
  text_layer_set_text(s_footer_layer, s_footer_buf);
}

static void click_select(ClickRecognizerRef r, void *ctx) {
  (void)r; (void)ctx;
  if (s_card < NUM_CARDS - 1) { s_card++; update_card(); } else { finish(); }
}
static void click_down(ClickRecognizerRef r, void *ctx) {
  (void)r; (void)ctx;
  if (s_card < NUM_CARDS - 1) { s_card++; update_card(); }
}
static void click_up(ClickRecognizerRef r, void *ctx) {
  (void)r; (void)ctx;
  if (s_card > 0) { s_card--; update_card(); }
}
static void click_back(ClickRecognizerRef r, void *ctx) {
  (void)r; (void)ctx; finish();
}
static void click_config_provider(void *ctx) {
  (void)ctx;
  window_single_click_subscribe(BUTTON_ID_SELECT, click_select);
  window_single_click_subscribe(BUTTON_ID_DOWN,   click_down);
  window_single_click_subscribe(BUTTON_ID_UP,     click_up);
  window_single_click_subscribe(BUTTON_ID_BACK,   click_back);
}

static TextLayer *mk_text(Layer *root, GRect f, GColor color, const char *font, GTextAlignment al) {
  TextLayer *t = text_layer_create(f);
  text_layer_set_background_color(t, GColorClear);
  text_layer_set_text_color(t, color);
  text_layer_set_font(t, fonts_get_system_font(font));
  text_layer_set_text_alignment(t, al);
  text_layer_set_overflow_mode(t, GTextOverflowModeWordWrap);
  layer_add_child(root, text_layer_get_layer(t));
  return t;
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_w = b.size.w; s_h = b.size.h;
  GColor fg = wc_theme_fg(s_settings);
  GColor accent = s_settings->accent;
  GColor muted = wc_theme_muted(s_settings);

  // Logo (card 0): layer matches the 56x56 bitmap exactly so it isn't clipped.
  int lw = 56, lh = 56;
  s_logo_bmp = gbitmap_create_with_resource(RESOURCE_ID_DISCORD_LOGO);
  s_logo_layer = bitmap_layer_create(GRect((b.size.w - lw) / 2, 16, lw, lh));
  bitmap_layer_set_bitmap(s_logo_layer, s_logo_bmp);
  bitmap_layer_set_compositing_mode(s_logo_layer, GCompOpSet);
  bitmap_layer_set_background_color(s_logo_layer, GColorClear);
  layer_add_child(root, bitmap_layer_get_layer(s_logo_layer));

  s_welcome_layer = mk_text(root, GRect(4, 78, b.size.w - 8, 22), fg, FONT_KEY_GOTHIC_18, GTextAlignmentCenter);
  text_layer_set_text(s_welcome_layer, "Welcome to");

  s_title_layer = mk_text(root, GRect(4, 100, b.size.w - 8, 32), accent, FONT_KEY_GOTHIC_28_BOLD, GTextAlignmentCenter);
  s_body_layer  = mk_text(root, GRect(10, 142, b.size.w - 20, 60), fg, FONT_KEY_GOTHIC_18, GTextAlignmentCenter);

  int footer_y = b.size.h - 18;
  s_footer_layer = mk_text(root, GRect(4, footer_y, b.size.w - 50, 16), muted, FONT_KEY_GOTHIC_14, GTextAlignmentLeft);
  s_page_layer   = mk_text(root, GRect(b.size.w - 46, footer_y, 42, 16), muted, FONT_KEY_GOTHIC_14, GTextAlignmentRight);

  update_card();
  window_set_click_config_provider(w, click_config_provider);
}

static void window_unload(Window *w) {
  (void)w;
  persist_write_bool(PK_TUTORIAL_DONE, true);
  text_layer_destroy(s_page_layer);    s_page_layer = NULL;
  text_layer_destroy(s_footer_layer);  s_footer_layer = NULL;
  text_layer_destroy(s_body_layer);    s_body_layer = NULL;
  text_layer_destroy(s_title_layer);   s_title_layer = NULL;
  text_layer_destroy(s_welcome_layer); s_welcome_layer = NULL;
  bitmap_layer_destroy(s_logo_layer);  s_logo_layer = NULL;
  gbitmap_destroy(s_logo_bmp);         s_logo_bmp = NULL;
  window_destroy(s_window);            s_window = NULL;
}

void tutorial_window_push(WristcordSettings *settings) {
  s_settings = settings;
  s_card = 0;
  if (s_window) { window_stack_push(s_window, true); return; }
  s_window = window_create();
  window_set_background_color(s_window, wc_theme_bg(settings));
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);
}
