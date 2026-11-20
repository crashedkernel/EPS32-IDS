#pragma once

static const char *recoveryReason(RecoveryFault fault) {
  return fault == RecoveryFault::MainLoop ? "Application loop stalled" :
         fault == RecoveryFault::SSH ? "SSH operation stalled" : "None recorded";
}
static void recoveryStatus() {
  if (!adminRequest(false)) return;
  const auto copy = recoverySnapshot();
  const uint32_t now = millis(), elapsed = now - copy.state.armedAt;
  server.send(200, "application/json", String("{\"enabled\":") + (copy.settings.enabled ? "true" : "false") +
    ",\"ready\":" + (copy.state.ready ? "true" : "false") + ",\"paused\":" + (copy.state.paused ? "true" : "false") +
    ",\"graceSeconds\":" + (elapsed < RecoveryGraceMs ? (RecoveryGraceMs - elapsed + 999) / 1000 : 0) +
    ",\"loopAgeSeconds\":" + uint32_t(now - copy.state.loopAt) / 1000 +
    ",\"sshBusy\":" + (copy.state.sshBusy ? "true" : "false") +
    ",\"sshAgeSeconds\":" + (copy.state.sshBusy ? uint32_t(now - copy.state.sshAt) / 1000 : 0) +
    ",\"restarts\":" + copy.state.restarts + ",\"lastRecovery\":" + json(recoveryReason(copy.state.last)) + "}");
}
static void saveRecoverySettings() {
  if (!adminRequest(true)) return;
  RecoverySettings next;
  next.enabled = server.arg("recoveryEnabled") == "yes";
  const String loop = server.arg("recoveryLoopSeconds"), ssh = server.arg("recoverySshSeconds");
  next.loopSeconds = loop.toInt();
  next.sshSeconds = ssh.toInt();
  if (!validRecoverySettings(next) || loop != String(next.loopSeconds) || ssh != String(next.sshSeconds)) {
    server.send(400, "text/plain", "Set the application timeout to 30–300 seconds and SSH timeout to 60–900 seconds.");
    return;
  }
  if (prefs.putBytes("recovery-v1", &next, sizeof(next)) != sizeof(next)) {
    server.send(500, "text/plain", "Could not save recovery settings.");
    return;
  }
  if (!applyRecoverySettings(next)) {
    server.send(503, "text/plain", "Settings saved, but the recovery monitor could not start. Retry or restart the system.");
    return;
  }
  server.send(200, "text/plain", next.enabled ? "Automatic recovery enabled. Startup grace is two minutes." : "Automatic recovery disabled.");
}
static String recoverySettingsSection() {
  const auto copy = recoverySnapshot();
  return String("<section class='card settings-card' id='system-recovery'><h2>System Recovery</h2>") +
    "<p class='helper'>Automatically reboot if the application stops completing its loop or an SSH operation stays stuck. Checks run independently of the web interface.</p>"
    "<form id='recoveryForm'><label class='check'><input type='checkbox' name='recoveryEnabled' value='yes'" +
    (copy.settings.enabled ? " checked" : "") + ">Enable Automatic Recovery</label><div class='form-grid'>"
    "<label class='field'>Application Timeout (Seconds)<input type='number' name='recoveryLoopSeconds' min='30' max='300' required value='" + String(copy.settings.loopSeconds) + "'></label>"
    "<label class='field'>SSH Operation Timeout (Seconds)<input type='number' name='recoverySshSeconds' min='60' max='900' required value='" + String(copy.settings.sshSeconds) + "'></label></div>"
    "<p class='helper'>A two-minute startup grace allows normal initialization. Disconnected Ethernet, failed logins, and routine SSH reconnects do not trigger recovery by themselves. This feature pauses after three recovery restarts without five healthy minutes; save enabled settings or reboot manually to resume.</p>"
    "<button type='submit' class='btn btn-primary'>Save Recovery Settings</button><p id='recoveryFeedback' role='status'></p></form>"
    "<p id='recoveryStatus' class='helper' role='status'>Loading recovery status…</p></section>";
}
