#pragma once
#include <pebble.h>
// Mark a channel read up to its newest message id (call when its chat opens).
void wc_readstate_mark(const char *channel_id, const char *newest_msg_id);
// True if last_message_id is newer than the stored "seen" id for this channel
// AND we have a stored baseline (never-opened channels return false = read).
bool wc_readstate_is_unread(const char *channel_id, const char *last_message_id);
// snowflake "greater than": longer string = newer; same length = lexicographic.
bool wc_snowflake_gt(const char *a, const char *b);
