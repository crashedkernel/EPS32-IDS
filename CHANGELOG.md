# Changelog

## 2026-11-20

### Added
- Optional System Recovery with persistent Admin controls, independent application/SSH stall checks, configurable timeouts, startup grace, and reboot-loop protection.
- Bounded browser sessions after Admin sign-in, isolated logout, and a sign-in banner for expired sessions.
- Collapsible Dashboard Device Map, Show All Devices filter reset, and randomized blink phases for online ports.
- Minimum-free heap and largest-block diagnostics, plus repeated snapshot-allocation and session tests.
- IP-mapped 256-port switch views in a fitted 32 × 8 layout numbered 000–255, with connection icons, dark-gray unused ports, customizable status colors and blink-speed sliders, and status-only legends.
- Online/offline Unauthorized and Acknowledged groups, with a consistently visible Admin Policy column and compact table sizing.
- Admin header Reboot/Logout icons, fresh authentication after logout, matching title-case section names, and sidebar hover descriptions.
- Responsive Health Status cards for CPU load, clock speed, memory, firmware flash, and configuration storage.
- Quick reboot and persistent RGB Stealth Mode controls in Admin.
- Alphabetical configuration navigation, fixed headers, themed scrollbars, and Back to top controls.
- A next-scan countdown beside the last completed scan.
- Sortable and searchable DNS names alongside device names in the Dashboard and Admin tables.
- Persistent RGB colors and switches for Unauthorized, Acknowledged, Authorized / clear, and Scan activity.

### Fixed
- Prevent background Admin requests from rotating Digest challenges and triggering repeated login dialogs.
- Rename visible heatmap labels to Device Map, preserve saved appearance preferences, reject truncated ICMP headers before inspection, and resolve a policy-type compiler warning.
- Keep switch selection and inventory navigation inside the main scroll frame so the full header stays visible.
- Fit all 32 switch columns without horizontal scrolling, explicitly label the restore-and-reboot action, and standardize button capitalization.
- Show Completed scan before Next scan, add a boxed Configuration navigation title, and apply green-to-red Health Status gradients.
- Toggle Stealth Mode from the saved system state, confirm the applied RGB state, and prevent repeated requests while saving.
- Enlarge the Configuration heading and navigation label by one size step.
- Copy the device report before sending it to a browser so a slow client cannot hold the scan mutex during network writes.
- Prefer PSRAM for inventory arrays, reuse the health-sampling buffer, and cache reverse DNS results to reduce repeated work during frequent scans.
- Discard a cached DNS name when its device changes IP address.
- Reject IPv6 input in IPv4-only network settings and preserve the static profile when DHCP settings are saved or restored.
- Report failed settings writes, derive router transport from the discovery source, and refresh Admin action availability correctly.
- Clear conflicting overview filters, use amber acknowledgment badges, and respect reduced-motion preferences.

### Removed
- Email/SMTP notifications, event queues, digests, provider credentials, and related Admin controls. Retired notification settings are cleared from internal storage on startup.

### Documentation
- Updated setup, device policy, LED, network, backup, health, recovery, and development instructions to match the current interface.
- Added sanitized Dashboard and Admin screenshots for the GitHub project overview.
- Clarified the meaning of firmware flash and NVS usage readings and the hardware control of Ethernet indicators.

## Initial release
- Ethernet discovery through local probes, OpenWrt, or GL.iNet client inventory, with SSH session reuse for GL.iNet polling.
- Device authorization, timed acknowledgment, bulk selection, history clearing, and onboard RGB status signaling.
- Configurable Ethernet, Wi-Fi access point, Admin authentication, and internal configuration backup.
