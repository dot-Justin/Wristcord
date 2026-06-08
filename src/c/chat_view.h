// src/c/chat_view.h
#pragma once
#include <pebble.h>
#include "settings.h"

void chat_view_window_push(WristcordSettings *settings, const char *channel_id, const char *channel_name);
