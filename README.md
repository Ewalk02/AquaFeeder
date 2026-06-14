# AquaFeeder

ESP32-S3-Zero firmware for the AquaFeeder stepper motor companion device. Drives a TMC2209 over STEP/DIR/EN, stores a feeding schedule locally, and exposes an HTTP API for [AquaPilot](../AquaPilot/) to configure and trigger feeds.

## Hardware

| Signal | GPIO | Notes |
|--------|------|-------|
| STEP   | 4    | TMC2209 STEP |
| DIR    | 5    | Direction |
| EN     | 6    | Enable (LOW = driver enabled on most TMC2209 boards) |

Motor defaults: **10 RPM shaft**, **1/8 microstepping** (1600 pulses/rev) — configurable in `idf.py menuconfig` → AquaFeeder → Stepper motor.

When a feed finishes, AquaFeeder POSTs to AquaPilot at `http://<aquapilot-ip>/api/feeder/complete` (AquaPilot IP is sent with each schedule sync).

### Motor clicks but does not turn

1. **Re-flash** after updates — STEP pulse is **1 ms** and speed accounts for microsteps.
2. In **menuconfig**, try **Stepper motor → Enable pin active LOW** toggled off if your EN logic is inverted.
3. Set **Microsteps** to match MS1/MS2 (floating on TMC2209 = **8** for 1/8).
4. Confirm TMC2209 **MS1/MS2** jumpers are set for standalone STEP/DIR mode (not UART-only).
5. Turn the **current trimmer** on the TMC2209 board clockwise — low current causes stall clicking.
6. Run a wiring test jog:
   ```bash
   curl -X POST http://<feeder-ip>/api/test/step \
     -H "Content-Type: application/json" \
     -d '{"steps":100}'
   ```

## Build

Requires [ESP-IDF 5.5.x](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/index.html).

If your IDF install only has the ESP32-P4 toolchain, add the ESP32-S3 target:

```bash
python ~/.espressif/v5.5.4/esp-idf/tools/idf_tools.py install xtensa-esp-elf
```

Then add `esp32s3` to the `targets` list in `~/.espressif/idf-env.json` for your IDF 5.5 install, or prepend the xtensa toolchain to `PATH` before building.

```bash
source ~/.espressif/v5.5.4/esp-idf/export.sh
cd AquaFeeder
idf.py set-target esp32s3
idf.py menuconfig   # set WiFi SSID/password under "AquaFeeder"
idf.py build flash monitor
```

## HTTP API

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/status` | Device state, clock, schedule, **live feed progress** (`feed_elapsed_ms`, `feed_steps`, …) |
| POST | `/api/feed` | `{"seconds": 3}` — manual feed |
| POST | `/api/schedule` | Push schedule from AquaPilot |
| POST | `/api/skip` | Skip next scheduled slot |

## Bring-up tests (curl)

Replace `<feeder-ip>` with the IP shown in serial log after WiFi connect.

```bash
# Status (503 until SNTP syncs)
curl http://<feeder-ip>/api/status

# Manual feed — motor runs ~2 s at 2 RPM
curl -X POST http://<feeder-ip>/api/feed \
  -H "Content-Type: application/json" \
  -d '{"seconds":2}'

# Apply schedule
curl -X POST http://<feeder-ip>/api/schedule \
  -H "Content-Type: application/json" \
  -d '{"enabled":true,"start_h":8,"start_m":0,"end_h":20,"end_m":0,"times_per_day":2,"amount_seconds":3,"timezone":"America/Chicago"}'

# Verify next feed time updated
curl http://<feeder-ip>/api/status

# Watch feed progress (poll while motor runs)
watch -n1 curl -s http://<feeder-ip>/api/status

# Skip next slot
curl -X POST http://<feeder-ip>/api/skip \
  -H "Content-Type: application/json" \
  -d '{}'
```

## AquaPilot integration

1. Flash AquaFeeder and note its LAN IP from serial output.
2. On AquaPilot: **Settings → Automatic Feeder**
3. Set **Feeder ESP32 host** to the S3 IP (or hostname).
4. Configure schedule and enable the feeder toggle.
5. AquaPilot pushes the schedule on save, on WiFi connect, and every 60 s.
6. Scheduled feeds run on the S3 even if AquaPilot is briefly offline.
7. **Feed Now** and **Skip Next Feeding** send HTTP commands to the feeder.

Confirm the feeder connection indicator on the AquaPilot dashboard turns on when `/api/status` succeeds.

## Project layout

```
main/
  app_main.c           — boot: NVS, WiFi, SNTP, HTTP, scheduler
  wifi_setup.c         — STA connect (Kconfig credentials)
  aquafeeder_time.c    — SNTP + timezone
  stepper_motor.c      — GPIO stepper driver
  feed_schedule_store.c — NVS schedule persistence
  feed_scheduler.c     — local slot algorithm + feed triggers
  feed_http.c          — HTTP route handlers
```
