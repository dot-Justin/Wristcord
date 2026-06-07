// src/c/server_list.h
#pragma once
#include <pebble.h>
#include "settings.h"

void server_list_window_push(WristcordSettings *settings);
void server_list_handle_settings(void);   // call after settings update (re-render / start fetch if newly tokenized)
