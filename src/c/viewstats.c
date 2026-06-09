// src/c/viewstats.c — per-guild "how often did the user open this" counters.
#include "viewstats.h"

static uint32_t key_for(const char *guild_id) {
  // djb2 hash. Truncate to 30 bits then OR in the high prefix so the key
  // lives in the 0x80000000..0xBFFFFFFF range — disjoint from settings (small
  // ints) and readstate (0x40000000..0x7FFFFFFF).
  uint32_t h = 5381;
  const unsigned char *p = (const unsigned char*)guild_id;
  while (*p) { h = ((h << 5) + h) + *p; p++; }
  return 0x80000000u | (h & 0x3FFFFFFFu);
}

void wc_viewstats_bump_guild(const char *guild_id) {
  if (!guild_id || !guild_id[0]) return;
  uint32_t k = key_for(guild_id);
  int32_t cur = persist_exists(k) ? persist_read_int(k) : 0;
  if (cur < 0) cur = 0;
  // Cap so a single guild can never wrap or dominate forever.
  if (cur < 1000000) cur += 1;
  persist_write_int(k, cur);
}

int32_t wc_viewstats_guild(const char *guild_id) {
  if (!guild_id || !guild_id[0]) return 0;
  uint32_t k = key_for(guild_id);
  if (!persist_exists(k)) return 0;
  int32_t v = persist_read_int(k);
  return v < 0 ? 0 : v;
}
