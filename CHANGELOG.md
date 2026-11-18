# Changelog

## Unreleased

### Added

- Responsive Health Status cards for CPU load, clock speed, memory, firmware flash, and configuration storage.
- Quick reboot and persistent RGB Stealth Mode controls in Admin.
- Alphabetical configuration navigation, fixed headers, themed scrollbars, and Back to top controls.
- Online, online-authorized, offline, and offline-authorized device groups on both pages.
- A next-scan countdown beside the last completed scan.
- A device heatmap with status colors, device details, and links to inventory results.
- Sortable and searchable DNS names alongside device names in the Dashboard and Admin tables.
- Persistent RGB colors and switches for Unauthorized, Acknowledged, Authorized / clear, and Scan activity.

### Fixed

- Copy the device report before sending it to a browser so a slow client cannot hold the scan mutex during network writes.
- Prefer PSRAM for inventory arrays, reuse the health-sampling buffer, and cache reverse DNS results to reduce repeated work during frequent scans.
- Discard a cached DNS name when its device changes IP address.

- Reject IPv6 input in IPv4-only network settings and preserve the static profile when DHCP settings are saved or restored.
- Report failed settings writes, derive router transport from the discovery source, and refresh Admin action availability correctly.
- Clear conflicting overview filters, use amber acknowledgment badges, and respect reduced-motion preferences.

### Documentation

- Updated setup, device policy, LED, network, backup, health, and development instructions to match the current interface.
- Clarified the meaning of firmware flash and NVS usage readings and the hardware control of Ethernet indicators.

## Initial release

- Ethernet discovery through local probes, OpenWrt, or GL.iNet client inventory, with SSH session reuse for GL.iNet polling.
- Device authorization, timed acknowledgment, bulk selection, history clearing, and onboard RGB status signaling.
- Configurable Ethernet, Wi-Fi access point, Admin authentication, and internal configuration backup.
