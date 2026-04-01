# ESP32 LC29H(DA) GNSS/RTK Rover Prototype

Bare-minimum Arduino prototype for this pipeline:

1. ESP32 joins phone hotspot Wi-Fi.
2. ESP32 connects to NTRIP caster and receives RTCM.
3. ESP32 forwards RTCM to LC29H(DA) over UART.
4. ESP32 reads NMEA from LC29H(DA).
5. ESP32 serves NMEA over TCP for QField (single client).
6. ESP32 advertises `_nmea-0183._tcp` via mDNS (`esp32-rover.local` by default).

## Quick start

1. Copy `src/config.h.example` to `src/config.h` and edit it with your Wi-Fi, NTRIP, and pin settings.
2. Build/flash with PlatformIO:

```bash
pio run -t upload
pio device monitor
```

3. In QField (iOS), configure external GNSS as TCP client to ESP32 IP and port `10110` (or your configured port).
   You can try hostname `esp32-rover.local` if your network/client resolves mDNS.

When connected in STA mode, the config/status page is also available on the rover IP:

- `http://<rover_sta_ip>/`
- `http://esp32-rover.local/` (if mDNS resolves)
- `http://<rover_sta_ip>/status` shows formatted live status (auto-refresh every 2s).
- `http://<rover_sta_ip>/config` shows editable Wi-Fi + NTRIP config.

If the rover cannot join Wi-Fi for `WIFI_AP_FALLBACK_MS`, it starts an AP + config page:

- SSID: `Rover-Setup-xxxxxx`
- URL: `http://192.168.4.1/`
- Save Wi-Fi + NTRIP settings and the rover reboots.
- Status page shows masked Wi-Fi/NTRIP passwords and formatted live telemetry.
- Includes delete actions for saved Wi-Fi or saved NTRIP (each reboots).
- Config page includes a non-reboot NTRIP connectivity test button.

## Notes

- `NTRIP_SEND_GGA` is enabled by default and can be disabled in `src/config.h`.
- The server allows one QField TCP client in this prototype.
- All config is hardcoded by design for phase-1 speed.
- Wi-Fi credentials can be updated in field via AP web portal and are saved in NVS.
- LC29H startup configuration commands are sent on every boot from `GNSS_STARTUP_COMMANDS` in `src/config.h`.
- Startup commands are generated with automatic NMEA checksum (`$...*CS`).

## Primary files

- `src/main.ino` orchestrates all tick functions.
- `src/ntrip_client.*` handles caster connection and RTCM stream.
- `src/gnss_uart.*` handles UART read/write to LC29H.
- `src/nmea_parser.*` frames and validates NMEA checksums.
- `src/nmea_server.*` serves NMEA to QField over TCP.
- `src/status.*` stores counters and last error cause.
