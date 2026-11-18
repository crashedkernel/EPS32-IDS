#pragma once
#include <stdint.h>
#include <stddef.h>
#include <new>
#include <esp_heap_caps.h>
struct Device {
  // Large inventory arrays belong in PSRAM, preserving internal RAM for Wi-Fi and SSH.
  static void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    void *memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory ? memory : heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  }
  static void operator delete[](void *memory) noexcept { heap_caps_free(memory); }
  static void operator delete[](void *memory, const std::nothrow_t &) noexcept {
    heap_caps_free(memory);
  }
  uint32_t ip = 0;
  char name[96] = {0};
  char hostname[96] = {0};
  char mac[18] = {0};
  bool online = false, changed = false, isNew = false, reported = false;
  uint32_t seen = 0, connectedAt = 0, lastSeen = 0, lastSeenEpoch = 0;
  uint32_t dnsCheckedAt = 0;
  uint32_t routerSession = 0;
  bool hasRouterSession = false;
  uint32_t ackUntil = 0;
  uint8_t policy = 0;
  char connection[32] = {0};
};
