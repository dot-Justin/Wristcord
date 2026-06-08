// src/c/compose.c  — STUB; M6-T3 will implement fully (dictation + send)
#include "compose.h"

void chat_compose_start(WristcordSettings *settings, const char *channel_id, const char *channel_name) {
  (void)settings; (void)channel_name;
  APP_LOG(APP_LOG_LEVEL_INFO, "compose in channel %s (dictation in M6-T3)", channel_id ? channel_id : "");
}
