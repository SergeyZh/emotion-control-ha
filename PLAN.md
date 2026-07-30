[![en](https://img.shields.io/badge/lang-en-blue.svg)](PLAN.md)
[![ru](https://img.shields.io/badge/lang-ru-green.svg)](PLAN.ru.md)
[![de](https://img.shields.io/badge/lang-de-yellow.svg)](PLAN.de.md)

# Plan: controlling L&S Emotion Control lights from ESPHome

An ESPHome `light` device that drives L&S lamps over the **Emotion Control** radio protocol through a
**CC2500** module (2.4 GHz) attached to an ESP32 over SPI. The protocol comes from the reverse
engineering in [`emotion-control-re`](https://github.com/SergeyZh/emotion-control-re) (fully decoded;
RF parameters and packet format confirmed by transmitting to a real lamp).

## Deliverable
An ESPHome light device with:
- on/off,
- color temperature control,
- brightness control (stepped — see "Control model").

## Hardware
- **CC2500 module** (2.4 GHz; **not** CC1101, which is sub-GHz), **26.000 MHz** crystal (confirmed —
  the register map from the reverse-engineering repo applies as-is, with no RF recalculation).
  Module pinout — [`datasheets/CC2500_module_pinout.md`](datasheets/CC2500_module_pinout.md).
- **ESP32**, SPI bus.

### Wiring (ESP32 ↔ CC2500 module)
| Module | Signal | ESP32 (example, VSPI) |
|---|---|---|
| SI (2) | MOSI | GPIO23 |
| SCLK (3) | SCLK | GPIO18 |
| SO (4) | MISO | GPIO19 |
| CSn (8) | CS | GPIO5 |
| GDO0 (7) | TX done (opt.) | GPIO4 |
| VDD (1) | 3.3 V | 3V3 |
| VSS (6) | GND | GND |

GDO2 is unused. GDO0 is optional: end of transmission can be caught through it, by polling
`MARCSTATE`, or with a fixed delay.

## Control model
- **A standard `COLOR_TEMPERATURE` light:** on/off + brightness + color temperature, no hacks — just
  like an ordinary lamp in Home Assistant.
- On/off (`0x01`/`0x02`) and color temperature (`0x50`) are absolute commands and map 1:1.
- **Brightness is an absolute slider over 5 levels.** We track the current level and step the
  difference (`0x11`/`0x21`) when it changes, without flicker; the end stops are reached with steps
  plus headroom, which doubles as a resynchronization point. The number of levels is
  `brightness_levels` (default 5).
- **Synchronization from the remote (RX):** commands from the physical remote received over the air
  update the state in Home Assistant (on/off, color temperature, brightness level) without
  transmitting back, so HA and the lamp do not drift apart.
- Additionally: **"Pair"** (`0x60`) and **Raw** (arbitrary command, editable from HA) as
  `template button`s in YAML.

## Protocol (in brief; in full in the reverse-engineering repo)
- RF: MSK, 250 kBaud, 2436.2 MHz carrier, sync word `0xD391`, FEC on, whitening off, hardware CRC.
  All of it is done by the CC2500 in hardware from the loaded register map.
- The map of 45 registers plus `PATABLE = FF FE BB A9 7F 6E 97 C5` comes from
  [`captures/PROTOCOL-FINDINGS.md`](https://github.com/SergeyZh/emotion-control-re/blob/main/captures/PROTOCOL-FINDINGS.md).
- Packet (in the TX FIFO): `0E | ID₀ ID₁ ID₂ ID₃ | cmd | val | 00×6 | chk | 16`, where `0E` is the
  length (14). `chk = 0x7E ⊕ ID₀ ⊕ ID₁ ⊕ ID₂ ⊕ ID₃ ⊕ 0x16 ⊕ cmd ⊕ val` (the general formula; the
  collapsed `0xBE ⊕ cmd ⊕ val` from the reverse-engineering repo holds only for ID `68 04 F7 4D`).
  The remote ID is set in the config; there is no default — users find their own with the sniffer
  (phase 8).
- Commands: `0x01` on (carries the current color temperature), `0x02` off, `0x11` brighter (step),
  `0x21` dimmer (step), `0x50` color temperature (`0x00` warm … `0xFF` cold), `0x60` pairing.
- Brightness and its commands (refined through reception, phase 6):
  - **Stepped — 5 levels:** `0x11` one step brighter, `0x21` one step dimmer.
  - **Smooth (ramp):** `0x10` ("brighter" held) — **start ramping UP**, `0x20` ("dimmer" held) —
    **start ramping DOWN**. A full min-to-max sweep takes about **1 second**.
  - `0x40` (button released) — **stop** the ramp at the current level.
  - ⇒ `0x10`/`0x20` are **not** an instant max/min: the actual level depends on when the `0x40`
    arrives. Absolute end stops are more reliably reached with STEPS (`0x11`/`0x21` plus headroom —
    the lamp clamps).
  - Brightness commands are singles (not ×5) and can be lost.
- Transmission: `SIDLE(0x36) → SFTX(0x3B) → burst write FIFO(0x7F) → STX(0x35)`, **×5 repeats**, then
  `SPWD(0x39)`. The remote (and we) only transmit; it has no RX path.

## Phases
1. **SPI check.** ✅ Confirmed on hardware (`PARTNUM=0x80 VERSION=0x03`).
2. **Radio initialization.** ✅ Confirmed on hardware (`SYNC1=0xD3 MDMCFG2=0x73 CHANNR=0x10`,
   "Radio configured and calibrated").
3. **Transmission.** ✅ Confirmed on the lamp. Payload assembly, `chk` by the general formula, TX cycle
   ×5 (`SIDLE→SFTX→FIFO→STX`, end detected via GDO0/MARCSTATE), chip left in IDLE. The `send_*`
   methods.
4. **The `light` platform + buttons.** ✅ Confirmed in Home Assistant. `EmotionControlLight` (type
   COLOR_TEMPERATURE): `write_state()` → off `0x02`, on `0x01`+CT, CT `0x50` (mireds ↔ `0xFF` warm,
   inverted). Pairing and raw buttons as `template button`; raw is editable from HA. The brightness
   model was reworked in phase 6b (absolute, 5 levels).
5. **Pairing and a live test.** ✅ Every command (on/off, color temperature, brightness, pairing, raw)
   verified against a real lamp.
6. **Receiving remote commands (RX) and state synchronization.**
   - **6a — reception.** ✅ Confirmed on hardware. The chip stays in RX (RXOFF_MODE=RX); `loop()` polls
     `RXBYTES` (read twice to work around the counter erratum), reads a 17-byte frame, then flushes and
     restarts RX (protection against FIFO overflow during a ×5 burst). Validation of
     CRC/length/`remote_id`/`chk`, deduplication, suppression of our own TX echo. This is also where
     the brightness commands were found (5 levels, `0x10`/`0x20` for the ends, `0x40` to release).
   - **6b — synchronization + absolute brightness.** ✅ Confirmed in HA and on the lamp. A received
     remote command → `EmotionControlLight::on_remote_command()` updates the state through `make_call`
     with transmission suppressed. Absolute brightness (5 levels). Refinements along the way:
     `0x10`/`0x20` are a smooth ramp (not an instant end stop; estimated from `ramp_duration`),
     brightness steps are ×1, a `step_interval` ≈ 200 ms pause between steps, buttons always transmit,
     deduplication is a single ~150 ms window.
7. **README + documentation.** ✅ Done. [`README.md`](README.md) (overview, hardware, quick start),
   [`GETTING-STARTED.md`](GETTING-STARTED.md) (step by step for new users),
   [`CONFIGURATION.md`](CONFIGURATION.md) (options, HA entities, commands, limitations),
   [`SNIFFER.md`](SNIFFER.md) (remotes, `on_frame`, reception diagnostics),
   [`CLAUDE.md`](CLAUDE.md) (notes for agents), and this plan. State restoration across reboots —
   `restore_mode=RESTORE_DEFAULT_OFF`, not retransmitted on boot.
8. **Sniffer: finding a remote's ID.** ✅ A separate `cc2500_emotion_sniffer` component bound to the hub
   as its parent (the radio is not duplicated). The hub validates a frame by CRC plus the `chk`
   computed from the received ID bytes and hands it to the `EmotionFrameListener` **before** the
   `remote_id` filter; actions still run only for our own ID. Deduplication moved above the filter and
   is keyed on ID+cmd+val. `remote_id` became optional with no default — without it the hub only
   listens. Ready-made config — [`sniffer.yaml`](sniffer.yaml). Discovered remotes are visible in Home
   Assistant too, through the sniffer's `text_sensor` platform (`discovered_remotes`, a list with each
   remote's best RSSI).
9. **The `on_frame` trigger.** ✅ An automation on a frame received from any remote, one firing per
   press (the `repeat` flag is computed by the hub; a held button means ~85 frames/s, which as events
   would flood the HA API). A live air table in HA is built from it — recipe in
   [`SNIFFER.md`](SNIFFER.md). A dedicated panel like `/config/bluetooth/advertisement-monitor` cannot
   be done from ESPHome: that one is registered by the built-in `bluetooth` integration, where ESPHome
   is only the transport (`bluetooth_proxy`).

## Status: functionally complete
Every advertised feature works on a real lamp and in Home Assistant. What is left is optional:
calibrating `ramp_duration`/`step_interval` for a specific lamp, cleanup and refactoring, publishing.

## Risks (closed)
- ✅ **FEC with variable length on RECEPTION by the CC2500 in hardware** — confirmed on hardware
  (phase 6a).
- ✅ **TX/RX half duplex** — we return to RX after transmitting, and our own echo is suppressed by
  deduplication.
- ✅ **Brightness level drift** — a baseline is taken from the first state; the end stops and reception
  from the remote resynchronize it; steps are spaced by `step_interval` so the lamp distinguishes them.
