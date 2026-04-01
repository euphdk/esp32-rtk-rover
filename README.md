# ESP32 LC29H(DA) GNSS/RTK Rover Prototype

Bare-minimum Arduino prototype for this pipeline:

1. ESP32 joins phone hotspot Wi-Fi.
2. ESP32 connects to NTRIP caster and receives RTCM.
3. ESP32 forwards RTCM to LC29H(DA) over UART.
4. ESP32 reads NMEA from LC29H(DA).
5. ESP32 serves NMEA over TCP for QField (single client).

## Quick start

1. Copy `src/config.h.example` to `src/config.h` and edit it with your Wi-Fi, NTRIP, and pin settings.
2. Build/flash with PlatformIO:

```bash
pio run -t upload
pio device monitor
```

3. In QField (iOS), configure external GNSS as TCP client to ESP32 IP and port `10110` (or your configured port).

## Notes

- `NTRIP_SEND_GGA` is enabled by default and can be disabled in `src/config.h`.
- The server allows one QField TCP client in this prototype.
- All config is hardcoded by design for phase-1 speed.
- LC29H startup configuration commands are sent on every boot from `GNSS_STARTUP_COMMANDS` in `src/config.h`.
- Startup commands are generated with automatic NMEA checksum (`$...*CS`).

## Primary files

- `src/main.ino` orchestrates all tick functions.
- `src/ntrip_client.*` handles caster connection and RTCM stream.
- `src/gnss_uart.*` handles UART read/write to LC29H.
- `src/nmea_parser.*` frames and validates NMEA checksums.
- `src/nmea_server.*` serves NMEA to QField over TCP.
- `src/status.*` stores counters and last error cause.
