# Development and verification

The sketch targets the Waveshare ESP32-S3-ETH. Use the Arduino settings and dependency versions in the [README](../README.md).

## Source layout

| Source | Responsibility |
| --- | --- |
| `IntrusionMonitor/IntrusionMonitor.ino` | Arduino entry points, web routes, discovery task, Ethernet and Wi-Fi setup, LED controls, configuration backup, and Admin HTML |
| `Device.h` | Device records and array allocation that prefers PSRAM |
| `DevicePolicy.h` | Persistent authorization and temporary acknowledgment |
| `NetworkSettings.h` | Network-setting representation and hostname/static IPv4 validation |
| `Health.h` | CPU sampling and authenticated resource information |
| `OpenWrt*.h` | OpenWrt discovery, router report storage, and GL.iNet SSH connection handling |
| `RouterCommand.h`, `GLClientStream.h`, `RouterSSHReport.h` | Router-side report reduction, bounded parsing, and SSH channel callbacks |
| `TimeSettings.h`, `TimeZones.h` | Time-server and time-zone validation |
| `web/` | Dashboard HTML, browser scripts, theme, text sizing, and CSS |
| `tools/build_web_assets.py` | Reproducible gzip assets for `WebAssets.h` |

Run `python3 tools/build_web_assets.py` after changing browser assets. Commit the regenerated `IntrusionMonitor/WebAssets.h` with its sources. Keep deployed credentials and configuration exports out of the repository.

## Compile

```sh
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=huge_app,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc \
  IntrusionMonitor
```

Use the same board options when uploading. Disabling full-flash erasure preserves the NVS configuration, whitelist, and internal backup.

## Settings and persistence

| Setting or state | Apply behavior | Persistence |
| --- | --- | --- |
| Ethernet, hostname, discovery, router, time, acknowledgment duration, Admin password | Save settings & restart | Internal NVS |
| AP name, password, channel, width | Save and restart AP, or main settings save | Internal NVS |
| AP Start/Stop | Immediate after the response is sent | Current session |
| AP persistent enable/disable | Applies to the current session and subsequent startup | Internal NVS |
| RGB colors, state toggles, Stealth Mode | Immediate | Internal NVS |
| Authorization | Immediate | Separate policy namespace |
| Acknowledgment | Immediate | Memory until expiry, history clearing, or restart |
| Device history and DNS cache | Updated after a successful scan | Memory until history clearing or restart |
| Configuration backup | Replaces the previous backup | Internal NVS |

The configuration backup covers Ethernet, AP startup and radio settings, discovery, router connection fields, time, and acknowledgment duration. Passwords, authorization entries, and LED preferences remain in their separately saved state during restore.

Settings use several NVS keys. A storage failure can leave a partially saved profile. The Admin response reports the failure and requires reviewing the settings before restarting.

See the [verification record](verification.md) for the most recent checks and remaining hardware acceptance work.

## Web interface

The Dashboard and device API are reachable by LAN clients. Admin routes authenticate the request, and modifying routes also validate a form token. The API streams a stable snapshot of at most 254 devices. Browser scripts use text nodes for device names and other reported values.

The main content area scrolls independently of the header. Mobile device tables display labeled fields, and heatmap tiles select the matching inventory result. Theme and text-size preferences are stored in the viewing browser.

## Release checks

The checks below describe the acceptance workflow. Record the results separately when running a release verification.

1. Regenerate web assets, compile with the supported board options, and run `git diff --check`.
2. Load the Dashboard by Ethernet IP and through the setup AP. Check inventory search, sorting, device groups, DNS names, clock, System Uptime, last/next scan information, and Scan now. Exercise heatmap selection and Back to top.
3. Verify that Admin challenges unauthenticated requests. Sign in and open every navigation section. At desktop and phone widths, check for clipped controls or columns. Toggle dark/light mode and text sizing, then check scrollbar contrast and fixed-header scrolling.
4. On a test system, authorize and deauthorize one device, then exercise bulk selection, acknowledgment, and selected/all history clearing. Check the inventory and RGB state after a completed scan. Verify acknowledgment expiry and BOOT tap/hold behavior.
5. Apply a color and toggle for each RGB state. Test Stealth Mode and restart to confirm persistence. Ethernet LINK and ACT are hardware-driven.
6. Check CPU sampling, clock, memory, and flash/configuration storage cards. CPU needs successive samples. Compare firmware and heap values with the corresponding system APIs. Treat NVS byte availability as an estimate from entry counts.
7. Save a configuration backup, change a test setting, then restore it. Confirm the restart and expected settings. Exercise invalid values before applying valid DHCP/static IPv4, hostname, router, time, and password changes.
8. Through Ethernet Admin, change AP SSID/channel/width, test Start and Stop, and verify the persistent startup preference across a reboot. Check a failed or invalid request leaves useful feedback.
9. In GL.iNet mode, verify consecutive scans, briefly disconnect Ethernet, reconnect it, and confirm discovery resumes. Check router logs to confirm normal polling reuses the SSH login. Test a changed router host key only on a controlled test endpoint.
10. Exercise the maximum supported inventory and frequent scans with repeated Dashboard requests. Observe scan progression, errors, uptime, and memory over an extended run.

## Memory and scan responsiveness

Device arrays prefer PSRAM and fall back to byte-addressable internal memory if PSRAM allocation fails. The web API reuses a bounded device snapshot and releases the scan mutex before writing it to the network. The scan task swaps completed tables under the mutex, then releases the old report. Health sampling reuses a fixed task buffer. Reverse DNS successes are refreshed after five minutes, and failed lookups are retried after one minute.

The scan task owns the SSH session and channels. TCP keepalives and Ethernet link-generation checks help discard stale transports, with reconnection attempted by a later scan. Scan failures retain the last successful report.

When investigating a hang, record the discovery source, device count, interval, error, uptime, and available heap. The 254-device capacity remains in place. A short successful scan run establishes basic responsiveness for that run. Longer monitoring is required to assess intermittent hangs.
