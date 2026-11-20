#pragma once

constexpr unsigned HeatStateCount = 5;
struct HeatmapSettings {
  uint32_t version = 2;
  uint32_t colors[HeatStateCount] = {0x2D9B65, 0xD34354, 0xE7B844, 0x8592A5, 0x28313E};
  bool blink[HeatStateCount] = {false, true, true, false, false};
  uint8_t speed[HeatStateCount] = {10, 20, 4, 10, 10}; // Tenths of a flash per second.
  bool randomOnline = false;
};
static HeatmapSettings heatmapSettings;
static const char *const heatStateLabels[] = {"Authorized Online", "Unauthorized Online", "Acknowledged Online", "Offline", "Unused Port"};

static String heatmapJson() {
  String result = "{\"colors\":[";
  for (unsigned i = 0; i < HeatStateCount; ++i) {
    char color[8];
    snprintf(color, sizeof(color), "#%06lx", static_cast<unsigned long>(heatmapSettings.colors[i]));
    if (i) result += ',';
    result += json(String(color));
  }
  result += "],\"blink\":[";
  for (unsigned i = 0; i < HeatStateCount; ++i) {
    if (i) result += ',';
    result += heatmapSettings.blink[i] ? "true" : "false";
  }
  result += "],\"speed\":[";
  for (unsigned i = 0; i < HeatStateCount; ++i) {
    if (i) result += ',';
    result += String(heatmapSettings.speed[i]);
  }
  return result + "],\"randomOnline\":" + (heatmapSettings.randomOnline ? "true" : "false") + "}";
}
static String heatmapSettingsForm() {
  String p = "<details class='heatmap-settings'><summary>Customize Device Map</summary><p class='helper'>Choose colors and blink speeds for both switch views. Speeds are flashes per second; turn on Blink to animate a status.</p><form id='heatmapForm'><div class='heatmap-settings-grid'>";
  for (unsigned i = 0; i < HeatStateCount; ++i) {
    String index(i);
    char color[8];
    snprintf(color, sizeof(color), "#%06lx", static_cast<unsigned long>(heatmapSettings.colors[i]));
    p += "<fieldset class='heatmap-setting'><legend>" + String(heatStateLabels[i]) + "</legend><label class='field'>Color<input type='color' name='heatColor" + index + "' value='" + color + "'></label><label class='check'><input type='checkbox' name='heatBlink" + index + "' value='yes'" + (heatmapSettings.blink[i] ? " checked" : "") + ">Blink</label><label class='field' for='heatSpeed" + index + "'>Blink Speed <output for='heatSpeed" + index + "' data-heat-output='" + index + "'>" + String(heatmapSettings.speed[i] / 10.0f, 1) + " flashes/s</output></label><input type='range' id='heatSpeed" + index + "' name='heatSpeed" + index + "' data-heat-speed='" + index + "' min='2' max='25' step='1' value='" + String(heatmapSettings.speed[i]) + "'><div class='slider-labels'><span>Slow</span><span>Fast</span></div></fieldset>";
  }
  return p + "</div><label class='check'><input type='checkbox' name='heatRandomOnline' value='yes'" + String(heatmapSettings.randomOnline ? " checked" : "") + ">Randomize Online Device Blinking</label><p class='helper'>Stagger online devices so their flashes do not occur together. The selected speeds still apply.</p><button type='submit' class='btn btn-primary'>Save Device Map Settings</button><p id='heatmapFeedback' role='status'></p></form></details>";
}
static void loadHeatmapSettings() {
  HeatmapSettings saved;
  if (prefs.getBytesLength("heatmap-v2") == sizeof(saved)) {
    prefs.getBytes("heatmap-v2", &saved, sizeof(saved));
    if (saved.version != 2) return;
  } else {
    struct LegacyV1 {
      uint32_t version;
      uint32_t colors[HeatStateCount];
      bool blink[HeatStateCount];
      uint8_t speed[HeatStateCount];
    } legacy{};
    if (prefs.getBytesLength("heatmap-v1") != sizeof(legacy)) return;
    prefs.getBytes("heatmap-v1", &legacy, sizeof(legacy));
    if (legacy.version != 1) return;
    memcpy(saved.colors, legacy.colors, sizeof(saved.colors));
    memcpy(saved.blink, legacy.blink, sizeof(saved.blink));
    memcpy(saved.speed, legacy.speed, sizeof(saved.speed));
    saved.randomOnline = false;
  }
  bool valid = true;
  for (unsigned i = 0; i < HeatStateCount; ++i)
    valid &= saved.colors[i] <= 0xFFFFFF && saved.speed[i] >= 2 && saved.speed[i] <= 25;
  if (valid) heatmapSettings = saved;
}
static void saveHeatmapSettings() {
  if (!adminRequest(true)) return;
  HeatmapSettings next;
  next.randomOnline = server.arg("heatRandomOnline") == "yes";
  for (unsigned i = 0; i < HeatStateCount; ++i) {
    const String index(i), color = server.arg("heatColor" + index), speed = server.arg("heatSpeed" + index);
    int number = speed.toInt();
    if (color.length() != 7 || color[0] != '#' || strspn(color.c_str() + 1, "0123456789abcdefABCDEF") != 6 ||
        number < 2 || number > 25 || speed != String(number)) {
      server.send(400, "text/plain", "Choose valid Device Map colors and blink speeds.");
      return;
    }
    next.colors[i] = strtoul(color.c_str() + 1, nullptr, 16);
    next.blink[i] = server.arg("heatBlink" + index) == "yes";
    next.speed[i] = number;
  }
  if (prefs.putBytes("heatmap-v2", &next, sizeof(next)) != sizeof(next)) {
    server.send(500, "text/plain", "Could not save Device Map settings.");
    return;
  }
  heatmapSettings = next;
  if (prefs.isKey("heatmap-v1")) prefs.remove("heatmap-v1");
  server.send(200, "application/json", heatmapJson());
}
