// src/c/tutorial.h
#pragma once
#include <pebble.h>
#include "settings.h"

#define PK_TUTORIAL_DONE 300

void tutorial_window_push(WristcordSettings *settings);
