# Changelog

## Unreleased

### Added

- Sortable and searchable DNS names in the Dashboard and Admin device tables.
- Persistent RGB colors and switches for Unauthorized, Acknowledged, Authorized / clear, and Scan activity, plus a master switch.

### Fixed

- Discard cached reverse DNS names when a device changes IP address.

## Initial release

- Ethernet device discovery through local probes, OpenWrt, or the GL.iNet client list. GL.iNet polls reuse an authenticated SSH session.
- Device authorization, timed acknowledgment, bulk policy and history controls, and onboard status signaling.
- Configurable Ethernet, Wi-Fi access point, Admin authentication, and internal configuration backup.
