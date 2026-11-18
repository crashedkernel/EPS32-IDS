# Verification record

Review date: 18 November 2026.

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
