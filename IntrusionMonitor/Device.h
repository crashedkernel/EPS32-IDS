#pragma once
#include <stdint.h>
struct Device {
  uint32_t ip = 0;
  char name[96] = {0};
  char hostname[96] = {0};
  char mac[18] = {0};
  bool online = false, changed = false, isNew = false, reported = false;
  uint32_t seen = 0, connectedAt = 0, lastSeen = 0, lastSeenEpoch = 0;
  uint32_t routerSession = 0;
  bool hasRouterSession = false;
  uint32_t ackUntil = 0;
  uint8_t policy = 0;
  char connection[32] = {0};
};
