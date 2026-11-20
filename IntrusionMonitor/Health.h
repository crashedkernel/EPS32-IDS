#pragma once
#include <esp_partition.h>

// Runtime counters include both cores. Sample on the Arduino loop task.
static float cpuUse = -1;
static uint32_t healthAt = 0;
static uint32_t previousRuntime = 0, previousIdle = 0;
static void sampleHealth() {
  if (millis() - healthAt < 2000)
    return;
  healthAt = millis();
  // Reuse storage on each sample instead of fragmenting the internal heap.
  static TaskStatus_t tasks[64];
  if (uxTaskGetNumberOfTasks() > 64)
    return;
  uint32_t total = 0, idle = 0;
  const unsigned count = uxTaskGetSystemState(tasks, 64, &total);
  for (unsigned i = 0; i < count; ++i)
    if (!strncmp(tasks[i].pcTaskName, "IDLE", 4))
      idle += tasks[i].ulRunTimeCounter;
  if (!count)
    return;
  const uint32_t elapsed = total - previousRuntime;
  if (previousRuntime && elapsed)
    cpuUse = constrain(100.0f * (1.0f - float(uint32_t(idle - previousIdle)) /
                                            (float(elapsed) * portNUM_PROCESSORS)),
                       0.0f, 100.0f);
  previousRuntime = total;
  previousIdle = idle;
}
static void healthStatus() {
  if (!adminRequest())
    return;
  const esp_partition_t *storage =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
  nvs_stats_t stats{};
  const bool storageKnown =
      storage && nvs_get_stats("nvs", &stats) == ESP_OK && stats.total_entries;
  server.send(
      200, "application/json",
      String("{\"cpu\":") + cpuUse + ",\"clockMHz\":" + ESP.getCpuFreqMHz() +
          ",\"heapTotal\":" + ESP.getHeapSize() + ",\"heapFree\":" + ESP.getFreeHeap() +
          ",\"heapMinimum\":" + ESP.getMinFreeHeap() +
          ",\"heapLargest\":" + heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) +
          ",\"flashTotal\":" + ESP.getFlashChipSize() + ",\"firmwareUsed\":" + ESP.getSketchSize() +
          ",\"storageTotal\":" + (storageKnown ? storage->size : 0) + ",\"storageUsedPercent\":" +
          (storageKnown ? 100.0f * stats.used_entries / stats.total_entries : -1) + "}");
}
