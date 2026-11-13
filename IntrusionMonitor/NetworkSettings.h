#pragma once

struct NetworkSettings {
  char hostname[64] = "esp32ids";
  char apName[33] = "Watchtower";
  char apPassword[64] = "Password123";
  bool reservedMacMode = false; // Preserve the stored network-v1 layout.
  bool staticIP = false;
  char ip[16] = "";
  char mask[16] = "";
  char gateway[16] = "";
  char dns[16] = "";
};

static NetworkSettings networkSettings;

static bool validHostname(const String &name) {
  if (!name.length() || name.length() > 63 || name[0] == '-' || name[name.length() - 1] == '-')
    return false;
  for (unsigned i = 0; i < name.length(); ++i) {
    char c = name[i];
    if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
        !(c >= '0' && c <= '9') && c != '-')
      return false;
  }
  return true;
}

static bool parseStaticNetwork(const NetworkSettings &settings, IPAddress &ip, IPAddress &mask,
                               IPAddress &gateway, IPAddress &dns) {
  if (!ip.fromString(settings.ip) || !mask.fromString(settings.mask) ||
      !gateway.fromString(settings.gateway) || !dns.fromString(settings.dns))
    return false;
  const uint32_t netmask = number(mask);
  const uint32_t hostmask = ~netmask;
  if (!netmask || hostmask < 3 || (hostmask & (hostmask + 1)))
    return false;
  const auto unicast = [](const IPAddress &address) {
    return address[0] > 0 && address[0] != 127 && address[0] < 224;
  };
  const uint32_t host = number(ip) & hostmask;
  const uint32_t gatewayHost = number(gateway) & hostmask;
  return unicast(ip) && unicast(gateway) && unicast(dns) &&
         (number(ip) & netmask) == (number(gateway) & netmask) &&
         ip != gateway && host && host != hostmask && gatewayHost && gatewayHost != hostmask;
}
