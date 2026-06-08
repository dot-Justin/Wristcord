// src/c/channel_list.h
#pragma once
#include <pebble.h>
#include "settings.h"

void channel_list_window_push(WristcordSettings *settings, const char *guild_id, const char *guild_name);
