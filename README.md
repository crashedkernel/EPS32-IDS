# Watchtower - Intrusion Detection System (IDS)

A LAN device monitor for the **Waveshare ESP32-S3-ETH**. It uses the layout, themes and router discovery from ESP32 Network Monitor.

## Setup in Arduino IDE

1. Install **esp32 by Espressif Systems 3.3.12** and **LibSSH-ESP32 by Ewan Parker 5.10.0**. Open `IntrusionMonitor/IntrusionMonitor.ino` with its companion headers in the same folder.
2. Select **ESP32S3 Dev Module**, **16MB flash**, **OPI PSRAM**, **Huge APP (3MB No OTA/1MB SPIFFS)**, **Hardware CDC and JTAG**, **USB CDC On Boot Enabled**, and the board's USB serial port. Upload. [Waveshare Arduino settings and W5500 wiring](https://docs.waveshare.com/ESP32-S3-ETH/Development-Environment-Setup-Arduino)
3. Connect Ethernet to a LAN port that offers DHCP (the default). Find the address in Serial Monitor at **115200 baud** by sending `?`, or open **http://esp32ids.local/ (the default hostname)** from the same LAN.
4. For direct access, join **Watchtower** using **Password123** and open **http://192.168.4.1/admin**. The default Admin username is **admin** and password is **Password123**. Change the Admin password under **Admin security**. The AP stays up during wired operation and its password is independent of the Admin password.

## Discovery and coverage

- **Local ARP / ping** discovers responsive IPv4 hosts on the reachable LAN. The default range is the Ethernet subnet, up to 254 addresses. If ARP cannot identify the device, the dashboard shows its IP and leaves MAC-based controls unavailable. Sleeping and isolated devices may be missed.
- **GL.iNet live client list** uses the router's read-only local client inventory over SSH. Select this mode for a compatible GL.iNet 4.x router, set its LAN IP, SSH port (usually 22), username and password. It uses the router's connection state and inventory, including 5 GHz, MLO or wired clients when the router reports them. Legacy authenticated OpenWrt HTTP/ubus is also available.


GL.iNet scans reuse one authenticated SSH connection and open a fresh channel per scan. TCP keepalives help detect a lost connection. Watchtower reconnects on the next scan after a failure and checks the saved SSH host key again.

Both device tables have a sortable, searchable **DNS name** column. Watchtower queries the Ethernet DNS server for reverse DNS (PTR) records for online IPv4 devices. Router aliases and reported device names appear in **Device name**. When a device keeps the same MAC and IP, a previously resolved name can remain visible during a temporary DNS failure.

## Device policies

**Authorize** adds a MAC to the whitelist and marks it green. **Deauthorize** returns it to Unauthorized. In Admin, select individual devices or use **Select all shown** to select the current search results. Selection stays active across searches and sorting. **Select none** clears it. **Acknowledge** suppresses an online unauthorized device’s red alert for the duration configured in Admin. The default is 15 minutes. Acknowledgments clear on restart or when device history is cleared. **Reset** returns the device to the default Unauthorized status. Dashboard actions open the password-protected Admin Console with the selected device highlighted.

Policies survive reboots. **Clear All Device History** and **Clear Selected Devices from History** remove observed rows. Connected devices can return on the next scan and saved policies apply again. Up to 254 device rows are kept, prioritizing the current report. A MAC allowlist is a device label and alerting policy. It does not change access permissions on the router. Devices using rotating private MAC addresses may appear as new devices.

The onboard RGB LED flashes red for any online unauthorized device, yellow when all online unauthorized devices have been acknowledged, and green when neither condition is present. It stays off until Ethernet is up and a scan has completed. Use **Acknowledge all online** in Admin or tap the board’s **BOOT** button. Hold BOOT for at least two seconds and release it to reboot. The hardware RESET button reboots directly. The W5500 controls the separate Ethernet LINK indicator.

## LED indicators

Under **Admin → LED indicators**, choose colors and enable each state individually. **Enable RGB indicator** is the master switch. Click **Apply LED settings** to apply and save the changes without a restart. Unauthorized takes priority over Acknowledged, then Scan activity when enabled, then Authorized / clear. Defaults are red, yellow, and green for the three status states, with blue Scan activity disabled by default. The W5500 drives LINK and ACT directly. The internal configuration backup excludes LED preferences, which are saved separately.

## Network settings

Under **Ethernet**, set the board hostname, view the board’s fixed MAC address, and choose DHCP or static IPv4. Static mode requires an address, subnet mask, gateway and DNS server. The hostname is used for DHCP and the `hostname.local` dashboard address.

Under **Wi-Fi access point**, set the SSID and password for direct access. Choose 2.4 GHz channels 1–11 and 20 or 40 MHz width. **Save and restart AP** applies Wi-Fi changes without restarting the system. Start and Stop apply for the current session. The persistent toggle controls whether the AP starts after reboot. Use the Ethernet dashboard to stop or disable the AP.

Use the dashboard on a trusted LAN. The Admin Console uses password authentication over local HTTP. The public dashboard exposes the device inventory to people who can reach it.

## Configuration backup

In Admin, **Save configuration** stores the active settings in internal flash and replaces the previous backup. **Restore saved configuration** validates and reapplies the backup, then restarts Watchtower. Passwords and the whitelist are retained. Save and restart first if you have pending edits.

## Development

Edit files in `web/`, then run `python3 tools/build_web_assets.py` before compiling. The generated header is included in the repository. See the [development guide](docs/development.md) for the compile command, source layout, and release checks.
