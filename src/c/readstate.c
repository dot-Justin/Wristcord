#include "readstate.h"
#define SEP '\x1f'
static uint32_t key_for(const char *chan) {
  uint32_t h = 5381; const unsigned char *p = (const unsigned char*)chan;
  while (*p) { h = ((h << 5) + h) + *p; p++; }
  return 0x40000000u | (h & 0x3FFFFFFFu);   // high range, won't collide with PK_THEME(100)/EXPANDED(200)/COLLAPSED_CATS(201)
}
bool wc_snowflake_gt(const char *a, const char *b) {
  if (!a || !a[0]) return false;
  if (!b || !b[0]) return true;
  size_t la = strlen(a), lb = strlen(b);
  if (la != lb) return la > lb;
  return strcmp(a, b) > 0;
}
void wc_readstate_mark(const char *channel_id, const char *newest_msg_id) {
  if (!channel_id || !channel_id[0] || !newest_msg_id || !newest_msg_id[0]) return;
  char val[48];
  snprintf(val, sizeof(val), "%s%c%s", channel_id, SEP, newest_msg_id);  // "chan<SEP>seen"
  persist_write_string(key_for(channel_id), val);
}
// Establish a read baseline the first time we ever see a channel, so that only
// activity *after* this point shows as unread (avoids "everything unread" noise).
void wc_readstate_seed_if_absent(const char *channel_id, const char *last_message_id) {
  if (!channel_id || !channel_id[0] || !last_message_id || !last_message_id[0]) return;
  if (persist_exists(key_for(channel_id))) return;
  wc_readstate_mark(channel_id, last_message_id);
}
bool wc_readstate_is_unread(const char *channel_id, const char *last_message_id) {
  if (!channel_id || !channel_id[0] || !last_message_id || !last_message_id[0]) return false;
  uint32_t k = key_for(channel_id);
  if (!persist_exists(k)) return false;               // never opened -> treat as read
  char val[48];
  if (persist_read_string(k, val, sizeof(val)) <= 0) return false;
  char *sep = strchr(val, SEP);
  if (!sep) return false;
  *sep = '\0';
  const char *stored_chan = val;
  const char *seen = sep + 1;
  if (strcmp(stored_chan, channel_id) != 0) return false;   // hash collision with a different channel -> ignore
  return wc_snowflake_gt(last_message_id, seen);
}
