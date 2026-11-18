# Watchtower - Intrusion Detection System (IDS)

Watchtower monitors devices on a LAN using a **Waveshare ESP32-S3-ETH**. Its Ethernet connection supplies device discovery and router access. A password-protected Admin Console manages device policies and system settings. The interface builds on ESP32 Network Monitor.

## Setup in Arduino IDE

1. Install **esp32 by Espressif Systems 3.3.12** and **LibSSH-ESP32 by Ewan Parker 5.10.0**. Open `IntrusionMonitor/IntrusionMonitor.ino` with its companion headers in the same folder.
2. Select the settings below and upload to the system's USB serial port. Leave **Erase All Flash Before Sketch Upload** disabled to retain saved settings.
3. Connect Ethernet to a LAN port that offers DHCP. Open Serial Monitor at **115200 baud** and send `?` to find the Ethernet address. The default hostname is `esp32ids`, so [esp32ids.local](http://esp32ids.local/) can also be used from a LAN client with mDNS support.
4. For direct wireless access, join **Watchtower** with **Password123** and open [Admin](http://192.168.4.1/admin). The default Admin username is **admin** and password is **Password123**. The AP starts by default. Its password and startup preference can be changed in Admin.

| Arduino setting | Value |
| --- | --- |
| Board | ESP32S3 Dev Module |
| Flash size | 16MB |
| PSRAM | OPI PSRAM |
| Partition scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| USB mode | Hardware CDC and JTAG |
| USB CDC On Boot | Enabled |

The firmware uses Waveshare's W5500 wiring. [Waveshare Arduino setup and pin assignments](https://docs.waveshare.com/ESP32-S3-ETH/Development-Environment-Setup-Arduino)

## Dashboard

The overview shows online, offline, authorized online, unauthorized online, and tracked-device totals from the latest completed scan. Unauthorized totals include acknowledged devices. The clock, last completed scan, next-scan countdown, and **System Uptime** show when the report was collected. The page refreshes every five seconds.

The **Device heatmap** has one tile per tracked device. Green means authorized online, red means unauthorized online, amber means acknowledged online, and gray stripes mean offline. Hover or focus a tile for details, then select it to find the device in the inventory.

Both the Dashboard and Admin device tables support search, sortable data columns, and **Online**, **Online authorized**, **Offline**, and **Offline authorized** filters. **DNS name** comes from reverse DNS (PTR) queries to the Ethernet DNS server. **Device name** uses the router's alias or reported name when available. Successful DNS names are cached for five minutes, and failed lookups are retried after one minute. A cached name is reused while its device retains the same MAC and IP.

Dark and light themes, text sizing, themed scrollbars, fixed headers, and **Back to top** controls are available on both pages.

## Discovery and coverage

Choose the source under **Admin → Scan preferences**. The interval accepts **10–86,400 seconds**. A scan can take longer than the configured interval, so the next scan waits for the current work to finish. **Scan now** requests an additional scan.

- **Local ARP / ping** discovers responsive IPv4 hosts on the reachable LAN. Automatic selection uses the Ethernet subnet, with a maximum of 254 addresses. CIDR or explicit ranges can also be configured. Sleeping and isolated devices may be missed. MAC-based policy controls require a known MAC address.
- **GL.iNet live client list** reads a compatible GL.iNet 4.x router's local inventory over SSH. Set the router's LAN IPv4 address, SSH port, username, and password under **Router connection**. It uses the router's connection state and can include 5 GHz, MLO, and wired clients when those appear in the router's inventory.
- **OpenWrt HTTP (legacy)** retrieves records through authenticated HTTP/ubus on a compatible router.

GL.iNet scans reuse one authenticated SSH connection and open a fresh channel per scan. TCP keepalives help detect a lost connection. Watchtower reconnects on a later scan after a transport failure and checks the saved SSH host key again. If the identity changes, verify the router before using **Forget this identity** in Admin.

Up to **254 device rows** are stored, with the current report taking priority. Failed discovery retains the previous successful report and displays an error.

## Device policies

**Authorize** saves a MAC in the whitelist and marks the device authorized. **Deauthorize** returns it to Unauthorized. In Admin, check individual devices or use **Select all shown** to select the current search results. Selection persists across searches and sorting. **Select none** clears the selection. Bulk controls authorize, deauthorize, or clear the selected devices from history.

**Acknowledge** suppresses an online unauthorized device's alert for the duration set under **Acknowledgment**. The default is **15 minutes**, with an allowed range of **1–10,080 minutes**. **Acknowledge all online** applies that timer to every online unauthorized device. Acknowledgments expire when the timer ends and clear when the system restarts or the relevant history is cleared.

Saved whitelist entries survive restarts. **Clear All Device History** and **Clear Selected Devices from History** remove observed rows and their temporary state. Rediscovered devices can return on a later scan, with their saved policies applied again. A MAC allowlist controls labels and signaling in Watchtower. Router access permissions remain under the router's control. Rotating private MAC addresses can cause a device to appear as a new entry.

## LED indicators and buttons

Under **Admin → LED indicators**, choose colors and enable each state. Click **Apply LED settings** to apply and save changes immediately. **Stealth Mode** toggles the RGB master switch and saves the choice. The W5500 directly drives the separate Ethernet LINK and ACT lights.

| State | Default color | Priority |
| --- | --- | --- |
| Unauthorized online | Red | Highest |
| Acknowledged online | Yellow | After Unauthorized |
| Scan activity | Blue, disabled by default | After acknowledged devices |
| Authorized / clear | Green | When no higher-priority condition applies |

The RGB indicator stays off while Ethernet is unavailable. Normal status signaling begins after a completed scan. Tap **BOOT** to acknowledge all online unauthorized devices. Hold **BOOT** for at least two seconds, then release it to restart. The hardware **RESET** button resets the chip directly. Admin also has a **Reboot system** button.

## Network, access point, and time settings

Under **Ethernet**, set **Hostname**, view the fixed Ethernet MAC, and choose DHCP or static IPv4. DHCP is the default. Static mode requires an address, subnet mask, gateway, and DNS server. The hostname is used for DHCP and the `hostname.local` dashboard address.

Under **Wi-Fi access point**, set the SSID, password, primary channel, and channel width. The ESP32-S3 supports 2.4 GHz. Channels **1–11** and **20 or 40 MHz** widths are available. **Save and restart AP** applies Wi-Fi changes without restarting the system. **Start AP** and **Stop AP** apply to the current session. **Enable AP persistently** and **Disable AP persistently** also control startup after reboot. Open Admin through Ethernet to stop or persistently disable the AP.

Under **Date & time**, select a time zone, 12- or 24-hour format, and NTP server. The defaults are **America/New_York**, **24-hour**, and **time.nist.gov**. The browser formats synchronized UTC timestamps in the selected zone.

## Health Status

Admin displays CPU load and clock speed, memory usage and availability, firmware flash usage, and configuration-storage usage in responsive cards. CPU load is sampled from task runtime counters. Memory refers to the heap reported by the Arduino core. Firmware flash usage compares sketch size with physical flash capacity. The area outside the sketch includes reserved partitions.

Configuration storage uses NVS entry counts. Its percentage and estimated available capacity describe allocated entries within the NVS partition.

## Configuration backup and Admin security

**Save configuration** stores the active network, AP, discovery, router connection, time, and acknowledgment settings in internal flash and replaces the previous backup. **Restore saved configuration** validates and applies that backup, then restarts Watchtower. Save and restart first if you have pending edits in the main settings form.

Passwords, the whitelist, and LED preferences are persisted separately from the backup. Restore retains their current values. Update the Admin password under **Admin security** using **12–128 characters**. Blank password fields preserve their saved values.

Use the dashboard on a trusted LAN. The Admin Console uses password authentication over local HTTP. The public dashboard exposes the inventory to clients that can reach the system. Credentials and settings are stored in the ESP32's internal NVS storage.

## Development

Edit files in `web/`, then run `python3 tools/build_web_assets.py` before compiling. The generated header is committed with its source files. See the [development guide](docs/development.md) for the compile command, source layout, and release checks, and the [changelog](CHANGELOG.md) for changes.

See the [verification record](docs/verification.md) for completed checks and remaining hardware acceptance work.
