# Development and verification

The sketch targets the Waveshare ESP32-S3-ETH. Use the Arduino settings and dependency versions in the [README](../README.md).

## Source layout

| Source | Responsibility |
| --- | --- |
| `IntrusionMonitor/IntrusionMonitor.ino` | Arduino entry points, web routes, discovery task, Ethernet and Wi-Fi setup, LED controls, configuration backup, and Admin HTML |
| `Device.h` | Device records and array allocation that prefers PSRAM |
| `DevicePolicy.h` | Persistent authorization and temporary acknowledgment |
| `NetworkSettings.h` | Network-setting representation and hostname/static IPv4 validation |
| `AdminSession.h` | Bounded browser sessions after interactive Admin authentication |
| `HeatmapSettings.h` | Shared Device Map appearance validation and persistence |
| `RecoveryPolicy.h`, `RecoveryMonitor.h`, `RecoverySettings.h` | Recovery timing policy, independent monitor, and protected Admin controls |
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
| Device Map appearance and System Recovery | Immediate | Internal NVS |
| Authorization | Immediate | Separate policy namespace |
| Acknowledgment | Immediate | Memory until expiry, history clearing, or restart |
| Device history and DNS cache | Updated after a successful scan | Memory until history clearing or restart |
| Configuration backup | Replaces the previous backup | Internal NVS |

The configuration backup covers Ethernet, AP startup and radio settings, discovery, router connection fields, time, and acknowledgment duration. Passwords, authorization entries, LED preferences, Device Map appearance, and recovery preferences remain in their separately saved state during restore.

Settings use several NVS keys. A storage failure can leave a partially saved profile. The Admin response reports the failure and requires reviewing the settings before restarting.

See the [verification record](verification.md) for the most recent checks and remaining hardware acceptance work.

## Web interface

The Dashboard and device API are reachable by LAN clients. Admin routes authenticate the request, and modifying routes also validate a form token. The API streams a stable snapshot of at most 254 devices. Browser scripts use text nodes for device names and other reported values.

The main content area scrolls independently of the header. Device tables retain consistent columns with a focusable horizontal scroller on narrow screens; Device Map ports select the matching inventory result. Theme and text-size preferences are stored in the viewing browser.

## Release checks

The checks below describe the acceptance workflow. Record the results separately when running a release verification.

1. Regenerate web assets, compile with the supported board options, and run `git diff --check`.
2. Load the Dashboard by Ethernet IP and through the setup AP. Check inventory search, sorting, device groups, DNS names, clock, System Uptime, last/next scan information, and Scan now. Exercise heatmap selection and Back to top.
3. Verify that interactive Admin sign-in challenges unauthenticated requests and background endpoints return 401 without a login challenge. Sign in and open every navigation section. At desktop and phone widths, check for clipped controls or columns. Toggle dark/light mode and text sizing, then check scrollbar contrast and fixed-header scrolling.
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

## Device Map and LED checks

`web/heatmap.js` is shared by both pages. It uses the same status colors, accessible device labels, focus preservation, and tile-selection callbacks. Admin tile selection resets its device-group filter and searches its inventory for the selected device.

Run `python3 tools/test_leds.py` with a C++17 compiler. It exercises the actual Stealth handler and RGB output decisions with simulated storage and a captured LED driver call: saved-state toggles, stale checkbox input, failed persistence, invalid requests, authentication rejection, turning RGB off, and restoring the configured pattern. This does not visually inspect physical LEDs.

The email feature and worker are removed. Startup clears only its retired `ids-email` namespace; network settings, credentials for retained features, device policies, and LED preferences are preserved. The original Huge APP partition layout remains in use.

## Switch mapping and Admin sessions

The shared Device Map always renders 256 slots, arranged into 32 columns and eight rows. Host octets 0–255 map directly to ports 000–255. It groups records by IPv4 prefix and offers a range selector when needed. For a reused IP, an online record takes priority; selecting that port searches by IP to include its history. The full inventory remains the authority for records without an eligible host address. Per-status colors, Blink switches, and speed sliders configure the shared CSS variables. Defaults blink unauthorized ports at 2 Hz and acknowledged ports at 0.4 Hz, with reduced-motion support.

`deviceSort.inGroup` applies exact presence and policy filters on both pages. Admin's Policy column stays near the left edge; fixed column widths, ellipses with titles, wrapping actions, and a focusable horizontal scroller keep every field accessible.

`python3 tools/test_auth.py` exercises the actual authentication gate and login/logout handlers with simulated WebServer authentication: logged-out cookies, browser-session isolation, fresh-login redirection, CSRF, and rejected credentials. Core Digest hashing is provided by Arduino WebServer. Digest is used only for deliberate Admin page sign-in. `AdminSession.h` then provides eight fixed session slots with 128-bit tokens, constant-time token comparison, and an eight-hour monotonic idle timeout. Background requests never challenge, preventing nonce replacement by parallel polls. Logout revokes the current token and leaves other browser sessions active. The Sign In endpoint clears the logged-out marker. Restarting clears all sessions and changes the realm. Credentials and settings are not erased.

## Fitted switch view and customization

`HeatmapSettings.h` stores five colors, blink switches, and speeds (integer tenths of flashes/s) in the `heatmap-v2` NVS blob. `/api/heatmap` exposes only these appearance preferences; `/admin/heatmap` requires Admin authentication and CSRF. Valid speeds are 2–25 tenths/s; failed validation or persistence leaves active preferences unchanged. Settings apply on the current page after saving and refresh on other open pages every 30 seconds. Configuration backup retains these preferences separately, like RGB LED preferences.

Run `python3 tools/test_heatmap.py` to exercise the actual save handler for all five statuses, slider boundaries, malformed values, persistence failures, and authentication. The standalone LED and auth checks remain available.

`watchtowerScrollTo` targets the main scroll frame directly and resets any outer document scroll. Switch selections and inventory links use this helper instead of `scrollIntoView`, which can scroll ancestors and shift the fixed header. Sidebar section links also use the helper. The switch uses 32 fractional-width columns and eight responsive rows with container sizing; small displays keep all ports in-frame, using compact icons and accessible labels.

The Configuration Backup action is labeled **Restore Saved Configuration & Reboot Board** and uses the existing validated restore-and-restart endpoint. Button labels use consistent title case. Restoring a backup is not part of routine UI verification because it can change live network settings.

## Authentication, Device Map, and resource review

A review of Arduino WebServer 3.3.12 found its Digest nonce and opaque values are shared across requests. Challenging every protected polling request could invalidate another in-flight request and repeatedly prompt Safari/Chrome. The session gate confines challenges to GET `/admin`; expired background requests return 401 without `WWW-Authenticate`. The browser shows a sign-in banner. Session cookies are HttpOnly, SameSite=Strict, and restricted to `/admin`. This remains a trusted-LAN HTTP interface; cookies are not encrypted by HTTP.

Device Map appearance v2 adds `randomOnline`. The loader preserves v1 colors/rates with randomization off, and a successful v2 save retires the old key. Online blinking uses deterministic device-specific CSS animation phases, not one JavaScript timer per port. Dashboard collapse state is stored locally; collapsed maps skip port reconstruction while inventory data continues updating.

`python3 tools/test_snapshot.py` tests the actual Device allocator and RouterSnapshot expansion using a tracked host allocator, including 1000 cycles to capacity, PSRAM fallback, and failed allocation. It requires the installed Arduino SDK’s portable cJSON header. All allocated blocks must be released. `test_auth.py` checks 1000 authenticated polls without another challenge, independent browsers, expiry, logout, CSRF, and bounded storage. Existing map/LED tests cover saved appearance migration and output state.

Health and Serial diagnostics now include minimum-free heap and largest free block. Short live sampling can detect an obvious downward trend but cannot prove long-term leak absence. Read [the 19 November review](code-review-2026-11-19.md) for scope and remaining limits.

### Recovery monitor regression checks

Run `python3 tools/test_recovery.py`. It compiles the actual recovery monitor with simulated time, task allocation, retained memory, and resets under UndefinedBehaviorSanitizer. Coverage includes disabled defaults, allocation failure, startup grace, stuck SSH, main-loop stall, routine SSH scope cleanup, three-restart pause, manual restart, healthy-runtime reset, corrupt preferences, and millisecond rollover. The monitor avoids network calls, report locks, flash writes, and logging in the recovery path.

For hardware acceptance, enable System Recovery in Admin, confirm grace and active monitoring, then test controlled stalls on an isolated development device. Do not deliberately hang a production scanner. Confirm the recovery cause after restart and persistence after power cycling.

## Local regression suite

Run the focused checks before publishing changes:

```sh
python3 tools/test_auth.py
python3 tools/test_heatmap.py
python3 tools/test_leds.py
python3 tools/test_snapshot.py
python3 tools/test_recovery.py
python3 tools/build_web_assets.py
```

These tests use a C++17 host compiler and simulated SDK services; the snapshot test also requires the Arduino ESP32 3.3.12 cJSON headers. They do not change deployed settings or prove long-duration hardware stability. Compile the firmware afterward using the supported board options above.

## Public screenshots

README screenshots use synthetic device records and masked network identifiers. Never capture deployed credentials, real client names, addresses, router identity, or configuration exports for publication. Generate screenshots from a local demo fixture using the current web assets and Admin page renderer; visually inspect the final images before committing them.
