// src/c/viewstats.h
// Tracks how often the user opens each guild from the watch. Each open bumps a
// monotonic per-guild counter stored in persist. Used by server_list to sort
// guilds by "most used" — the user's actual usage pattern, not Discord-wide
// activity.
//
// Keys live in the 0x80000000+hash range so they can't collide with the small
// PK_* ints settings.c uses or readstate.c's 0x40000000+ namespace.
#pragma once
#include <pebble.h>

// Record that the user just opened this guild. Cheap; safe to call from
// channel_list_window_push.
void wc_viewstats_bump_guild(const char *guild_id);

// Return the recorded open-count for `guild_id`. 0 if never opened.
int32_t wc_viewstats_guild(const char *guild_id);
