# ESP32 LC29H(DA) GNSS/RTK Rover Prototype

Bare-minimum Arduino prototype for this pipeline:

1. ESP32 joins phone hotspot Wi-Fi.
2. ESP32 connects to NTRIP caster and receives RTCM.
3. ESP32 forwards RTCM to LC29H(DA) over UART.
4. ESP32 reads NMEA from LC29H(DA).
5. ESP32 serves NMEA over TCP for QField (single client).
6. ESP32 advertises `_nmea-0183._tcp` via mDNS (`esp32-rover.local` by default).

## Quick start

1. Copy `src/config.h.example` to `src/config.h` and edit it with your initial Wi-Fi, NTRIP, and pin settings.

```bash
cp src/config.h.example src/config.h
```

2. Build/flash with PlatformIO:

```bash
pio run -t upload
pio device monitor
```

3. In QField (iOS), configure external GNSS as TCP client to ESP32 IP and port `10110` (or your configured port).
   You can try hostname `esp32-rover.local` if your network/client resolves mDNS.

## Wiring (ESP32 <-> LC29H)

Default firmware UART pins are:

- `GNSS_RX_PIN = 16` (ESP32 RX)
- `GNSS_TX_PIN = 17` (ESP32 TX)

Wire serial cross-over:

- ESP32 `TX2` (GPIO17) -> LC29H `RX`
- ESP32 `RX2` (GPIO16) <- LC29H `TX`
- ESP32 `GND` <-> LC29H `GND`

Power wiring (as requested):

- ESP32 board powered by USB
- ESP32 `5V`/`VIN` pin -> LC29H `5V` input
- ESP32 `GND` pin -> LC29H `GND`

ASCII wiring sketch:

```text
USB Power
   |
   v
+-------------------------+
| ESP32-WROOM-32 Dev Board|
|                         |
| GPIO17 (TX2) ---------> |------ LC29H RX
| GPIO16 (RX2) <--------- |------ LC29H TX
| GND ------------------- |------ LC29H GND
| 5V / VIN -------------- |------ LC29H 5V
+-------------------------+
```

Notes:

- Keep ESP32 and LC29H on common ground.
- Keep UART wires short and clean for field reliability.
- If your exact LC29H carrier has different pin labels, map by signal function (`TX`, `RX`, `GND`, `5V`).

## Web UI

When connected in STA mode, the web UI is available on the rover IP:

- `http://<rover_sta_ip>/status` (formatted live status, auto-refresh every 2s)
- `http://<rover_sta_ip>/config` (edit Wi-Fi + NTRIP)
- `http://esp32-rover.local/status` and `http://esp32-rover.local/config` (if mDNS resolves)

If the rover cannot join Wi-Fi for `WIFI_AP_FALLBACK_MS`, it starts an AP + config page:

- SSID: `Rover-Setup-xxxxxx`
- Default AP password: `changeme123` (from `WIFI_AP_PASS` in `src/config.h`)
- URL: `http://192.168.4.1/`
- `/status` and `/config` are both available in AP mode
- Save Wi-Fi + NTRIP settings and the rover reboots
- Delete actions available for saved Wi-Fi or saved NTRIP (each reboots)
- Config page includes a non-reboot NTRIP connectivity test button

## Config persistence

- Runtime config is loaded from NVS if present; otherwise it falls back to `src/config.h`
- AP fallback does not erase saved Wi-Fi/NTRIP settings
- Saved Wi-Fi/NTRIP remain active across reboots until overwritten or deleted

## Notes

- `NTRIP_SEND_GGA` is enabled by default and can be disabled in `src/config.h`.
- The server allows one QField TCP client in this prototype.
- `src/config.h` is local/ignored; keep `src/config.h.example` in git as template.
- Wi-Fi and NTRIP credentials can be updated in field via AP/STA web portal and are saved in NVS.
- LC29H startup configuration commands are sent on every boot from `GNSS_STARTUP_COMMANDS` in `src/config.h`.
- Startup commands are generated with automatic NMEA checksum (`$...*CS`).

## Primary files

- `src/main.ino` orchestrates all tick functions.
- `src/ntrip_client.*` handles caster connection and RTCM stream.
- `src/gnss_uart.*` handles UART read/write to LC29H.
- `src/nmea_parser.*` frames and validates NMEA checksums.
- `src/nmea_server.*` serves NMEA to QField over TCP.
- `src/status.*` stores counters and last error cause.
- `src/ap_portal.*` serves `/status` and `/config` web pages.
- `src/config_store.*` loads/saves Wi-Fi+NTRIP config to NVS.
