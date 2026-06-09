// src/c/all_servers.h
// Full server list with folder support — what server_list.c was through v1.1.
// Pushed from the home page when the user picks "Show all servers".
#pragma once
#include <pebble.h>
#include "settings.h"

void all_servers_window_push(WristcordSettings *settings);
