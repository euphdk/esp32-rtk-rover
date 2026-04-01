# AGENT.md

This file is for any coding agent ("vibecoder") working in this repository.

## Project intent

This is a practical field GNSS/RTK rover prototype for ESP32 + Quectel LC29H(DA), used with QField on iPhone.

Primary runtime pipeline:

1. ESP32 joins Wi-Fi (STA)
2. ESP32 pulls RTCM from NTRIP caster
3. ESP32 forwards RTCM to LC29H over UART
4. ESP32 reads NMEA from LC29H
5. ESP32 serves NMEA over TCP for QField

Secondary capabilities:

- AP fallback + web config portal
- mDNS advertisement
- field-oriented health logging

## Tech stack

- Framework: Arduino (ESP32)
- Build: PlatformIO
- Main board target: `esp32dev`

Common command:

```bash
pio run
```

## Repo rules (must follow)

1. Keep architecture simple and debuggable; avoid clever abstractions.
2. Prefer non-blocking logic in `loop()`/tick style.
3. Do not break the three-path separation:
   - RTCM ingestion path
   - GNSS command/config path
   - NMEA egress path
4. Keep field observability first-class (logs + counters + last error).
5. Do not commit real credentials or private network details.
6. Keep `src/config.h` local only; use `src/config.h.example` as template.

## Git workflow rules (must follow)

1. Commit after each completed change set.
2. Use focused commit messages with clear intent.
3. Never run destructive git operations unless explicitly requested.
4. If docs or behavior changed, update `README.md` in the same change set.

## Config and secrets

- Runtime config is loaded from NVS first, then defaults from `src/config.h`.
- Web portal can change Wi-Fi and NTRIP config and persist it.
- Passwords must be masked in UI/status outputs.
- Never print plaintext passwords in logs.

## Web portal expectations

Current routes:

- `/status` - formatted live status page (auto-refresh)
- `/config` - config editor (Wi-Fi + NTRIP)
- `/api/status` - machine-readable status
- `/api/ntrip_test` - test NTRIP without reboot

Behavior requirements:

- Available in STA mode
- Available in AP fallback mode
- Captive portal probe routes should redirect cleanly

## NTRIP behavior expectations

- Handle both HTTP-style and `ICY 200 OK` responses.
- Handle reconnection/backoff safely.
- If NTRIP host/mount is empty, do not thrash reconnect logs.

## GNSS/NMEA behavior expectations

- Track bad checksum and too-long line counters.
- Keep status logs useful for field debugging.
- Avoid parser behavior that starves networking/ticks.

## iPhone hotspot realities

- Connectivity can be unstable.
- AP/STA transitions and reconnect logic must be robust.
- Always preserve partial operation modes (GNSS alive even if NTRIP/QField disconnected).

## Definition of done for code changes

Before finishing a change:

1. `pio run` passes.
2. Logs/status remain coherent.
3. No secret leakage in code/docs.
4. Commit is created.
