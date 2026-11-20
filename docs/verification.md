# Verification record

Latest repository review: 20 November 2026. Earlier entries below record the build and checks performed at each stage.

## Build and source checks

- Compiled for ESP32S3 Dev Module with 16 MB flash, OPI PSRAM, Huge APP, hardware USB CDC/JTAG, and CDC on boot.
- Program size was 1,379,391 bytes (43% of the application partition). Global variables used 54,792 bytes.
- Verified that every generated gzip asset decompresses to the corresponding web source.
- Checked documentation links, Markdown code fences, and Git whitespace.
- Ran focused validation checks against the actual network settings header for hostname syntax, valid static IPv4 profiles, subnet and gateway boundaries, and IPv6 rejection.

## Browser checks

The Admin HTML was rendered from the firmware's page-building functions with sample settings. Both pages were exercised in Chrome with 254 sample devices, including long names and a name containing HTML markup. Tests used desktop and narrow phone-sized content, light/dark themes, and text sizing up to 150%.

Checks passed for device rendering, sorting, groups, heatmap selection, overview filtering, navigation anchors, health cards, static-IP field requirements, selection controls, authenticated form tokens in Admin actions, fixed-header scrolling, themed scrollbars, and Back to top. No browser script errors were reported. Desktop and narrow-content screenshots were also inspected.

Admin actions used simulated responses. These checks validate browser behavior without changing a deployed network configuration.

## Live system checks

The reviewed build was uploaded and flash verification passed. The live Dashboard, device API, and shared browser assets responded. Admin read and modifying routes rejected unauthenticated requests.

Five consecutive device reports completed with 114 tracked devices, advancing scan numbers, no reported scan error, and approximately 0.7-second response times.

## Remaining hardware acceptance checks

Authenticated changes to Ethernet/AP credentials, restore behavior across restart, LED colors, physical button gestures, router reconnection, and long-duration memory stability require the acceptance workflow in the development guide. This review did not exercise those changes on the deployed network.

The default settings are documented in the README. Existing saved settings were retained during the firmware update.

## Cleanup, Admin heatmap, and Stealth update

Removed SMTP/provider configuration, notification workers and event hooks, email UI and routes, and obsolete tests. The firmware clears retired notification settings on startup. The Dashboard and Admin share one heatmap renderer. Configuration headings were enlarged by one size step.

The actual Stealth route and RGB update function passed host checks for saved-state toggling, stale UI input, write failure, invalid requests, authentication, RGB off, and pattern restoration. Physical LED appearance has not been observed. Authenticated live changes require the current Admin password.

Admin desktop and narrow-content fixtures each passed 34 checks, including heatmap tile selection, larger Configuration text, Stealth off/on confirmation, failed-save handling, and existing form/table controls. The Dashboard passed 21 checks after moving its heatmap to the shared renderer. All changed gzip assets matched their source.

Compilation passed at 1,382,419 bytes (43% of the application partition), with 54,792 bytes of static RAM. Upload and flash verification passed. The live Dashboard and shared heatmap asset responded; scan 4 reported 113 devices and no scan error. Email routes returned 404. LED status and Stealth writes rejected unauthenticated requests with 401. Retained system settings were preserved.

## Switch view and Admin update — 19 November 2026

Implemented the confirmed mapping port 10 → 192.168.2.10, a fixed 64 × 4 layout, dark-gray unused ports, connection icons, and separate IP ranges. Added exact online/offline policy groups, an accessible Admin policy table, header Reboot/Logout controls, navigation descriptions with matching section titles, gradient health bars, and reordered scan timing bubbles.

Host authentication checks exercise the actual gate and handlers using simulated credential verification. Live password entry, physical LED appearance, and logout in an authenticated browser still require the current Admin credentials. No saved password or device policy was changed for testing.

Admin desktop and phone fixtures each passed 63 checks, including all nine groups and visible policy badges, title/tooltip matching, header icons, fixed 64 × 4 layout, sparse IP mapping, unused ports, separate IP ranges, and retained Stealth behavior. Dashboard desktop (normal motion) and phone (reduced motion) fixtures each passed 39 checks, including all groups, scan bubble order, switch icons, and animation preferences. The browser checks caught and resolved a missing overview-filter variable before release. A switch-view screenshot was visually inspected.

The final build used 1,388,715 bytes (44% of the application partition) and 54,808 bytes of static RAM. Upload and flash verification passed. The live Dashboard served the corrected timing order, the switch assets contained IP-octet mapping and 64-column layout, and scan 6 reported 115 devices with no scan error. Unauthenticated logout was rejected (401), the Sign In route redirected correctly (303), and the logged-out cookie gate rejected Admin access without triggering an automatic Digest prompt. No live Admin credentials were used or changed.

## Fitted switch and custom appearance — 19 November 2026

The latest layout supersedes the earlier 64 × 4 switch: it uses 32 × 8 ports numbered 000–255, with direct IP-octet mapping and no horizontal grid overflow. Switch selection scrolls only the main frame, preserving the complete header. The restore action explicitly reads Restore Saved Configuration & Reboot Board. Buttons use title case.

Admin desktop and phone fixtures each passed 70 checks; Dashboard desktop passed 42 checks with normal animations. These include fitted grid dimensions, numbering, IP mapping, header position after selection, status-only legends, color persistence requests, and slider output/application for unused ports. Host heatmap tests passed for all five statuses, valid speed limits, malformed input, failed writes, and authentication. Restoring a live backup and authenticated appearance persistence on the deployed system were not exercised; these require the current Admin credentials. A desktop screenshot was visually checked.

The phone Dashboard also passed 42 checks, including no horizontal switch overflow and a fully visible header after device selection. The final firmware compiled at 1,394,519 bytes (44% of the application partition) and 54,840 bytes of static RAM. Upload and flash verification passed. Live scan 7 reported 115 devices with no scan error. The public appearance API returned valid preferences for all five statuses; deployed assets included the fitted layout, 000-based mapping, and content-frame navigation. Unauthenticated heatmap writes were rejected (401). Saved settings and device policies were retained.

## Session stability and Device Map review — 19 November 2026

The repeated-login path was traced to shared Digest challenge state being replaced by background polling. Admin now issues bounded browser sessions after interactive authentication; missing/expired background sessions return 401 without a browser challenge. Sessions are isolated by browser and expire after eight idle hours.

Source review covered firmware ownership and bounds, scanner/report locking, HTTP/cJSON cleanup, SSH channel/event cleanup, frontend timers, and preferences. The native snapshot test released all 5001 allocations across 1000 capacity cycles and failure cases. Auth tests passed 1000 polls with no new challenge, isolated logout, expiry, bad credentials, and bounded storage. Appearance migration and RGB tests passed. UndefinedBehaviorSanitizer checks passed; AddressSanitizer was unavailable because its macOS runtime failed to initialize. See the code-review report for limits.

Admin desktop/mobile fixtures each passed 73 checks; Dashboard desktop/mobile fixtures each passed 46 checks. Expired-session UI showed the Sign In banner successfully. The final warnings-enabled build completed without warnings at 1,397,751 bytes (44% of the application partition) and 55,224 bytes of static RAM. Upload and flash verification passed.

Live sampling covered 12 samples over 110.7 seconds, spanning scans 7–18 with 115 devices and no scan errors. Free heap stayed between 161,068 and 161,144 bytes; minimum-free heap stayed at 148,848 bytes and the largest free block stayed at 112,628 bytes. No progressive decline was observed in this short run. It does not rule out long-duration leaks or fragmentation. Background health requests were repeatedly rejected with 401 and no WWW-Authenticate header, confirming they cannot trigger a new browser challenge.

## System Recovery — 19 November 2026

- Actual recovery monitor passed simulated-time/reset tests under UndefinedBehaviorSanitizer: disabled mode, task allocation failure, startup grace, SSH and loop stalls, three-restart pause, manual restart, healthy-runtime reset, corrupt settings, and clock rollover.
- All existing authentication, Device Map, snapshot allocation, and LED regression tests passed.
- Admin fixture passed 77 browser checks at desktop and 390-pixel content widths, including recovery defaults, protected save fields, status rendering, and responsive layout. Actions used simulated responses.
- ESP32-S3 build: 1,403,103 bytes of program storage (44%); 55,280 bytes of static RAM (16%). No project-source compiler warnings were reported; upstream library warnings remain.
- Automatic recovery remains disabled by default. Live fault injection and authenticated enabling are reserved for isolated hardware acceptance; simulated fault tests do not establish recovery from complete scheduler or power failure.

The recovery build was uploaded over USB and flash verification passed. Live device reports responded after reboot; the recovery endpoint returned 401 without an authentication challenge when accessed without an Admin session. No intentional live stall was injected.

## Repository publication review — 20 November 2026

- Tidied recovery-monitor formatting and reconciled README/development instructions with current session handling, Device Map naming, backup exclusions, responsive tables, and immediate recovery settings.
- All five host regression suites passed again: Admin sessions, map appearance, RGB/Stealth, snapshot allocation, and recovery.
- Regenerated browser assets and compiled the firmware with all warnings enabled: 1,403,103 bytes of program storage (44%) and 55,280 bytes of static RAM (16%). Dependency warnings remain; no project-source warnings were reported.
- Visually inspected three public screenshots from a local fixture. All device records are synthetic and network identifiers are hidden. No deployed settings or credentials were used.
- Checked generated gzip contents, local documentation links, and Git whitespace before publication. The previously uploaded behavior is unchanged by this formatting/documentation pass; no new firmware upload was needed.
