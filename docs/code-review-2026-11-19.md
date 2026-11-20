# Watchtower review — 19 November 2026

## Findings and changes

- **Repeated login dialogs:** Arduino WebServer keeps one Digest challenge state. Concurrent unauthenticated polling could replace the nonce used by another request. Admin now establishes a bounded browser session after interactive sign-in; background endpoints never send login challenges. Logout revokes only the current browser. Sessions have eight fixed slots and an eight-hour idle timeout.
- **Device Map controls:** visible names are consistent, the Dashboard map collapses/expands, Show All Devices resets every inventory filter, and online blink phases can be staggered independently. Map v1 appearance preferences migrate safely to v2 without reading former struct padding as the new option.
- **Defensive checks:** invalid/unusable map IPv4 inputs are ignored, truncated IPv4 ping responses are rejected before inspecting their headers, and an enum/byte conditional warning was resolved.
- **Diagnostics:** minimum-free heap and largest free block are visible in Health Status and the Serial `?` output.

## Ownership and bounds reviewed

- Device arrays prefer PSRAM and are bounded to 254 records. Expansion retains the prior allocation on failure; replaced scan buffers are released. The public API snapshot is a deliberate lifetime allocation reused across requests.
- SSH is owned by the scan task. Per-scan channels, callbacks, and events are cleaned up; the authenticated transport is deliberately reused. Failure closes stale transport state.
- Router HTTP responses are capped, HTTP clients are ended, cJSON objects are released, and streamed GL.iNet rows have fixed length and total-byte limits.
- Ping sockets and DNS UDP resources are closed. ARP request synchronization is released on success and setup failure.
- Settings that require reboot are persisted without mutating the scan task’s active String configuration. Device policy changes and report swaps use the report mutex; network response writes happen after releasing it.
- Browser port nodes are replaced, without retaining old nodes in application collections. Random blinking uses CSS phases. Collapsed maps avoid rebuilding their ports. Repeated timers are page-level, not per device.

No unmatched project-owned allocation was identified in these paths. This is a source review, not a formal proof that all firmware or dependency defects are absent.

## Verification

- Session tests exercise 1000 polls without further challenges, multiple browsers, isolated logout, expiration, rejection, CSRF, and bounded storage.
- Snapshot tests exercise 1000 growth cycles to 254 devices, PSRAM fallback, and failed expansion. All 5001 successful allocations are released.
- Map tests cover colors, speeds, randomization, v1 migration, bad inputs, failed persistence, and authentication. RGB tests cover Stealth state and failures.
- Host tests also pass UndefinedBehaviorSanitizer checks. AddressSanitizer could not start in this macOS environment (runtime initialization failure); no AddressSanitizer result is claimed.
- Browser fixtures validate layouts and interactions with simulated data and authenticated responses. A real Safari/Chrome sign-in with the user’s configured password has not been exercised.

Live build and sampling results are recorded in [verification.md](verification.md). Longer high-rate monitoring, reconnect cycles, and authenticated interaction remain important for assessing intermittent hangs and fragmentation.

## Live sample

Live sampling covered 12 samples over 110.7 seconds, spanning scans 7–18 with 115 devices and no scan errors. Free heap stayed between 161,068 and 161,144 bytes; minimum-free heap stayed at 148,848 bytes and the largest free block stayed at 112,628 bytes. No progressive decline was observed in this short run. It does not rule out long-duration leaks or fragmentation.
