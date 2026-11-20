#pragma once

// Fixed storage: polling never allocates new sessions or rotates Digest challenges.
struct AdminSession {
  char token[33] = {};
  uint64_t touched = 0;
};
static AdminSession adminSessions[8];
constexpr uint64_t adminIdleMs = 8ULL * 60 * 60 * 1000;
static uint64_t adminNowMs() { return esp_timer_get_time() / 1000ULL; }
static String adminCookie(const char *name) {
  const String cookies = server.header("Cookie"), prefix = String(name) + "=";
  int begin = 0;
  while (begin < int(cookies.length())) {
    int end = cookies.indexOf(';', begin);
    if (end < 0) end = cookies.length();
    String item = cookies.substring(begin, end);
    item.trim();
    if (item.startsWith(prefix)) return item.substring(prefix.length());
    begin = end + 1;
  }
  return "";
}
static bool adminTokenEquals(const char *stored, const String &token) {
  if (!stored[0] || token.length() != 32) return false;
  unsigned difference = 0;
  for (unsigned i = 0; i < 32; ++i) difference |= static_cast<unsigned char>(stored[i] ^ token[i]);
  return difference == 0;
}
static bool validAdminSession() {
  const String token = adminCookie("watchtower_session");
  const uint64_t now = adminNowMs();
  for (auto &session : adminSessions) {
    if (session.token[0] && now - session.touched >= adminIdleMs) session.token[0] = 0;
    if (adminTokenEquals(session.token, token)) { session.touched = now; return true; }
  }
  return false;
}
static void startAdminSession() {
  const uint64_t now = adminNowMs();
  AdminSession *slot = nullptr;
  for (auto &session : adminSessions) {
    if (!session.token[0] || now - session.touched >= adminIdleMs) { slot = &session; break; }
    if (!slot || session.touched < slot->touched) slot = &session;
  }
  snprintf(slot->token, sizeof(slot->token), "%08lx%08lx%08lx%08lx",
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
  slot->touched = now;
  server.sendHeader("Set-Cookie", String("watchtower_session=") + slot->token + "; Path=/admin; HttpOnly; SameSite=Strict");
}
static void endAdminSession() {
  const String token = adminCookie("watchtower_session");
  for (auto &session : adminSessions)
    if (adminTokenEquals(session.token, token)) session.token[0] = 0;
  server.sendHeader("Set-Cookie", "watchtower_session=; Path=/admin; Max-Age=0; HttpOnly; SameSite=Strict");
}
