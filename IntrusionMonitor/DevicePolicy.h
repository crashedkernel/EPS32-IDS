#pragma once

enum Policy : uint8_t { Unknown, Allowed, Acknowledged };
static const char *const policyNames[] = {"Unauthorized", "Authorized", "Acknowledged"};
static Preferences policyStore;
constexpr uint32_t defaultAckMinutes = 15;
static uint32_t ackMinutes = defaultAckMinutes;

static uint32_t uptimeSeconds() { return esp_timer_get_time() / 1000000ULL; }

static Policy effectivePolicy(const Device &device, uint32_t now) {
  if (device.policy == Unknown && device.ackUntil > now)
    return Acknowledged;
  return Policy(device.policy);
}
static bool macKey(const char *mac, char key[13]) {
  if (strlen(mac) != 17)
    return false;
  for (unsigned i = 0; i < 6; i++) {
    if (!isxdigit(static_cast<unsigned char>(mac[3 * i])) ||
        !isxdigit(static_cast<unsigned char>(mac[3 * i + 1])) || (i < 5 && mac[3 * i + 2] != ':'))
      return false;
    key[2 * i] = tolower(static_cast<unsigned char>(mac[3 * i]));
    key[2 * i + 1] = tolower(static_cast<unsigned char>(mac[3 * i + 1]));
  }
  key[12] = 0;
  const char *hex = "0123456789abcdef";
  return strcmp(key, "000000000000") != 0 && ((strchr(hex, key[1]) - hex) % 2 == 0);
}

static bool sameDevice(const Device &a, const Device &b) {
  if (a.mac[0] || b.mac[0])
    return a.mac[0] && b.mac[0] && !strcasecmp(a.mac, b.mac);
  return a.ip && a.ip == b.ip;
}

static Policy storedPolicy(const char *mac) {
  char key[13];
  if (!macKey(mac, key) || !policyStore.isKey(key))
    return Unknown;
  uint8_t value = policyStore.getUChar(key, Unknown);
  return value == Allowed ? Allowed : Unknown;
}

// Caller holds the report mutex.
static bool setPolicy(const String &mac, Policy policy) {
  char key[13];
  if (!macKey(mac.c_str(), key))
    return false;
  if (policy == Acknowledged) {
    for (unsigned i = 0; i < deviceCount; i++) {
      Device &device = devices[i];
      if (!strcasecmp(device.mac, mac.c_str()) && device.online && device.policy == Unknown) {
        device.ackUntil = uptimeSeconds() + ackMinutes * 60;
        return true;
      }
    }
    return false;
  }
  bool saved = policy == Unknown ? (!policyStore.isKey(key) || policyStore.remove(key))
                                 : policyStore.putUChar(key, policy) == sizeof(uint8_t);
  if (saved) {
    for (unsigned i = 0; i < deviceCount; i++)
      if (!strcasecmp(devices[i].mac, mac.c_str())) {
        devices[i].policy = policy;
        devices[i].ackUntil = 0;
      }
  }
  return saved;
}

static unsigned acknowledgeOnlineDevices() {
  unsigned count = 0;
  xSemaphoreTake(lock, portMAX_DELAY);
  const uint32_t until = uptimeSeconds() + ackMinutes * 60;
  for (unsigned i = 0; i < deviceCount; ++i) {
    if (devices[i].online && devices[i].policy == Unknown) {
      devices[i].ackUntil = until;
      ++count;
    }
  }
  xSemaphoreGive(lock);
  return count;
}

static void updateDevice(Device &device, const Device *previous, uint32_t now, uint32_t epoch) {
  const bool wasOnline = previous && previous->online;
  device.changed = previous && wasOnline != device.online;
  device.isNew = !previous && device.online;
  device.policy = previous ? previous->policy : storedPolicy(device.mac);
  device.ackUntil = previous ? previous->ackUntil : 0;
  device.connectedAt = previous ? previous->connectedAt : 0;
  device.lastSeen = previous ? previous->lastSeen : 0;
  device.lastSeenEpoch = previous ? previous->lastSeenEpoch : 0;
  if (previous && previous->hasRouterSession && (!device.online || wasOnline) &&
      (!device.online || !device.hasRouterSession)) {
    device.hasRouterSession = true;
    device.routerSession = previous->routerSession;
    if (device.online)
      device.routerSession += now - previous->lastSeen;
  }
  if (device.online) {
    if (!wasOnline)
      device.connectedAt = now;
    device.lastSeen = now;
    device.lastSeenEpoch = epoch;
  }
}
