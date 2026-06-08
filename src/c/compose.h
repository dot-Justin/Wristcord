#pragma once
#include <pebble.h>
#include "settings.h"

void chat_compose_start(WristcordSettings *settings, const char *channel_id, const char *channel_name);
void wc_compose_handle_inbox(DictionaryIterator *it);
