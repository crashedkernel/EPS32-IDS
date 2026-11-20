#include <functional>
#include <memory>
#include <cJSON.h>
/* Waveshare ESP32-S3-ETH network monitor. Arduino-ESP32 3.x; LibSSH-ESP32 5.10.0. */
#include "WebAssets.h"
#include "TimeSettings.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <nvs.h>
#include <ETH.h>
#include <esp_netif_net_stack.h>
#include <SPI.h>
#include <WiFiUdp.h>
#include <atomic>
#include <errno.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <fcntl.h>
#include <libssh_esp32.h>
#include <lwip/etharp.h>
#include <lwip/inet.h>
#include <lwip/pbuf.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>
#include <time.h>

constexpr unsigned MAX_HOSTS = 254;
uint32_t scanIntervalSeconds = 60;
WebServer server(80);
Preferences prefs;
String adminPassword, mode, subnet, rangeStart, rangeEnd, csrf;
String adminRealm;
String scanSource = "local", routerIP, routerUser, routerPassword;
String routerTransport = "http";
uint16_t routerPort = 80;
constexpr char defaultPassword[] = "Password123";
bool ethernetStarted = false;
static std::atomic<uint32_t> ethernetLinkGeneration{0};
static bool networkReady() { return ETH.linkUp() && ETH.hasIP(); }
String timeZone = DEFAULT_TIME_ZONE, timeFormat = "24", timeServer = DEFAULT_TIME_SERVER;
bool apRunning = false;
static bool apEnabled = true;
static bool apStartsOnBoot = true;
static uint8_t apChannel = 1, apWidth = 20;
static uint32_t apChangeAt = 0;
static bool apRestartPending = false;
SemaphoreHandle_t lock;
#include "Device.h"
Device *devices = nullptr;
unsigned deviceCount = 0, progress = 0;
bool scanning = false, completed = false, scanRequested = false;
TaskHandle_t scannerTask = nullptr;
std::atomic<bool> clockHasSync{false};
uint64_t lastScanEpoch = 0;
void clockSyncCallback(struct timeval *) { clockHasSync.store(true); }
uint64_t utcNow() {
  time_t now = time(nullptr);
  return clockHasSync.load() && now >= 1700000000 ? uint64_t(now) : 0;
}
std::atomic<uint32_t> nextScanAt{0};
uint32_t lastScan = 0, restartAt = 0, scanNumber = 0;
String scanError;

uint32_t number(IPAddress ip) {
  return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) | (uint32_t(ip[2]) << 8) | ip[3];
}
IPAddress address(uint32_t n) { return IPAddress(n >> 24, n >> 16, n >> 8, n); }
bool parseIP(String s, uint32_t &n) {
  IPAddress a;
  if (!a.fromString(s) || a.type() != IPv4)
    return false;
  n = number(a);
  return true;
}
String escape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  s.replace("'", "&#39;");
  return s;
}
String json(String s) {
  String r = "\"";
  for (unsigned i = 0; i < s.length(); i++) {
    unsigned char c = s[i];
    if (c == '"' || c == '\\') {
      r += '\\';
      r += char(c);
    } else if (c >= 32)
      r += char(c);
    else {
      char code[7];
      snprintf(code, sizeof(code), "\\u%04x", c);
      r += code;
    }
  }
  return r + '"';
}
bool bounds(String m, String cidr, String first, String last, uint32_t &lo, uint32_t &hi) {
  if (m == "auto") {
    uint32_t ip = number(ETH.localIP()), mask = number(ETH.subnetMask());
    lo = (ip & mask) + 1;
    hi = (ip | ~mask) - 1;
  } else if (m == "subnet") {
    int slash = cidr.indexOf('/');
    if (slash < 0)
      return false;
    String bits = cidr.substring(slash + 1);
    if (bits.length() != 2 || bits.toInt() < 24 || bits.toInt() > 30)
      return false;
    uint32_t ip;
    if (!parseIP(cidr.substring(0, slash), ip))
      return false;
    uint32_t mask = 0xffffffffUL << (32 - bits.toInt());
    lo = (ip & mask) + 1;
    hi = (ip | ~mask) - 1;
  } else if (m == "range") {
    if (!parseIP(first, lo) || !parseIP(last, hi))
      return false;
  } else
    return false;
  return lo <= hi && uint64_t(hi) - lo + 1 <= MAX_HOSTS && (lo >> 24) != 0 && (hi >> 24) < 224;
}
uint16_t checksum(const uint8_t *p, size_t n) {
  uint32_t sum = 0;
  while (n > 1) {
    sum += (p[0] << 8) | p[1];
    p += 2;
    n -= 2;
  }
  if (n)
    sum += p[0] << 8;
  while (sum >> 16)
    sum = (sum & 65535) + (sum >> 16);
  return ~sum;
}
bool ping(uint32_t ip) {
  int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (fd < 0)
    return false;
  timeval timeout = {0, 120000};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  uint16_t tag = uint16_t(esp_random());
  uint8_t packet[12] = {8, 0, 0, 0, uint8_t(tag >> 8), uint8_t(tag), 0, 1, 'N', 'M', 'O', 'N'};
  uint16_t c = checksum(packet, sizeof(packet));
  packet[2] = c >> 8;
  packet[3] = c;
  sockaddr_in target = {};
  target.sin_family = AF_INET;
  target.sin_addr.s_addr = htonl(ip);
  bool ok = false;
  if (sendto(fd, packet, sizeof(packet), 0, (sockaddr *)&target, sizeof(target)) ==
      sizeof(packet)) {
    uint32_t start = millis();
    uint8_t buf[256];
    while (millis() - start < 150) {
      sockaddr_in from = {};
      socklen_t len = sizeof(from);
      int n = recvfrom(fd, buf, sizeof(buf), 0, (sockaddr *)&from, &len);
      if (n < 0)
        break;
      if (n < 20)
        continue; // A truncated IPv4 packet has no header to inspect.
      unsigned h = (buf[0] & 15) * 4;
      if (h >= 20 && n >= int(h + 12) && from.sin_addr.s_addr == target.sin_addr.s_addr &&
          buf[h] == 0 && buf[h + 4] == packet[4] && buf[h + 5] == packet[5] && buf[h + 7] == 1) {
        ok = true;
        break;
      }
    }
  }
  close(fd);
  return ok;
}
// Observe fresh ARP replies before forwarding every frame to the normal stack.
// A previous scan's ARP cache entry must never be counted as current evidence.
portMUX_TYPE arpMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t arpTarget = 0, arpLocal = 0;
bool arpActive = false, arpAnswered = false;
uint8_t arpMac[6] = {};
err_t monitorInput(struct pbuf *p, struct netif *n) {
  uint8_t frame[42];
  if (pbuf_copy_partial(p, frame, sizeof(frame), 0) == sizeof(frame) && frame[12] == 8 &&
      frame[13] == 6 && frame[14] == 0 && frame[15] == 1 && frame[16] == 8 && frame[17] == 0 &&
      frame[18] == 6 && frame[19] == 4 && frame[20] == 0 && frame[21] == 2) {
    uint32_t sender = (uint32_t(frame[28]) << 24) | (uint32_t(frame[29]) << 16) |
                      (uint32_t(frame[30]) << 8) | frame[31];
    uint32_t target = (uint32_t(frame[38]) << 24) | (uint32_t(frame[39]) << 16) |
                      (uint32_t(frame[40]) << 8) | frame[41];
    portENTER_CRITICAL(&arpMux);
    if (arpActive && sender == arpTarget && target == arpLocal) {
      memcpy(arpMac, frame + 22, 6);
      arpAnswered = true;
    }
    portEXIT_CRITICAL(&arpMux);
  }
  return tcpip_input(p, n);
}
struct ArpRequest {
  uint32_t ip, local;
  bool sent;
  SemaphoreHandle_t done;
};
void requestArp(void *arg) {
  auto *r = (ArpRequest *)arg;
  struct netif *n;
  NETIF_FOREACH(n) {
    if (n != esp_netif_get_netif_impl(ETH.netif()) || ntohl(netif_ip4_addr(n)->addr) != r->local)
      continue;
    // Only wrap the standard input function; leave custom network stacks alone.
    if (n->input != tcpip_input && n->input != monitorInput)
      break;
    n->input = monitorInput;
    ip4_addr_t ip;
    ip.addr = htonl(r->ip);
    portENTER_CRITICAL(&arpMux);
    arpTarget = r->ip;
    arpLocal = r->local;
    arpAnswered = false;
    arpActive = true;
    portEXIT_CRITICAL(&arpMux);
    r->sent = etharp_request(n, &ip) == ERR_OK;
    break;
  }
  xSemaphoreGive(r->done);
}
bool probeArp(uint32_t ip, char *mac) {
  uint32_t local = number(ETH.localIP()), mask = number(ETH.subnetMask());
  if ((ip & mask) != (local & mask))
    return false;
  ArpRequest req = {ip, local, false, xSemaphoreCreateBinary()};
  if (!req.done)
    return false;
  if (tcpip_callback(requestArp, &req) == ERR_OK)
    xSemaphoreTake(req.done, portMAX_DELAY);
  vSemaphoreDelete(req.done);
  bool answered = false;
  if (req.sent) {
    uint32_t start = millis();
    do {
      portENTER_CRITICAL(&arpMux);
      answered = arpAnswered;
      portEXIT_CRITICAL(&arpMux);
      if (answered)
        break;
      vTaskDelay(pdMS_TO_TICKS(5));
    } while (millis() - start < 120);
  }
  portENTER_CRITICAL(&arpMux);
  if (answered)
    snprintf(mac, 18, "%02X:%02X:%02X:%02X:%02X:%02X", arpMac[0], arpMac[1], arpMac[2], arpMac[3],
             arpMac[4], arpMac[5]);
  arpActive = false;
  portEXIT_CRITICAL(&arpMux);
  return answered;
}
bool dnsName(const uint8_t *b, int n, int &pos, String &out) {
  int cur = pos, end = -1;
  out = "";
  for (int hops = 0; hops < 64; hops++) {
    if (cur >= n)
      return false;
    int l = b[cur++];
    if (!l) {
      pos = end < 0 ? cur : end;
      return true;
    }
    if ((l & 192) == 192) {
      if (cur >= n)
        return false;
      int ptr = ((l & 63) << 8) | b[cur++];
      if (end < 0)
        end = cur;
      cur = ptr;
      continue;
    }
    if (l > 63 || cur + l > n || out.length() + l > 95)
      return false;
    if (out.length())
      out += '.';
    while (l--)
      out += char(b[cur++]);
  }
  return false;
}
String reverseName(uint32_t ip, IPAddress dns, uint32_t waitMs) {
  WiFiUDP udp;
  if (!udp.begin(0))
    return "";
  uint16_t id = uint16_t(esp_random());
  uint8_t q[96] = {uint8_t(id >> 8), uint8_t(id), 1, 0, 0, 1, 0, 0, 0, 0, 0, 0};
  int len = 12;
  IPAddress a = address(ip);
  String host = String(a[3]) + "." + a[2] + "." + a[1] + "." + a[0] + ".in-addr.arpa";
  int start = 0;
  while (start < int(host.length())) {
    int dot = host.indexOf('.', start);
    if (dot < 0)
      dot = host.length();
    q[len++] = dot - start;
    for (int i = start; i < dot; i++)
      q[len++] = host[i];
    start = dot + 1;
  }
  q[len++] = 0;
  q[len++] = 0;
  q[len++] = 12;
  q[len++] = 0;
  q[len++] = 1;
  udp.beginPacket(dns, 53);
  udp.write(q, len);
  udp.endPacket();
  uint32_t since = millis();
  String result;
  while (millis() - since < waitMs) {
    int size = udp.parsePacket();
    if (size) {
      uint8_t b[512];
      int n = udp.read(b, sizeof(b));
      if (n < 12 || udp.remoteIP() != dns || b[0] != q[0] || b[1] != q[1] || !(b[2] & 128) ||
          (b[3] & 15))
        continue;
      int p = 12;
      String ignored;
      int questions = (b[4] << 8) | b[5], answers = (b[6] << 8) | b[7];
      bool valid = true;
      for (int i = 0; i < questions; i++) {
        if (!dnsName(b, n, p, ignored) || p + 4 > n) {
          valid = false;
          break;
        }
        p += 4;
      }
      if (!valid)
        continue;
      for (int i = 0; i < answers; i++) {
        if (!dnsName(b, n, p, ignored) || p + 10 > n)
          break;
        int type = (b[p] << 8) | b[p + 1], length = (b[p + 8] << 8) | b[p + 9];
        p += 10;
        if (p + length > n)
          break;
        if (type == 12) {
          int namePos = p;
          dnsName(b, n, namePos, result);
          break;
        }
        p += length;
      }
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  udp.stop();
  return result;
}
#include "RecoveryMonitor.h"
#include "OpenWrt.h"
#include "NetworkSettings.h"
#include "DevicePolicy.h"
static bool resolveDeviceHostnames(Device *rows, unsigned count);
static bool resolveDeviceHostnames(Device *rows, unsigned count) {
  const IPAddress dns = ETH.dnsIP();
  const uint32_t localIP = number(ETH.localIP());
  for (unsigned i = 0; i < count; ++i) {
    if (!networkReady())
      return false;
    Device &device = rows[i];
    device.hostname[0] = '\0';
    device.dnsCheckedAt = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (unsigned j = 0; j < deviceCount; ++j) {
      if (sameDevice(device, devices[j]) && device.ip == devices[j].ip) {
        strlcpy(device.hostname, devices[j].hostname, sizeof(device.hostname));
        device.dnsCheckedAt = devices[j].dnsCheckedAt;
        break;
      }
    }
    xSemaphoreGive(lock);
    if (!device.online || !device.ip)
      continue;
    const uint32_t now = uptimeSeconds();
    if (device.ip == localIP) {
      snprintf(device.hostname, sizeof(device.hostname), "%s.local", networkSettings.hostname);
    } else if (dns != IPAddress() &&
               (!device.dnsCheckedAt ||
                now - device.dnsCheckedAt >= (device.hostname[0] ? 300U : 60U))) {
      const String hostname = reverseName(device.ip, dns, 45);
      if (hostname.length())
        strlcpy(device.hostname, hostname.c_str(), sizeof(device.hostname));
      device.dnsCheckedAt = now ? now : 1;
    }
    if (scanSource == "local")
      strlcpy(device.name, device.hostname, sizeof(device.name));
  }
  return networkReady();
}

void scanTask(void *) {
  for (;;) {
    if (!networkReady()) {
      closeRouterSSH();
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    uint32_t began = millis(), lo, hi;
    if (scanSource == "glinet") {
      lo = 0;
      hi = 0xffffffffUL;
    } else if (!bounds(mode, subnet, rangeStart, rangeEnd, lo, hi)) {
      xSemaphoreTake(lock, portMAX_DELAY);
      scanError = "Choose a subnet or range with at most 254 addresses in Admin.";
      xSemaphoreGive(lock);
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    nextScanAt.store(0);
    scanning = true;
    scanRequested = false;
    ulTaskNotifyTake(pdTRUE, 0);
    progress = 0;
    scanError = "";
    xSemaphoreGive(lock);
    // Allocate reports according to the actual inventory. Swap completed tables
    // under the mutex so readers never see a partially built report.
    unsigned capacity = scanSource == "glinet" ? 16 : hi - lo + 1;
    if (scanSource != "local") {
      xSemaphoreTake(lock, portMAX_DELAY);
      capacity = deviceCount + 8;
      xSemaphoreGive(lock);
      if (capacity < 16)
        capacity = 16;
      if (capacity > MAX_HOSTS)
        capacity = MAX_HOSTS;
    }
    Device *fresh = new (std::nothrow) Device[capacity];
    if (!fresh) {
      xSemaphoreTake(lock, portMAX_DELAY);
      scanning = false;
      scanError = "Not enough memory for a scan working table. Previous report "
                  "retained.";
      xSemaphoreGive(lock);
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }
    unsigned count = 0;
    bool aborted = false;
    if (scanSource != "local") {
      // GL.iNet supplies a complete authoritative list; do not invent offline
      // rows for clients it has removed. Legacy OpenWrt retains prior rows.
      xSemaphoreTake(lock, portMAX_DELAY);
      count = scanSource == "glinet" ? 0 : deviceCount;
      if (count)
        memcpy(fresh, devices, count * sizeof(Device));
      xSemaphoreGive(lock);
      for (unsigned i = 0; i < count; i++) {
        fresh[i].online = false;
        fresh[i].changed = false;
        fresh[i].isNew = false;
      }
      RouterSnapshot snapshot = {fresh, count, capacity, lo, hi};
      snapshot.canGrow = true;
      String error;
      aborted = !readRouterSnapshot(snapshot, error);
      count = snapshot.count;
      fresh = snapshot.rows;
      capacity = snapshot.capacity;
      if (!aborted && !networkReady()) {
        aborted = true;
        error = "Ethernet disconnected during router query.";
      }
      xSemaphoreTake(lock, portMAX_DELAY);
      if (aborted)
        scanError = error + " Previous report retained.";
      else
        progress = count;
      xSemaphoreGive(lock);
    } else {
      for (uint32_t ip = lo; ip <= hi; ip++) {
        if (!networkReady()) {
          aborted = true;
          break;
        }
        const bool self = ip == number(ETH.localIP());
        Device device;
        if (self || probeArp(ip, device.mac) || ping(ip)) {
          device.ip = ip;
          device.online = true;
          if (self)
            strlcpy(device.mac, ETH.macAddress().c_str(), sizeof(device.mac));
          fresh[count++] = device;
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        progress++;
        xSemaphoreGive(lock);
        vTaskDelay(1);
      }
    }
    if (!aborted)
      aborted = !resolveDeviceHostnames(fresh, count);
    Device *oldReport = nullptr;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!aborted) {
      const uint32_t now = esp_timer_get_time() / 1000000ULL;
      const uint32_t epoch = utcNow();
      if (scanSource == "openwrt") {
        // Drop carried-over offline rows cleared while the router query ran.
        unsigned kept = 0;
        for (unsigned i = 0; i < count; ++i) {
          bool retain = fresh[i].online;
          for (unsigned j = 0; j < deviceCount && !retain; ++j)
            retain = sameDevice(fresh[i], devices[j]);
          if (retain) {
            if (kept != i)
              fresh[kept] = fresh[i];
            ++kept;
          }
        }
        count = kept;
      }
      const unsigned scannedCount = count;
      for (unsigned i = 0; i < scannedCount; i++) {
        const Device *previous = nullptr;
        for (unsigned j = 0; j < deviceCount; j++) {
          if (sameDevice(fresh[i], devices[j])) {
            previous = &devices[j];
            break;
          }
        }
        if (!fresh[i].hostname[0] && previous && previous->ip == fresh[i].ip)
          strlcpy(fresh[i].hostname, previous->hostname, sizeof(fresh[i].hostname));
        if (scanSource == "local" && !fresh[i].name[0])
          strlcpy(fresh[i].name, fresh[i].hostname, sizeof(fresh[i].name));
        updateDevice(fresh[i], previous, now, epoch);
      }
      RouterSnapshot history = {fresh, count, capacity, lo, hi};
      history.canGrow = true;
      for (unsigned j = 0; j < deviceCount; j++) {
        bool found = false;
        for (unsigned i = 0; i < scannedCount && !found; i++)
          found = sameDevice(history.rows[i], devices[j]);
        if (found)
          continue;
        Device offline = devices[j];
        offline.online = false;
        updateDevice(offline, &devices[j], now, epoch);
        if (history.count < history.capacity || history.expand())
          history.rows[history.count++] = offline;
      }
      fresh = history.rows;
      count = history.count;
      oldReport = devices;
      devices = fresh;
      fresh = nullptr;
      deviceCount = count;
      lastScan = millis();
      lastScanEpoch = utcNow();
      scanNumber++;
      completed = true;
    } else if (scanSource == "local")
      scanError = "Ethernet disconnected; previous scan retained.";
    scanning = false;
    xSemaphoreGive(lock);
    delete[] oldReport;
    delete[] fresh;
    uint32_t elapsed = millis() - began;
    uint32_t interval = scanIntervalSeconds * 1000UL;
    const uint32_t wait = elapsed < interval ? interval - elapsed : 250;
    nextScanAt.store(millis() + wait);
    const uint32_t waitStarted = millis();
    const uint32_t generation = ethernetLinkGeneration.load();
    while (millis() - waitStarted < wait) {
      if (!networkReady() || ethernetLinkGeneration.load() != generation) {
        closeRouterSSH();
        break;
      }
      const uint32_t waited = millis() - waitStarted;
      if (waited >= wait || ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(min(wait - waited, 1000UL))))
        break;
    }
  }
}
const char ADMIN_PAGE_HEAD[] PROGMEM =
    R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="light dark"><title>Watchtower - Intrusion Detection System (IDS)</title><script src="/assets/theme.js"></script><link rel="stylesheet" href="/assets/ui.css"></head><body>)HTML";
void gzipAsset(const uint8_t *data, size_t length, const char *type) {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, type, reinterpret_cast<const char *>(data), length);
}
#include "AdminSession.h"
bool auth() {
  server.sendHeader("Cache-Control", "no-store");
  if (adminCookie("watchtower_logout") == "1") {
    server.send(401, "text/html", "<p>You are logged out of Watchtower Admin.</p><a href='/admin/login'>Sign In to Admin</a>");
    return false;
  }
  if (validAdminSession()) return true;
  // Only a deliberate Admin page visit may open a browser login dialog.
  // Concurrent polling challenges invalidate WebServer's shared nonce.
  if (server.method() != HTTP_GET || server.uri() != "/admin") {
    server.send(401, "text/plain", "Admin session expired. Open /admin/login to sign in.");
    return false;
  }
  const String authorization = server.header("Authorization");
  const int realmStart = authorization.indexOf("realm=\"");
  const int realmEnd = realmStart < 0 ? -1 : authorization.indexOf('"', realmStart + 7);
  if (authorization.startsWith("Digest ") && realmStart >= 0 && realmEnd > realmStart &&
      authorization.substring(realmStart + 7, realmEnd) == adminRealm &&
      server.authenticate("admin", adminPassword.c_str())) {
    startAdminSession();
    return true;
  }
  server.requestAuthentication(DIGEST_AUTH, adminRealm.c_str());
  return false;
}
void dashboard() { gzipAsset(DASHBOARD_HTML, sizeof(DASHBOARD_HTML), "text/html; charset=utf-8"); }
void forceScan() {
  if (server.arg("token") != csrf) {
    server.send(403, "application/json",
                "{\"error\":\"Reload the dashboard before requesting a scan.\"}");
    return;
  }
  if (!networkReady() || !scannerTask) {
    server.send(409, "application/json",
                "{\"error\":\"Scanner unavailable. Check the Ethernet link and DHCP lease.\"}");
    return;
  }
  static uint32_t lastForced = 0;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (scanning || scanRequested) {
    xSemaphoreGive(lock);
    server.send(202, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  if (lastForced && millis() - lastForced < 10000) {
    xSemaphoreGive(lock);
    server.send(429, "application/json",
                "{\"error\":\"Please wait 10 seconds between manual scans.\"}");
    return;
  }
  lastForced = millis();
  scanRequested = true;
  xSemaphoreGive(lock);
  xTaskNotifyGive(scannerTask);
  server.send(202, "application/json", "{\"status\":\"requested\"}");
}
void api() {
  static Device *snapshot = nullptr;
  if (!snapshot)
    snapshot = new (std::nothrow) Device[MAX_HOSTS];
  if (!snapshot) {
    server.send(503, "text/plain", "Not enough memory for the device report.");
    return;
  }
  String out;
  if (!out.reserve(1400)) {
    server.send(503, "text/plain", "Monitor busy; retry shortly.");
    return;
  }
  if (xSemaphoreTake(lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
    server.send(503, "text/plain", "Scan busy. Retry shortly.");
    return;
  }
  const unsigned snapshotCount = deviceCount;
  if (snapshotCount)
    memcpy(snapshot, devices, snapshotCount * sizeof(Device));
  unsigned onlineCount = 0, offlineCount = 0, newCount = 0, changedCount = 0;
  for (unsigned i = 0; i < snapshotCount; i++) {
    if (snapshot[i].online)
      onlineCount++;
    else
      offlineCount++;
    if (snapshot[i].isNew)
      newCount++;
    if (snapshot[i].changed)
      changedCount++;
  }
  uint64_t now = utcNow();
  uint64_t scanned = lastScanEpoch;
  if (completed && !scanned && now)
    scanned = now - (millis() - lastScan) / 1000;
  out += "{\"nextScanSeconds\":" +
         String(nextScanAt.load() ? max(int32_t(0), int32_t(nextScanAt.load() - millis())) / 1000
                                  : 0) +
         ",\"firmware\":\"ids-1\",\"nowEpoch\":" + String(now) +
         ",\"uptimeSeconds\":" + String(uint64_t(esp_timer_get_time()) / 1000000ULL) +
         ",\"timeZone\":" + json(timeZone) + ",\"timeFormat\":" + json(timeFormat) +
         ",\"timeServer\":" + json(timeServer) + ",\"scanNumber\":" + String(scanNumber) +
         ",\"clockSynced\":" + (now ? "true" : "false") + ",\"lastScanEpoch\":" + String(scanned) +
         ",\"scanToken\":" + json(csrf) +
         ",\"scanRequested\":" + (scanRequested ? "true" : "false") +
         ",\"clientMode\":" + String(scanSource == "glinet" ? "true" : "false") +
         ",\"source\":" + json(scanSource) +
         ",\"ethernetIP\":" + json(networkReady() ? ETH.localIP().toString() : String("")) +
         ",\"interval\":" + scanIntervalSeconds + ",\"online\":" + onlineCount +
         ",\"offline\":" + offlineCount + ",\"newDevices\":" + newCount +
         ",\"changedDevices\":" + changedCount +
         ",\"scanning\":" + String(scanning ? "true" : "false") +
         ",\"completed\":" + (completed ? "true" : "false") + ",\"progress\":" + progress +
         ",\"age\":" + ((millis() - lastScan) / 1000) + ",\"error\":" + json(scanError) +
         ",\"ethernetStarted\":" + (ethernetStarted ? "true" : "false") +
         ",\"hostname\":" + json(networkSettings.hostname) +
         ",\"apName\":" + json(networkSettings.apName) +
         ",\"ethernetMac\":" + json(ethernetStarted ? ETH.macAddress() : String("")) +
         ",\"devices\":[";
  xSemaphoreGive(lock);
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent(out);
  for (unsigned i = 0; i < snapshotCount; i++) {
    out = "";
    if (i)
      out += ',';
    auto &d = snapshot[i];
    char key[13];
    out += "{\"ip\":" + json(d.ip ? address(d.ip).toString() : String("")) +
           ",\"name\":" + json(d.name) + ",\"hostname\":" + json(d.hostname) +
           ",\"mac\":" + json(d.mac) + ",\"status\":" + json(d.online ? "ONLINE" : "OFFLINE") +
           ",\"changed\":" + (d.changed ? "true" : "false") +
           ",\"isNew\":" + (d.isNew ? "true" : "false") + ",\"connection\":" + json(d.connection) +
           ",\"policy\":" + json(policyNames[effectivePolicy(d, uptimeSeconds())]) +
           ",\"canAcknowledge\":" + (d.online && d.policy == Unknown ? "true" : "false") +
           ",\"policyAvailable\":" + (macKey(d.mac, key) ? "true" : "false") +
           ",\"lastSeen\":" + d.lastSeenEpoch +
           ",\"sessionSource\":" + json(d.hasRouterSession ? "router" : "estimated") +
           ",\"session\":" +
           (d.hasRouterSession
                ? uint64_t(d.routerSession) + (d.online ? uptimeSeconds() - d.lastSeen : 0)
                : uint64_t((d.online ? uptimeSeconds() : d.lastSeen) - d.connectedAt)) +
           "}";
    if (!server.client().connected())
      break;
    server.sendContent(out);
    delay(1);
  }
  out = "]}";
  server.sendContent(out);
  server.sendContent("");
}
void changeDevices() {
  if (!auth())
    return;
  if (server.arg("csrf") != csrf) {
    server.send(403, "text/plain", "Reload the Admin Console and try again.");
    return;
  }
  const String action = server.arg("action");
  if (action == "acknowledge-all") {
    const unsigned count = acknowledgeOnlineDevices();
    server.send(200, "text/plain",
                String(count) + " device(s) acknowledged for " + ackMinutes + " minutes.");
    return;
  }
  const String mac = server.arg("mac");
  const Policy policy = action == "allow"         ? Allowed
                        : action == "acknowledge" ? Acknowledged
                                                  : Unknown;
  char key[13];
  const bool clear = action == "clear";
  const bool clearDevice = action == "clear-device";
  if (!clear &&
      ((policy == Unknown && action != "reset" && !clearDevice) || !macKey(mac.c_str(), key))) {
    server.send(400, "text/plain", "Choose a device with a valid MAC address.");
    return;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  bool saved = true;
  if (clear) {
    delete[] devices;
    devices = nullptr;
    deviceCount = 0;
  } else if (clearDevice) {
    unsigned kept = 0;
    for (unsigned i = 0; i < deviceCount; i++) {
      if (mac.equalsIgnoreCase(devices[i].mac))
        continue;
      if (kept != i)
        devices[kept] = devices[i];
      ++kept;
    }
    deviceCount = kept;
  } else {
    saved = setPolicy(mac, policy);
  }
  xSemaphoreGive(lock);
  if (!saved && policy == Acknowledged) {
    server.send(409, "text/plain", "Acknowledge an online unauthorized device.");
    return;
  }
  server.send(saved ? 200 : 500, "text/plain",
              saved ? "Saved" : "Could not save the device policy.");
}
String field(String label, String name, String value, String type = "text") {
  return "<label class='field'>" + label + "<input name='" + name + "' type='" + type +
         "' value='" + escape(value) + "' autocomplete='" +
         (type == "password" ? "new-password" : "off") + "'></label>";
}
String timeSettingsFields() {
  String p =
      "<section class='card settings-card' id='time'><div class='section-title'><div><h2>Date &amp; Time</h2><p>Choose the clock shown to "
      "everyone viewing the dashboard.</p></div></div><div class='form-grid'><label "
      "class='field'>Time zone<select name='timeZone' id='timeZone'><option selected value='" +
      escape(timeZone) + "'>" + escape(timeZone) +
      "</option></select></label><label class='field'>Time format<select name='timeFormat' "
      "id='timeFormat'>";
  for (auto format : {"24", "12"})
    p += "<option value='" + String(format) + "'" + (timeFormat == format ? " selected" : "") +
         ">" + String(format) + " hours</option>";
  p +=
      "</select></label><label class='field'>Time server<select name='timeServer' id='timeServer'>";
  bool preset = false;
  for (auto host : {"time.nist.gov", "pool.ntp.org", "time.cloudflare.com", "time.google.com"}) {
    bool selected = timeServer == host;
    preset = preset || selected;
    p += "<option value='" + String(host) + "'" + (selected ? " selected" : "") + ">" +
         String(host) + "</option>";
  }
  p += "<option value='custom'" + String(preset ? "" : " selected") +
       ">Custom server</option></select></label><div id='customTimeServerField'>" +
       field("Custom NTP hostname or IPv4 address", "customTimeServer",
             preset ? String("") : timeServer) +
       "</div></div><p class='helper' id='timePreview'></p><p class='helper'>24-hour time is the "
       "default. Time zones follow daylight-saving changes automatically. NTP Pool uses "
       "pool.ntp.org. NIST and NTP Pool include provider-specific fallback servers; other choices "
       "use the selected server only.</p></section>";
  return p;
}
static bool adminRequest(bool write = false) {
  if (!auth())
    return false;
  if (write && server.arg("csrf") != csrf) {
    server.send(403, "text/plain", "Reload Admin and try again.");
    return false;
  }
  return true;
}

#include "Health.h"

static void saveConfigurationBackup() {
  if (!adminRequest(true))
    return;
  if (restartAt) {
    server.send(409, "text/plain", "Wait for the restart to finish.");
    return;
  }
  const String data =
      String("{\n  \"version\": 1,\n  \"hostname\": ") + json(networkSettings.hostname) +
      ",\n  \"ethernet\": {\"mode\": " + json(networkSettings.staticIP ? "static" : "dhcp") +
      ", \"ip\": " + json(networkSettings.ip) + ", \"mask\": " + json(networkSettings.mask) +
      ", \"gateway\": " + json(networkSettings.gateway) +
      ", \"dns\": " + json(networkSettings.dns) +
      "},\n  \"accessPoint\": {\"ssid\": " + json(networkSettings.apName) +
      ", \"channel\": " + apChannel + ", \"widthMHz\": " + apWidth +
      ", \"enabledAtBoot\": " + (apStartsOnBoot ? "true" : "false") +
      "},\n  \"discovery\": {\"source\": " + json(scanSource) + ", \"mode\": " + json(mode) +
      ", \"subnet\": " + json(subnet) + ", \"start\": " + json(rangeStart) +
      ", \"end\": " + json(rangeEnd) + ", \"intervalSeconds\": " + scanIntervalSeconds +
      "},\n  \"router\": {\"ip\": " + json(routerIP) + ", \"port\": " + routerPort +
      ", \"transport\": " + json(routerTransport) + ", \"username\": " + json(routerUser) +
      "},\n  \"time\": {\"zone\": " + json(timeZone) + ", \"format\": " + json(timeFormat) +
      ", \"server\": " + json(timeServer) + "},\n  \"acknowledgmentMinutes\": " + ackMinutes +
      "\n}\n";
  const bool saved = prefs.putString("configBackup", data) == data.length();
  server.send(saved ? 200 : 500, "text/plain",
              saved ? "Configuration saved to internal flash."
                    : "Could not save the configuration backup.");
}

enum LedState { LedUnauthorized, LedAcknowledged, LedAuthorized, LedActivity, LedStateCount };
static const char *const ledLabels[] = {"Unauthorized", "Acknowledged", "Authorized / clear",
                                        "Scan activity"};
struct LedSettings {
  bool enabled = true;
  bool show[LedStateCount] = {true, true, true, false};
  uint32_t colors[LedStateCount] = {0xFF0000, 0xFFCA00, 0x00FF00, 0x0080FF};
};
static LedSettings leds;

static String ledSettingsSection() {
  String html = "<section class='card settings-card' id='leds'><h2>LED Indicators</h2>"
                "<p class='helper'>Control the onboard RGB indicator. The Ethernet controller "
                "drives LINK and ACT; Stealth Mode controls the RGB indicator only.</p>"
                "<button type='button' class='btn' id='stealthMode' aria-pressed='false'>Loading Stealth Mode…</button><p "
                "id='stealthFeedback' role='status'></p><form id='ledForm'><label "
                "class='check'><input type='checkbox' name='enabled' value='yes'";
  if (leds.enabled)
    html += " checked";
  html += ">Enable RGB indicator</label><div class='form-grid'>";
  for (unsigned i = 0; i < LedStateCount; ++i) {
    const String index(i);
    char color[8];
    snprintf(color, sizeof(color), "#%06lx", static_cast<unsigned long>(leds.colors[i]));
    html += "<div><label class='check'><input type='checkbox' name='show" + index +
            "' value='yes'" + (leds.show[i] ? " checked" : "") + ">" + ledLabels[i] +
            "</label>"
            "<label class='field'>Color<input type='color' name='color" +
            index + "' value='" + color + "'></label></div>";
  }
  return html + "</div><button class='btn btn-primary' type='submit'>Apply LED Settings</button>"
                "<p id='ledFeedback' role='status'></p></form></section>";
}

static void saveLedSettings() {
  if (!adminRequest(true))
    return;
  LedSettings next;
  next.enabled = server.arg("enabled") == "yes";
  for (unsigned i = 0; i < LedStateCount; ++i) {
    const String index(i);
    const String color = server.arg("color" + index);
    if (color.length() != 7 || color[0] != '#' ||
        strspn(color.c_str() + 1, "0123456789abcdefABCDEF") != 6) {
      server.send(400, "text/plain", "Choose a valid color for each indicator.");
      return;
    }
    next.show[i] = server.arg("show" + index) == "yes";
    next.colors[i] = strtoul(color.c_str() + 1, nullptr, 16);
  }
  if (prefs.putBytes("leds-v1", &next, sizeof(next)) != sizeof(next)) {
    server.send(500, "text/plain", "Could not save LED settings.");
    return;
  }
  leds = next;
  server.send(200, "text/plain", "LED settings saved.");
}

#include "HeatmapSettings.h"
#include "RecoverySettings.h"

void admin() {
  if (!auth())
    return;
  String p;
  if (!p.reserve(32000)) {
    server.send(503, "text/plain", "Admin is busy. Try again shortly.");
    return;
  }
  p +=
      String(ADMIN_PAGE_HEAD) + R"HTML(<header class="topbar"><div class="shell topbar-inner"><a class="brand" href="/"><span class="brand-mark"><svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3" y="3" width="6" height="6" rx="1.5"/><rect x="15" y="15" width="6" height="6" rx="1.5"/><path d="M6 9v9h9M9 6h9v9"/></svg></span><div><span class="eyebrow">Administration</span><h1>Watchtower - Intrusion Detection System (IDS)</h1></div></a><div class="actions"><div class="font-controls" role="group" aria-label="Page text size"><button class="btn" type="button" data-font-step="-1" aria-label="Decrease text size">A-</button><button class="btn font-reset" type="button" data-font-reset aria-label="Reset text size to 100 percent"><span data-font-label aria-live="polite">100%</span></button><button class="btn" type="button" data-font-step="1" aria-label="Increase text size">A+</button></div><button class="btn" type="button" data-theme-toggle><svg class="theme-moon" viewBox="0 0 24 24" aria-hidden="true"><path d="M20.5 14A8.5 8.5 0 0 1 10 3.5 8.5 8.5 0 1 0 20.5 14Z"/></svg><svg class="theme-sun" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2v2m0 16v2M2 12h2m16 0h2"/></svg><span class="theme-label" data-theme-label>Dark Mode</span></button><button type="button" id="rebootSystem" class="btn header-icon" title="Reboot System" aria-label="Reboot System"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M20 8a8 8 0 1 0 .5 7M20 3v5h-5"/></svg></button><button type="button" id="logoutAdmin" class="btn header-icon" title="Logout" aria-label="Logout"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M10 4H4v16h6m4-12 4 4-4 4m-6-4h12"/></svg></button><a class="btn btn-primary" href="/">Back to Dashboard</a></div></div></header><main class="shell"><div class="admin-layout"><nav class="settings-nav" aria-label="Settings sections"><div class="nav-label">Configuration</div><a href="#network" title="Set the hostname, DHCP, and static IP address.">Ethernet</a><a href="#access-point" title="Configure the local hotspot, channel, and startup behavior.">Wi-Fi Access Point</a><a href="#system-recovery" title="Enable automatic recovery for a stalled application or SSH operation.">System Recovery</a><a href="#scan" title="Choose device discovery options and scan frequency.">Scan Preferences</a><a href="#router" title="Connect to your router for live device information.">Router Connection</a><a href="#acknowledgment" title="Set how long device acknowledgments last.">Acknowledgment</a><a href="#health" title="View CPU, memory, and internal storage usage.">Health Status</a><a href="#leds" title="Set RGB status colors and Stealth Mode.">LED Indicators</a><a href="#time" title="Set the time zone, clock format, and time server.">Date &amp; Time</a><a href="#security" title="Change the Admin login password.">Admin Security</a><a href="#configuration-backup" title="Save or restore settings in internal storage.">Configuration Backup</a><a href="#admin-heatmap" title="View device policies and connection types at a glance.">Device Map</a><a href="#devices" title="Authorize devices, acknowledge alerts, and manage history.">Device Policies</a></nav><div><div id="adminSessionAlert" class="alert" role="status" hidden></div><p id="rebootFeedback" role="status"></p><div class="form-title"><h2 class="configuration-title">Configuration</h2><p class="muted">Monitor settings</p><p>Changes are saved to your device and applied after a restart.</p></div>)HTML";
  p +=
      R"HTML(<section class="card heatmap-card settings-card" id="admin-heatmap" aria-labelledby="heatmapTitle"><div class="heatmap-heading"><h2 id="heatmapTitle">Device Map</h2><span id="heatmapCount" class="muted"></span></div><div class="heatmap-legend"><span><i class="heat-online"></i>Authorized Online</span><span><i class="heat-alert"></i>Unauthorized Online</span><span><i class="heat-ack"></i>Acknowledged Online</span><span><i class="heat-offline"></i>Offline</span><span><i class="heat-unused"></i>Unused Port</span></div><div class="switch-caption"><span>256 DEVICE PORTS</span><span>32 × 8 · Wi-Fi / Ethernet · Select a Port for Details</span></div><div id="deviceHeatmap" class="device-heatmap" aria-label="256 device ports; unused ports are dark gray"></div><p id="heatmapDetail" class="helper" role="status">Select a tile to find its device in the inventory.</p>)HTML" + heatmapSettingsForm() + R"HTML(</section><section class='card settings-card' id='devices'><div class='section-title'><div>
      <h2>Device Policies</h2><p>Manage discovered devices and clear the observed history.</p></div></div>
      <label class='field'>Device group<select id='adminDeviceGroup'><option value="all">All Devices</option><option value="online">Online</option><option value="online-authorized">Online Authorized</option><option value="online-unauthorized">Online Unauthorized</option><option value="online-acknowledged">Online Acknowledged</option><option value="offline">Offline</option><option value="offline-authorized">Offline Authorized</option><option value="offline-unauthorized">Offline Unauthorized</option><option value="offline-acknowledged">Offline Acknowledged</option></select></label>
      <label class='field device-search'>Search devices<input id='deviceSearch' type='search' placeholder='Hostname, IP, MAC, presence or policy'></label>
      <div class='device-selection'>
        <button type='button' id='selectAllDevices' class='btn' disabled>Select All Shown</button>
        <button type='button' id='selectNoDevices' class='btn' disabled>Select None</button>
        <button type='button' id='authorizeSelected' class='btn btn-primary' disabled>Authorize Selected</button>
        <button type='button' id='deauthorizeSelected' class='btn' disabled>Deauthorize Selected</button>
        <button type='button' id='clearSelectedDevices' class='btn btn-danger' disabled>Clear Selected Devices from History</button>
        <span id='deviceSelectionCount' role='status'>0 selected</span>
      </div>
      <div id='adminDevices' data-token=')HTML" +
      csrf + R"HTML('></div>
      <button type='button' id='acknowledgeAllDevices' class='btn btn-primary'>Acknowledge All Online</button>
      <button type='button' id='clearDevices' class='btn btn-danger'>Clear All Device History</button>
      <p id='deviceFeedback' class='helper' role='status'></p></section>
      <section class='card settings-card' id='health'><h2>Health Status</h2>
        <div id='healthStatus' class='health-grid' aria-label='System resource usage'>
          <p class='helper'>Loading health information</p>
        </div><p id='healthError' class='helper' role='status' hidden></p>
      </section>
      <section class='card settings-card' id='configuration-backup'>
        <h2>Configuration Backup</h2>
        <p class='helper'>Save the active settings to internal flash. Each backup replaces the previous one. Passwords, device policies, LED, Device Map, and recovery preferences are excluded.</p>
        <div class='device-selection'>
          <button type='button' id='saveBackup' class='btn btn-primary'>Save Configuration</button>
          <button type='button' id='restoreBackup' class='btn'>Restore Saved Configuration &amp; Reboot Board</button>
        </div>
        <p id='backupStatus' role='status'></p>
      </section>
      )HTML" +
      recoverySettingsSection() + ledSettingsSection() + R"HTML(
      <form method='post' action='/admin' id='settingsForm'><input type='hidden' name='csrf' value=")HTML" +
      csrf +
      R"HTML("><section class='card settings-card' id='network'><h2>Ethernet</h2><div class='form-grid'>)HTML";
  p += field("Hostname", "hostname", networkSettings.hostname) +
       "<div class='field'>Fixed Ethernet MAC address <code>" +
       escape(ethernetStarted ? ETH.macAddress() : String("Unavailable")) + "</code></div>" +
       "<label class='field'>IP assignment<select name='ipMode' id='ipMode'><option value='dhcp'" +
       String(networkSettings.staticIP ? "" : " selected") +
       ">DHCP</option><option value='static'" +
       String(networkSettings.staticIP ? " selected" : "") +
       ">Static IPv4</option></select></label></div>";
  p += "<div id='staticFields' class='form-grid'>" +
       field("IPv4 address", "ethIP", networkSettings.ip) +
       field("Subnet mask", "ethMask", networkSettings.mask) +
       field("Default gateway", "ethGateway", networkSettings.gateway) +
       field("DNS server", "ethDNS", networkSettings.dns) +
       "</div></section><section class='card settings-card' id='access-point'>"
       "<h2>Wi-Fi Access Point</h2><div class='form-grid'>" +
       field("Network name (SSID)", "apName", networkSettings.apName) +
       field("New Wi-Fi password", "apPassword", "", "password") +
       "<label class='field'>Radio band<select disabled><option>2.4 GHz</option>"
       "<option disabled>5 GHz (unavailable on ESP32-S3)</option></select></label>"
       "<label class='field'>Channel and center frequency<select id='apChannel' name='apChannel'>";
  for (unsigned channel = 1; channel <= 11; ++channel) {
    p += "<option value='" + String(channel) + "'" + (channel == apChannel ? " selected" : "") +
         ">" + channel + " — " + (2407 + 5 * channel) + " MHz</option>";
  }
  p += "</select></label><label class='field'>Channel width<select id='apWidth' name='apWidth'>"
       "<option value='20'" +
       String(apWidth == 20 ? " selected" : "") +
       ">20 MHz</option>"
       "<option value='40'" +
       String(apWidth == 40 ? " selected" : "") +
       ">40 MHz</option>"
       "</select></label></div><p class='helper'>Leave the password blank to keep it. "
       "Use 8–63 printable ASCII characters. Manage the AP over Ethernet to stay connected.</p>"
       "<div class='device-selection'><button type='button' class='btn' id='apStart'>Start "
       "AP</button>"
       "<button type='button' class='btn' id='apStop'>Stop AP</button>"
       "<button type='button' class='btn btn-primary' id='apRestart'>Save and Restart "
       "AP</button></div>"
       "<button type='button' class='btn' id='apPersistent' data-enabled='" +
       String(apStartsOnBoot ? "true'>Disable AP Persistently" : "false'>Enable AP Persistently") +
       "</button><p id='apStatus' role='status'>" + String(apRunning ? "Running" : "Stopped") +
       "</p>" +
       "</section><section class='card settings-card' "
       "id='scan'><div class='section-title'><div><h2>Scan Preferences</h2><p>Choose "
       "where to look for devices and how often to update.</p></div></div><div "
       "class='form-grid'><label class='field'>Discovery source<select "
       "name='source' id='source'>";
  p += "<option value='local'" + String(scanSource == "local" ? " selected" : "") +
       ">Local ARP / ping</option><option value='openwrt'" +
       String(scanSource == "openwrt" ? " selected" : "") +
       ">OpenWrt HTTP (legacy)</option><option value='glinet'" +
       String(scanSource == "glinet" ? " selected" : "") +
       ">GL.iNet live client list</option></select></label><label class='field'>Scan "
       "interval (seconds)<input type='number' name='interval' min='10' "
       "max='86400' step='1' required value='" +
       String(scanIntervalSeconds) +
       "'></label><label class='field'>Address selection<select name='mode' "
       "id='mode'>";
  for (auto m : {"auto", "subnet", "range"})
    p += "<option value='" + String(m) + "'" + (mode == m ? " selected" : "") + ">" +
         String(!strcmp(m, "auto")     ? "Automatic Ethernet subnet"
                : !strcmp(m, "subnet") ? "CIDR subnet"
                                       : "Specific IP range") +
         "</option>";
  p += "</select></label></div><div id='subnetFields'>" +
       field("Subnet (CIDR), for example 192.168.2.0/24", "subnet", subnet) +
       "</div><div class='form-grid' id='rangeFields'>" +
       field("First IP address", "start", rangeStart) + field("Last IP address", "end", rangeEnd) +
       "</div><p class='helper'>Up to 254 addresses per range. Use 60 seconds "
       "for one-minute updates. GL.iNet mode imports the complete router client list; subnet/range "
       "filters apply only to other sources. The dashboard's Scan now button requests an "
       "additional scan.</p></section><section class='card settings-card' "
       "id='router'><div class='section-title'><div><h2>Router Connection</h2><p>Use "
       "your existing OpenWrt login to retrieve device "
       "records.</p></div></div><p class='helper' id='localSourceNote'>These "
       "settings are retained when local discovery is selected.</p><div "
       "class='form-grid'><label class='field'>Connection type<select "
       "name='transport' id='transport'><option value='http'" +
       String(routerTransport == "http" ? " selected" : "") +
       ">HTTP / ubus</option><option value='ssh'" +
       String(routerTransport == "ssh" ? " selected" : "") + ">SSH</option></select></label>" +
       field("Router IPv4 address", "routerIP", routerIP) +
       field("Port (SSH 22, HTTP 80)", "routerPort", String(routerPort), "number") +
       field("User ID", "routerUser", routerUser) +
       field("Router password", "routerPassword", "", "password") +
       "</div><p class='helper'>Leave the password blank to keep it. GL.iNet mode uses SSH to read "
       "the router's own clients.get_list API on loopback. Its online flag, aliases and "
       "disconnected-client records supply the report. No neighbor or DHCP status inference is "
       "used. Legacy OpenWrt HTTP needs authenticated /ubus access.</p><details "
       "class='identity'><summary>Saved SSH router identity</summary><code>";
  Preferences identity;
  String saved = "Not learned yet";
  if (identity.begin("ids-ssh", true)) {
    saved = identity.getString("endpoint", "") + " " + identity.getString("key", "");
    identity.end();
  }
  p += escape(saved) +
       "</code><label class='check'><input type='checkbox' name='forgetSSH' "
       "value='yes'>Forget this identity after verifying a replacement "
       "router</label></details></section>" +
       "<section class='card settings-card' id='acknowledgment'><h2>Acknowledgment</h2>" +
       field("Suppress alerts for (minutes)", "ackMinutes", String(ackMinutes), "number") +
       "<p class='helper'>Use 1–10080 minutes. Tap BOOT to acknowledge all online unauthorized "
       "devices. Hold BOOT for two seconds to restart. Acknowledgments clear on "
       "restart.</p></section>" +
       timeSettingsFields() +
       "<section class='card settings-card' "
       "id='security'><div class='section-title'><div><h2>Admin Security</h2><p>Update "
       "the password used to sign in to this settings "
       "page.</p></div></div><div class='form-grid'>" +
       field("New admin password", "admin", "", "password") +
       field("Confirm new password", "confirm", "", "password") +
       "</div><p class='helper'>Use at least 12 characters. Leave both fields "
       "blank to keep the current password. Your username is "
       "admin.</p></section><div class='card savebar'><p>Saving restarts the "
       "monitor and clears observed sessions. Saved device policies are kept.</p><button "
       "type='submit' class='btn btn-primary' id='saveButton'>Save Settings "
       "&amp; Restart</button></div></form>";
  p += "</div></div><footer class='page-footer'><span>Watchtower - Intrusion Detection System "
       "(IDS) / Administration</span>"
       "</footer></main><script defer src='/assets/sort.js'></script><script defer src='/assets/heatmap.js'></script><script defer "
       "src='/assets/admin.js'></script><script defer "
       "src='/assets/policies.js'></script></body></html>";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", p);
}
static bool validApChannel(const String &value) {
  const long channel = value.toInt();
  return channel >= 1 && channel <= 11 && String(channel) == value;
}

static bool updateApSettings(NetworkSettings &settings, const String &name, const String &password);
static bool updateApSettings(NetworkSettings &settings, const String &name,
                             const String &password) {
  if (!name.length() || name.length() > 32 ||
      (password.length() && (password.length() < 8 || password.length() > 63)))
    return false;
  for (unsigned i = 0; i < name.length(); ++i)
    if (uint8_t(name[i]) < 32 || name[i] == 127)
      return false;
  for (unsigned i = 0; i < password.length(); ++i)
    if (password[i] < 32 || password[i] > 126)
      return false;
  strlcpy(settings.apName, name.c_str(), sizeof(settings.apName));
  if (password.length())
    strlcpy(settings.apPassword, password.c_str(), sizeof(settings.apPassword));
  return true;
}

static void controlAccessPoint() {
  if (!adminRequest(true))
    return;
  const String action = server.arg("action");
  if (restartAt || apChangeAt) {
    server.send(409, "text/plain", "A restart or access point change is already pending.");
    return;
  }
  const bool persistent = action == "enable" || action == "disable";
  const bool stopping = action == "stop" || action == "disable";
  if (stopping && (!networkReady() || server.client().localIP() != ETH.localIP())) {
    server.send(409, "text/plain",
                "Open Admin through the Ethernet address to stop the access point.");
    return;
  }
  if (action == "restart") {
    NetworkSettings next = networkSettings;
    const String channel = server.arg("channel"), width = server.arg("width");
    if (!validApChannel(channel) || (width != "20" && width != "40") ||
        !updateApSettings(next, server.arg("name"), server.arg("password"))) {
      server.send(400, "text/plain", "Check the Wi-Fi name, password and channel.");
      return;
    }
    if (prefs.putBytes("network-v1", &next, sizeof(next)) != sizeof(next) ||
        prefs.putUChar("apChannel", channel.toInt()) != sizeof(uint8_t) ||
        prefs.putUChar("apWidth", width.toInt()) != sizeof(uint8_t)) {
      server.send(500, "text/plain", "Could not save Wi-Fi settings.");
      return;
    }
    // Other tasks read Ethernet settings and hostname from this struct.
    memcpy(networkSettings.apName, next.apName, sizeof(next.apName));
    memcpy(networkSettings.apPassword, next.apPassword, sizeof(next.apPassword));
    apChannel = channel.toInt();
    apWidth = width.toInt();
  } else if (action != "start" && action != "stop" && !persistent) {
    server.send(400, "text/plain", "Choose Start, Stop or Restart.");
    return;
  }
  if (persistent) {
    const bool enabled = action == "enable";
    if (prefs.putBool("apOnBoot", enabled) != sizeof(uint8_t)) {
      server.send(500, "text/plain", "Could not save the access point preference.");
      return;
    }
    apStartsOnBoot = enabled;
  }
  apEnabled = !stopping;
  apRestartPending = action == "restart";
  apChangeAt = millis() + 700;
  server.send(
      200, "text/plain",
      persistent
          ? (apStartsOnBoot ? "Wi-Fi AP enabled at startup." : "Wi-Fi AP disabled at startup.")
      : stopping          ? "Stopping Wi-Fi access point."
      : action == "start" ? "Starting Wi-Fi access point."
                          : "Settings saved. Reconnect to Wi-Fi after the access point restarts.");
}

static void applySettings(const std::function<String(const char *)> &setting, bool restoring) {
  String a = setting("admin"), m = setting("mode");
  if (restartAt) {
    server.send(409, "text/plain", "Wait for the restart to finish.");
    return;
  }
  NetworkSettings next = networkSettings;
  const String hostname = setting("hostname"), apName = setting("apName"),
               apPassword = setting("apPassword"), ipMode = setting("ipMode");
  const String channel = setting("apChannel"), width = setting("apWidth");
  if (!validHostname(hostname) || !updateApSettings(next, apName, apPassword) ||
      (channel.length() && !validApChannel(channel)) ||
      (width.length() && width != "20" && width != "40") ||
      (ipMode != "dhcp" && ipMode != "static")) {
    server.send(400, "text/plain",
                "Check the hostname, Ethernet mode, Wi-Fi name, password and channel.");
    return;
  }
  strlcpy(next.hostname, hostname.c_str(), sizeof(next.hostname));
  next.staticIP = ipMode == "static";
  const String ip = setting("ethIP"), mask = setting("ethMask"), gateway = setting("ethGateway"),
               dns = setting("ethDNS");
  if (ip.length() > 15 || mask.length() > 15 || gateway.length() > 15 || dns.length() > 15) {
    server.send(400, "text/plain", "IPv4 settings must contain at most 15 characters per field.");
    return;
  }
  // Preserve the saved static profile when DHCP is selected or restored.
  strlcpy(next.ip, ip.c_str(), sizeof(next.ip));
  strlcpy(next.mask, mask.c_str(), sizeof(next.mask));
  strlcpy(next.gateway, gateway.c_str(), sizeof(next.gateway));
  strlcpy(next.dns, dns.c_str(), sizeof(next.dns));
  if (next.staticIP) {
    IPAddress parsedIP, parsedMask, parsedGateway, parsedDNS;
    if (!parseStaticNetwork(next, parsedIP, parsedMask, parsedGateway, parsedDNS)) {
      server.send(400, "text/plain",
                  "Enter a valid static IPv4 address, subnet mask, gateway and DNS.");
      return;
    }
  }
  const String ackText = setting("ackMinutes");
  const long minutes = ackText.toInt();
  if (minutes < 1 || minutes > 10080 || String(minutes) != ackText) {
    server.send(400, "text/plain", "Enter an acknowledgment duration from 1 to 10080 minutes.");
    return;
  }
  String intervalText = setting("interval");
  bool validInterval = intervalText.length() > 0 && intervalText.length() <= 5;
  for (unsigned i = 0; i < intervalText.length(); i++)
    if (intervalText[i] < '0' || intervalText[i] > '9')
      validInterval = false;
  uint32_t interval = intervalText.toInt();
  String source = setting("source"), rip = setting("routerIP"), user = setting("routerUser"),
         password = setting("routerPassword"), portText = setting("routerPort");
  String transport = source == "glinet" ? "ssh" : "http";
  uint32_t routerAddress = 0;
  long port = portText.toInt();
  bool validRouter =
      (portText.length() > 0 && portText.length() <= 5 && port >= 1 && port <= 65535);
  for (unsigned i = 0; i < portText.length(); i++)
    if (portText[i] < '0' || portText[i] > '9')
      validRouter = false;
  if ((source != "local" && source != "openwrt" && source != "glinet") ||
      (transport != "http" && transport != "ssh") || password.length() > 128 ||
      user.length() > 64 ||
      (source != "local" &&
       (!owIP(rip.c_str(), routerAddress) || !user.length() ||
        (!restoring && !(password.length() || routerPassword.length())) || !validRouter))) {
    server.send(400, "text/plain",
                "Check scan source, OpenWrt connection, IPv4 address, port, user ID "
                "and password.");
    return;
  }
  String zone = setting("timeZone"), format = setting("timeFormat");
  String ntp = setting("timeServer");
  if (ntp == "custom")
    ntp = setting("customTimeServer");
  ntp.trim();
  if (!validTimeZone(zone.c_str()) || (format != "12" && format != "24") ||
      !validTimeServer(ntp.c_str())) {
    server.send(400, "text/plain",
                "Choose a valid time zone, 12/24-hour format and NTP hostname or IPv4 address "
                "(without a URL or port).");
    return;
  }
  uint32_t lo, hi;
  if (!validInterval || interval < 10 || interval > 86400 || (a.length() && a.length() < 12) ||
      a.length() > 128 || a != setting("confirm") ||
      (m != "auto" && !bounds(m, setting("subnet"), setting("start"), setting("end"), lo, hi))) {
    server.send(400, "text/plain",
                "Invalid settings. Check password length, confirmation, "
                "scan interval (10 to 86400 seconds), and scan range (maximum "
                "254 IPs; CIDR /24 through /30).");
    return;
  }
  if (prefs.putBytes("network-v1", &next, sizeof(next)) != sizeof(next)) {
    server.send(500, "text/plain", "Could not save network settings.");
    return;
  }
  if (channel.length() && prefs.putUChar("apChannel", channel.toInt()) != sizeof(uint8_t)) {
    server.send(500, "text/plain", "Could not save Wi-Fi channel.");
    return;
  }
  if (width.length() && prefs.putUChar("apWidth", width.toInt()) != sizeof(uint8_t)) {
    server.send(500, "text/plain", "Could not save Wi-Fi channel width.");
    return;
  }
  const String apOnBoot = setting("apOnBoot");
  if (apOnBoot.length() && prefs.putBool("apOnBoot", apOnBoot == "true") != sizeof(uint8_t)) {
    server.send(500, "text/plain", "Could not restore the Wi-Fi startup preference.");
    return;
  }
  bool saved = true;
  if (setting("forgetSSH") == "yes")
    saved &= prefs.putBool("forgetSSH", true) == sizeof(uint8_t);
  // Empty strings are valid for retained, unused router and range fields.
  const auto putString = [&](const char *key, const String &value) {
    prefs.putString(key, value);
    return prefs.getString(key, "") == value;
  };
  saved &= putString("transport", transport);
  saved &= putString("source", source);
  saved &= putString("routerIP", rip);
  saved &= putString("routerUser", user);
  if (password.length())
    saved &= putString("routerPwd", password);
  if (validRouter)
    saved &= prefs.putUShort("routerPort", port) == sizeof(uint16_t);
  saved &= putString("timeZone", zone);
  saved &= putString("timeFormat", format);
  saved &= putString("timeServer", ntp);
  saved &= prefs.putUInt("interval", interval) == sizeof(uint32_t);
  saved &= prefs.putUInt("ackMinutes", minutes) == sizeof(uint32_t);
  if (a.length() && prefs.putString("admin", a) != a.length()) {
    server.send(500, "text/plain", "Could not save the Admin password.");
    return;
  }
  saved &= putString("mode", m);
  saved &= putString("subnet", setting("subnet"));
  saved &= putString("start", setting("start"));
  saved &= putString("end", setting("end"));
  if (!saved) {
    server.send(
        500, "text/plain",
        "Some settings could not be saved. Review the settings and try again before restarting.");
    return;
  }
  if (restoring)
    server.send(200, "text/plain", "Settings restored. Watchtower is restarting.");
  else
    server.send(200, "text/html; charset=utf-8",
                String(ADMIN_PAGE_HEAD) +
                    "<main class='shell'><h1>Settings saved</h1><p>Restarting. "
                    "Reopen the dashboard on Ethernet or reconnect to the configured Wi-Fi access "
                    "point.</p>"
                    "<a href='/'>Back to Dashboard</a></main></body></html>");
  restartAt = millis() + 1500;
}
void save() {
  if (!auth())
    return;
  if (server.arg("csrf") != csrf) {
    server.send(403, "text/plain", "Invalid form token. Reload Admin.");
    return;
  }
  applySettings([](const char *key) { return server.arg(key); }, false);
}

static void restoreSettings() {
  if (!adminRequest(true))
    return;
  if (restartAt) {
    server.send(409, "text/plain", "Wait for the restart to finish.");
    return;
  }
  const String backup = prefs.getString("configBackup", "");
  if (!backup.length()) {
    server.send(404, "text/plain", "No configuration backup is saved.");
    return;
  }
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
      cJSON_ParseWithOpts(backup.c_str(), nullptr, true), cJSON_Delete);
  const cJSON *version = cJSON_GetObjectItemCaseSensitive(root.get(), "version");
  if (!cJSON_IsNumber(version) || version->valuedouble != 1) {
    server.send(400, "text/plain", "The saved backup is not a version 1 Watchtower configuration.");
    return;
  }

  struct ConfigField {
    const char *form, *section, *key;
    bool number;
  };
  const ConfigField fields[] = {{"hostname", nullptr, "hostname", false},
                                {"ipMode", "ethernet", "mode", false},
                                {"ethIP", "ethernet", "ip", false},
                                {"ethMask", "ethernet", "mask", false},
                                {"ethGateway", "ethernet", "gateway", false},
                                {"ethDNS", "ethernet", "dns", false},
                                {"apName", "accessPoint", "ssid", false},
                                {"source", "discovery", "source", false},
                                {"mode", "discovery", "mode", false},
                                {"subnet", "discovery", "subnet", false},
                                {"start", "discovery", "start", false},
                                {"end", "discovery", "end", false},
                                {"interval", "discovery", "intervalSeconds", true},
                                {"routerIP", "router", "ip", false},
                                {"routerPort", "router", "port", true},
                                {"routerUser", "router", "username", false},
                                {"timeZone", "time", "zone", false},
                                {"timeFormat", "time", "format", false},
                                {"timeServer", "time", "server", false},
                                {"ackMinutes", nullptr, "acknowledgmentMinutes", true}};
  String values[sizeof(fields) / sizeof(fields[0])];
  for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
    const auto &field = fields[i];
    const cJSON *section =
        field.section ? cJSON_GetObjectItemCaseSensitive(root.get(), field.section) : root.get();
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(section, field.key);
    if (field.number && cJSON_IsNumber(value) && value->valuedouble >= 0 &&
        value->valuedouble <= 86400 && uint32_t(value->valuedouble) == value->valuedouble)
      values[i] = String(uint32_t(value->valuedouble));
    else if (!field.number && cJSON_IsString(value))
      values[i] = value->valuestring;
    else {
      server.send(400, "text/plain", String("Missing or invalid setting: ") + field.key);
      return;
    }
  }
  const cJSON *ap = cJSON_GetObjectItemCaseSensitive(root.get(), "accessPoint");
  const cJSON *savedChannel = cJSON_GetObjectItemCaseSensitive(ap, "channel");
  String restoredChannel;
  if (savedChannel) {
    if (!cJSON_IsNumber(savedChannel) || savedChannel->valuedouble < 1 ||
        savedChannel->valuedouble > 11 ||
        int(savedChannel->valuedouble) != savedChannel->valuedouble) {
      server.send(400, "text/plain", "Invalid Wi-Fi channel in the configuration backup.");
      return;
    }
    restoredChannel = String(savedChannel->valueint);
  }
  const cJSON *savedWidth = cJSON_GetObjectItemCaseSensitive(ap, "widthMHz");
  if (savedWidth && (!cJSON_IsNumber(savedWidth) ||
                     (savedWidth->valuedouble != 20 && savedWidth->valuedouble != 40))) {
    server.send(400, "text/plain", "Invalid Wi-Fi channel width in the configuration backup.");
    return;
  }
  const cJSON *apOnBoot = cJSON_GetObjectItemCaseSensitive(ap, "enabledAtBoot");
  if (apOnBoot && !cJSON_IsBool(apOnBoot)) {
    server.send(400, "text/plain", "Invalid Wi-Fi startup preference in the backup.");
    return;
  }
  applySettings(
      [&](const char *key) -> String {
        if (!strcmp(key, "apOnBoot"))
          return apOnBoot ? String(cJSON_IsTrue(apOnBoot) ? "true" : "false") : String();
        if (!strcmp(key, "apChannel"))
          return restoredChannel;
        if (!strcmp(key, "apWidth"))
          return savedWidth ? String(savedWidth->valueint) : String();
        for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
          if (!strcmp(key, fields[i].form))
            return values[i];
        return "";
      },
      true);
}

void printConnectionInfo() {
  Serial.printf("Memory: free=%u minimum=%u largest=%u\n",
                static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  Serial.println("\nWatchtower - Intrusion Detection System (IDS) (ids-1) | Serial: 115200 baud | "
                 "send ? to repeat");
  if (apRunning)
    Serial.printf("Setup Wi-Fi: %s\nAdmin: http://%s/admin\n", networkSettings.apName,
                  WiFi.softAPIP().toString().c_str());
  if (networkReady())
    Serial.printf("Ethernet dashboard: http://%s/\n", ETH.localIP().toString().c_str());
  else
    Serial.println("Ethernet waiting for a link and IPv4 address.");
}
void setupAP() {
  if (apRunning || !apEnabled || apChangeAt)
    return;
  static uint32_t lastAttempt = 0;
  if (lastAttempt && millis() - lastAttempt < 5000)
    return;
  lastAttempt = millis();
  if (!WiFi.mode(WIFI_AP) ||
      !WiFi.softAP(networkSettings.apName, networkSettings.apPassword, apChannel)) {
    Serial.println("Setup hotspot failed to start; retrying in 5 seconds.");
    return;
  }
  if (esp_wifi_set_bandwidth(WIFI_IF_AP, apWidth == 40 ? WIFI_BW_HT40 : WIFI_BW_HT20) != ESP_OK)
    Serial.println("Could not apply the requested access point width.");
  apRunning = true;
  printConnectionInfo();
}
static void startEthernet() {
  Network.onEvent([](arduino_event_id_t event) {
    if (event == ARDUINO_EVENT_ETH_START)
      ETH.setHostname(networkSettings.hostname);
    else if (event == ARDUINO_EVENT_ETH_GOT_IP)
      ETH.setDefault();
    else if (event == ARDUINO_EVENT_ETH_DISCONNECTED || event == ARDUINO_EVENT_ETH_LOST_IP ||
             event == ARDUINO_EVENT_ETH_STOP)
      ethernetLinkGeneration.fetch_add(1);
  });
  // Waveshare ESP32-S3-ETH W5500: SCK, MISO, MOSI, CS; then IRQ and reset.
  SPI.begin(13, 12, 11, 14);
  ethernetStarted = ETH.begin(ETH_PHY_W5500, 1, 14, 10, 9, SPI);
  if (ethernetStarted && networkSettings.staticIP) {
    IPAddress ip, mask, gateway, dns;
    if (!parseStaticNetwork(networkSettings, ip, mask, gateway, dns) ||
        !ETH.config(ip, gateway, mask, dns)) {
      Serial.println("Static Ethernet configuration failed. Use the Wi-Fi AP to change settings.");
      ETH.end();
      ethernetStarted = false;
    }
  }
  Serial.println(ethernetStarted ? "W5500 initialized." : "W5500 initialization failed.");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.printf(
      "\nWatchtower - Intrusion Detection System (IDS) (ids-1) starting; reset reason: %d\n",
      int(esp_reset_reason()));
  prefs.begin("esp32ids", false);
  // Retire the removed notification feature's saved settings and credentials.
  nvs_handle_t retiredNotifications;
  if (nvs_open("ids-email", NVS_READONLY, &retiredNotifications) == ESP_OK) {
    nvs_close(retiredNotifications);
    if (nvs_open("ids-email", NVS_READWRITE, &retiredNotifications) == ESP_OK) {
      if (nvs_erase_all(retiredNotifications) != ESP_OK || nvs_commit(retiredNotifications) != ESP_OK)
        Serial.println("Could not clear retired notification settings.");
      nvs_close(retiredNotifications);
    }
  }
  if (prefs.getBytesLength("leds-v1") == sizeof(leds))
    prefs.getBytes("leds-v1", &leds, sizeof(leds));
  loadHeatmapSettings();
  adminPassword = prefs.getString("admin", defaultPassword);
  if (!adminPassword.length())
    adminPassword = defaultPassword;
  if (prefs.getBytesLength("network-v1") == sizeof(networkSettings))
    prefs.getBytes("network-v1", &networkSettings, sizeof(networkSettings));
  for (const char *key : {"botToken", "botChat"})
    if (prefs.isKey(key))
      prefs.remove(key);
  adminRealm = "Watchtower Admin " + String(esp_random(), HEX);
  csrf = String(esp_random(), HEX) + String(esp_random(), HEX);
  scanSource = prefs.getString("source", "local");
  if (scanSource != "openwrt" && scanSource != "glinet")
    scanSource = "local";
  routerIP = prefs.getString("routerIP", "192.168.1.1");
  routerTransport = prefs.getString("transport", "http");
  if (routerTransport != "ssh")
    routerTransport = "http";
  routerPort = prefs.getUShort("routerPort", 80);
  if (scanSource == "openwrt" && routerTransport == "ssh") {
    scanSource = "glinet";
    prefs.putString("source", scanSource);
  }
  if (scanSource == "glinet")
    routerTransport = "ssh";
  routerUser = prefs.getString("routerUser", "root");
  routerPassword = prefs.getString("routerPwd", "");
  scanIntervalSeconds = prefs.getUInt("interval", 60);
  if (scanIntervalSeconds < 10 || scanIntervalSeconds > 86400)
    scanIntervalSeconds = 60;
  timeZone = prefs.getString("timeZone", DEFAULT_TIME_ZONE);
  if (!validTimeZone(timeZone.c_str()))
    timeZone = DEFAULT_TIME_ZONE;
  timeFormat = prefs.getString("timeFormat", "24");
  if (timeFormat != "12" && timeFormat != "24")
    timeFormat = "24";
  timeServer = prefs.getString("timeServer", DEFAULT_TIME_SERVER);
  if (!validTimeServer(timeServer.c_str()))
    timeServer = DEFAULT_TIME_SERVER;
  mode = prefs.getString("mode", "auto");
  subnet = prefs.getString("subnet", "192.168.1.0/24");
  rangeStart = prefs.getString("start", "192.168.1.1");
  rangeEnd = prefs.getString("end", "192.168.1.254");
  lock = xSemaphoreCreateMutex();
  if (!lock) {
    Serial.println("Cannot allocate mutex");
    while (true)
      delay(1000);
  }
  policyStore.begin("ids-policies", false);
  ackMinutes = prefs.getUInt("ackMinutes", defaultAckMinutes);
  if (ackMinutes < 1 || ackMinutes > 10080)
    ackMinutes = defaultAckMinutes;
  pinMode(0, INPUT_PULLUP);
  startEthernet();
  apWidth = prefs.getUChar("apWidth", 20) == 40 ? 40 : 20;
  apStartsOnBoot = prefs.getBool("apOnBoot", true);
  apEnabled = apStartsOnBoot;
  apChannel = prefs.getUChar("apChannel", 1);
  if (apChannel < 1 || apChannel > 11)
    apChannel = 1;
  setupAP();
  server.on("/", HTTP_GET, dashboard);
  server.on("/api/devices", HTTP_GET, api);
  server.on("/api/scan", HTTP_POST, forceScan);
  server.on("/assets/ui.css", HTTP_GET,
            []() { gzipAsset(UI_CSS, sizeof(UI_CSS), "text/css; charset=utf-8"); });
  server.on("/assets/sort.js", HTTP_GET,
            []() { gzipAsset(SORT_JS, sizeof(SORT_JS), "text/javascript; charset=utf-8"); });
  server.on("/assets/theme.js", HTTP_GET,
            []() { gzipAsset(THEME_JS, sizeof(THEME_JS), "text/javascript; charset=utf-8"); });
  server.on("/assets/admin.js", HTTP_GET,
            []() { gzipAsset(ADMIN_JS, sizeof(ADMIN_JS), "text/javascript; charset=utf-8"); });
  server.on("/assets/policies.js", HTTP_GET, []() {
    gzipAsset(POLICIES_JS, sizeof(POLICIES_JS), "text/javascript; charset=utf-8");
  });
  server.on("/assets/timezones.json", HTTP_GET,
            []() { gzipAsset(TIMEZONES_JSON, sizeof(TIMEZONES_JSON), "application/json"); });
  server.on("/assets/heatmap.js", HTTP_GET, []() {
    gzipAsset(HEATMAP_JS, sizeof(HEATMAP_JS), "text/javascript; charset=utf-8");
  });
  server.on("/assets/dashboard.js", HTTP_GET, []() {
    gzipAsset(DASHBOARD_JS, sizeof(DASHBOARD_JS), "text/javascript; charset=utf-8");
  });
  server.on("/api/heatmap", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", heatmapJson());
  });
  server.on("/admin/heatmap", HTTP_POST, saveHeatmapSettings);
  server.on("/admin", HTTP_GET, admin);
  server.on("/admin/login", HTTP_GET, []() {
    server.sendHeader("Set-Cookie", "watchtower_logout=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Location", "/admin");
    server.send(303, "text/plain", "Sign in to Watchtower Admin.");
  });
  server.on("/admin/logout", HTTP_POST, []() {
    if (!adminRequest(true)) return;
    endAdminSession();
    server.sendHeader("Set-Cookie", "watchtower_logout=1; Path=/; HttpOnly; SameSite=Strict");
    server.send(200, "text/plain", "Logged out.");
  });
  server.on("/admin", HTTP_POST, save);
  server.on("/admin/ap", HTTP_POST, controlAccessPoint);
  server.on("/admin/ap", HTTP_GET, []() {
    if (adminRequest()) {
      wifi_bandwidth_t width;
      const int applied = apRunning && esp_wifi_get_bandwidth(WIFI_IF_AP, &width) == ESP_OK
                              ? (width == WIFI_BW_HT40 ? 40 : 20)
                              : 0;
      server.send(200, "application/json",
                  String("{\"running\":") + (apRunning ? "true" : "false") +
                      ",\"enabledAtBoot\":" + (apStartsOnBoot ? "true" : "false") +
                      ",\"pending\":" + (apChangeAt ? "true" : "false") +
                      ",\"channel\":" + apChannel + ",\"widthMHz\":" + applied + "}");
    }
  });
  server.on("/admin/devices", HTTP_POST, changeDevices);
  server.on("/admin/leds", HTTP_POST, saveLedSettings);
  server.on("/admin/leds", HTTP_GET, []() {
    if (adminRequest())
      server.send(200, "application/json",
                  String("{\"enabled\":") + (leds.enabled ? "true" : "false") + "}");
  });
  server.on("/admin/health", HTTP_GET, healthStatus);
  server.on("/admin/recovery", HTTP_GET, recoveryStatus);
  server.on("/admin/recovery", HTTP_POST, saveRecoverySettings);
  server.on("/admin/reboot", HTTP_POST, []() {
    if (!adminRequest(true))
      return;
    server.send(200, "text/plain", "System restarting.");
    restartAt = millis() + 1000;
  });
  server.on("/admin/stealth", HTTP_POST, []() {
    if (!adminRequest(true))
      return;
    LedSettings next = leds;
    if (server.arg("action") == "toggle")
      next.enabled = !leds.enabled;
    else if (server.arg("enabled") == "yes" || server.arg("enabled") == "no")
      next.enabled = server.arg("enabled") == "yes";
    else {
      server.send(400, "text/plain", "Choose a valid Stealth Mode action.");
      return;
    }
    if (prefs.putBytes("leds-v1", &next, sizeof(next)) != sizeof(next)) {
      server.send(500, "text/plain", "Could not save Stealth Mode.");
      return;
    }
    leds = next;
    server.send(200, "application/json", String("{\"enabled\":") + (leds.enabled ? "true" : "false") + "}");
  });
  server.on("/admin/backup", HTTP_POST, saveConfigurationBackup);
  server.on("/admin/backup/restore", HTTP_POST, restoreSettings);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  const char *requestHeaders[] = {"Cookie"};
  server.collectHeaders(requestHeaders, 1);
  server.begin();
  if (prefs.getBool("forgetSSH", false)) {
    Preferences trust;
    if (trust.begin("ids-ssh", false)) {
      if (trust.clear())
        prefs.putBool("forgetSSH", false);
      trust.end();
    }
  }
  setupRecovery();
  if (scanSource == "glinet")
    libssh_begin();
  unsigned stackSize = scanSource == "glinet" ? 51200 : 24576;
  if (xTaskCreate(scanTask, "network-scan", stackSize, nullptr, 1, &scannerTask) != pdPASS)
    scanError = "Unable to start scanner: restart the monitor.";
}
static void updateStatusLed() {
  static uint32_t lastUpdate = 0;
  static uint32_t lastColor = UINT32_MAX;
  const uint32_t now = millis();
  if (now - lastUpdate < 40)
    return;
  lastUpdate = now;

  uint32_t color = 0;
  if (leds.enabled && networkReady()) {
    if (xSemaphoreTake(lock, 0) != pdTRUE)
      return;
    bool unauthorized = false, acknowledged = false;
    const uint32_t seconds = uptimeSeconds();
    for (unsigned i = 0; i < deviceCount; ++i) {
      if (!devices[i].online)
        continue;
      const Policy policy = effectivePolicy(devices[i], seconds);
      unauthorized |= policy == Unknown;
      acknowledged |= policy == Acknowledged;
    }
    const LedState state = unauthorized                         ? LedUnauthorized
                           : acknowledged                       ? LedAcknowledged
                           : scanning && leds.show[LedActivity] ? LedActivity
                                                                : LedAuthorized;
    const uint32_t halfPeriod = state == LedAuthorized ? 500 : 160;
    if ((completed || state == LedActivity) && leds.show[state] && (now / halfPeriod) % 2 == 0)
      color = leds.colors[state];
    xSemaphoreGive(lock);
  }
  if (color != lastColor) {
    rgbLedWrite(21, ((color >> 16) & 255) / 5, ((color >> 8) & 255) / 5, (color & 255) / 5);
    lastColor = color;
  }
}

// GPIO0 is BOOT. The RESET switch directly drives chip enable.
static void serviceBootButton() {
  static bool previousRaw = false, pressed = false;
  static uint32_t changedAt = 0, pressedAt = 0;
  const uint32_t now = millis();
  const bool raw = digitalRead(0) == LOW;
  if (raw != previousRaw) {
    previousRaw = raw;
    changedAt = now;
  }
  if (now - changedAt < 30 || raw == pressed)
    return;
  pressed = raw;
  if (pressed) {
    pressedAt = changedAt;
  } else if (changedAt - pressedAt >= 2000) {
    ESP.restart();
  } else {
    acknowledgeOnlineDevices();
  }
}

void loop() {
  sampleHealth();
  serviceBootButton();
  updateStatusLed();
  server.handleClient();
  while (Serial.available()) {
    if (Serial.read() == '?')
      printConnectionInfo();
  }
  if (restartAt && int32_t(millis() - restartAt) >= 0)
    ESP.restart();
  static bool advertised = false;
  static bool clockStarted = false;
  if (apChangeAt && int32_t(millis() - apChangeAt) >= 0) {
    apChangeAt = 0;
    if (apRunning && (!apEnabled || apRestartPending)) {
      WiFi.softAPdisconnect(true);
      apRunning = false;
    }
    apRestartPending = false;
  }
  setupAP();
  if (networkReady()) {
    if (!clockStarted) {
      esp_sntp_set_time_sync_notification_cb(clockSyncCallback);
      if (timeServer == "time.nist.gov")
        configTime(0, 0, timeServer.c_str(), "time-a-g.nist.gov", "time-b-g.nist.gov");
      else if (timeServer == "pool.ntp.org")
        configTime(0, 0, timeServer.c_str(), "0.pool.ntp.org", "1.pool.ntp.org");
      else
        configTime(0, 0, timeServer.c_str());
      esp_sntp_set_sync_interval(3600000);
      clockStarted = true;
    }
    if (!advertised) {
      advertised = MDNS.begin(networkSettings.hostname);
      if (advertised) {
        MDNS.addService("http", "tcp", 80);
        printConnectionInfo();
      }
    }
  } else if (advertised) {
    MDNS.end();
    advertised = false;
  }
  recoveryLoopBeat();
  delay(2);
}
