#pragma once
#include <stdint.h>

constexpr uint32_t RecoveryGraceMs = 120000;
constexpr uint32_t RecoveryHealthyMs = 300000;
constexpr uint32_t RecoveryRestartLimit = 3;
enum class RecoveryFault : uint32_t { None, MainLoop, SSH };
struct RecoverySettings {
  uint32_t version = 1;
  uint32_t enabled = 0;
  uint32_t loopSeconds = 60;
  uint32_t sshSeconds = 120;
};
struct RecoveryState {
  bool ready = false, paused = false, sshBusy = false;
  uint32_t loopAt = 0, sshAt = 0, armedAt = 0;
  uint32_t restarts = 0;
  RecoveryFault last = RecoveryFault::None;
};
static bool validRecoverySettings(const RecoverySettings &s) {
  return s.version == 1 && s.enabled <= 1 && s.loopSeconds >= 30 && s.loopSeconds <= 300 &&
         s.sshSeconds >= 60 && s.sshSeconds <= 900;
}
static RecoveryFault recoveryFault(const RecoverySettings &settings, const RecoveryState &state,
                                    uint32_t now) {
  if (!settings.enabled || !state.ready || state.paused ||
      uint32_t(now - state.armedAt) < RecoveryGraceMs) return RecoveryFault::None;
  if (uint32_t(now - state.loopAt) >= settings.loopSeconds * 1000UL) return RecoveryFault::MainLoop;
  if (state.sshBusy && uint32_t(now - state.sshAt) >= settings.sshSeconds * 1000UL) return RecoveryFault::SSH;
  return RecoveryFault::None;
}
