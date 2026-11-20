#pragma once
#include "RecoveryPolicy.h"
#include <esp_attr.h>

// The monitor never acquires the report mutex or performs network/flash I/O.
struct RecoverySnapshot {
  RecoverySettings settings;
  RecoveryState state;
};
static RecoverySnapshot recovery;
static portMUX_TYPE recoveryMux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t recoveryTask = nullptr; // Created on the Arduino loop task only.
struct RecoveryBootRecord {
  uint32_t magic, reason, restarts, pending, checksum;
};
RTC_NOINIT_ATTR static RecoveryBootRecord recoveryBoot;
constexpr uint32_t RecoveryMagic = 0x57545231;
static uint32_t recoveryChecksum(const RecoveryBootRecord &r) {
  return RecoveryMagic ^ r.reason ^ r.restarts ^ r.pending ^ 0xB34791AC;
}
static bool validRecoveryBoot() {
  return recoveryBoot.magic == RecoveryMagic && recoveryBoot.reason <= 2 &&
         recoveryBoot.restarts <= RecoveryRestartLimit && recoveryBoot.pending <= 1 &&
         recoveryBoot.checksum == recoveryChecksum(recoveryBoot);
}
// Caller holds recoveryMux, or is in setup before the monitor exists.
static void storeRecoveryBoot(RecoveryFault reason, uint32_t restarts, bool pending) {
  recoveryBoot.magic = 0;
  recoveryBoot.reason = static_cast<uint32_t>(reason);
  recoveryBoot.restarts = restarts;
  recoveryBoot.pending = pending ? 1 : 0;
  recoveryBoot.checksum = recoveryChecksum(recoveryBoot);
  recoveryBoot.magic = RecoveryMagic;
}
static RecoverySnapshot recoverySnapshot() {
  portENTER_CRITICAL(&recoveryMux);
  RecoverySnapshot copy = recovery;
  portEXIT_CRITICAL(&recoveryMux);
  return copy;
}
static void recoveryLoopBeat() {
  if (!recoveryTask) return;
  portENTER_CRITICAL(&recoveryMux);
  recovery.state.loopAt = millis();
  portEXIT_CRITICAL(&recoveryMux);
}
class RecoverySSHScope {
public:
  RecoverySSHScope() {
    portENTER_CRITICAL(&recoveryMux);
    recovery.state.sshAt = millis();
    recovery.state.sshBusy = true;
    portEXIT_CRITICAL(&recoveryMux);
  }
  ~RecoverySSHScope() {
    portENTER_CRITICAL(&recoveryMux);
    recovery.state.sshBusy = false;
    portEXIT_CRITICAL(&recoveryMux);
  }
  RecoverySSHScope(const RecoverySSHScope &) = delete;
  RecoverySSHScope &operator=(const RecoverySSHScope &) = delete;
};
static void recoveryTick() {
  bool restart = false;
  portENTER_CRITICAL(&recoveryMux);
  // Capture time and heartbeats together: a newer heartbeat must not look like a wraparound.
  const uint32_t now = millis();
  if (recovery.settings.enabled && recovery.state.ready && !recovery.state.paused) {
    const RecoveryFault fault = recoveryFault(recovery.settings, recovery.state, now);
    if (fault != RecoveryFault::None) {
      storeRecoveryBoot(fault, recovery.state.restarts, true);
      recovery.state.paused = true;
      restart = true;
    } else if (recovery.state.restarts && uint32_t(now - recovery.state.armedAt) >= RecoveryHealthyMs) {
      recovery.state.restarts = 0;
      storeRecoveryBoot(recovery.state.last, 0, false);
    }
  }
  portEXIT_CRITICAL(&recoveryMux);
  if (restart) ESP.restart();
}
static void recoveryWorker(void *) {
  for (;;) {
    recoveryTick();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
static bool ensureRecoveryTask() {
  if (recoveryTask) return true;
  return xTaskCreate(recoveryWorker, "system-recovery", 4096, nullptr, 1, &recoveryTask) == pdPASS;
}
static bool applyRecoverySettings(const RecoverySettings &settings) {
  const bool ready = !settings.enabled || ensureRecoveryTask();
  portENTER_CRITICAL(&recoveryMux);
  recovery.settings = settings;
  recovery.state.ready = ready;
  recovery.state.paused = false;
  recovery.state.armedAt = recovery.state.loopAt = millis();
  recovery.state.restarts = 0;
  storeRecoveryBoot(recovery.state.last, 0, false);
  portEXIT_CRITICAL(&recoveryMux);
  return ready;
}
static void setupRecovery() {
  RecoverySettings saved;
  if (prefs.getBytesLength("recovery-v1") == sizeof(saved)) {
    prefs.getBytes("recovery-v1", &saved, sizeof(saved));
    if (!validRecoverySettings(saved)) saved = RecoverySettings();
  }
  const bool retained = validRecoveryBoot() && esp_reset_reason() != ESP_RST_POWERON &&
                        esp_reset_reason() != ESP_RST_BROWNOUT;
  recovery.settings = saved;
  recovery.state = RecoveryState();
  if (retained) {
    recovery.state.last = static_cast<RecoveryFault>(recoveryBoot.reason);
    if (recoveryBoot.pending && esp_reset_reason() == ESP_RST_SW)
      recovery.state.restarts = recoveryBoot.restarts + 1;
  }
  recovery.state.paused = recovery.state.restarts >= RecoveryRestartLimit;
  recovery.state.armedAt = recovery.state.loopAt = millis();
  storeRecoveryBoot(recovery.state.last, recovery.state.restarts, false);
  if (saved.enabled && !recovery.state.paused) {
    const bool ready = ensureRecoveryTask();
    portENTER_CRITICAL(&recoveryMux);
    recovery.state.ready = ready;
    portEXIT_CRITICAL(&recoveryMux);
  }
}
