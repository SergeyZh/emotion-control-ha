# CLAUDE.md — notes for agents

Project: an **ESPHome `light`** device for controlling **L&S Emotion Control** lights through a
**CC2500** module (2.4 GHz) attached to an ESP32 over SPI. Overall plan and phase status — in
[`PLAN.md`](PLAN.md).

This file is **English only** — it is not translated. Everything else in the docs is trilingual, see
"Multilingual documentation" below.

## Multilingual documentation
The project ships documentation in **English (primary), Russian and German**. Naming: `NAME.md` is
English, `NAME.ru.md` Russian, `NAME.de.md` German. Currently: `README`, `GETTING-STARTED`,
`CONFIGURATION`, `SNIFFER`, `PLAN`.

Rules when touching documentation:
- **English is the source of truth.** Write or update it first, then port the same change to `.ru.md`
  and `.de.md`.
- **All three languages must stay in sync.** A change that lands in only one language is an incomplete
  change — never leave the other two stale. This includes adding a new document: it needs all three
  variants from the start.
- **Every doc starts with the language badge header** linking its own three variants, e.g. for
  `README`:
  ```markdown
  [![en](https://img.shields.io/badge/lang-en-blue.svg)](README.md)
  [![ru](https://img.shields.io/badge/lang-ru-green.svg)](README.ru.md)
  [![de](https://img.shields.io/badge/lang-de-yellow.svg)](README.de.md)
  ```
- **Cross-links stay inside their language.** `README.de.md` links to `CONFIGURATION.de.md`, not to
  the English one. Anchor links must match the translated headings (`#features-and-limitations` in
  English vs `#eigenschaften-und-einschränkungen` in German), so verify anchors after translating a
  heading.
- Exceptions, deliberately not translated: this file (`CLAUDE.md`), `datasheets/` (English technical
  reference), YAML comments in configs (currently Russian) and the source code (English).
- Translate terminology consistently: remote / Fernbedienung / пульт, lamp / Leuchte / лампа,
  frame / Frame / кадр, brightness level / Helligkeitsstufe / ступень яркости, pairing / Kopplung /
  сопряжение, sniffer / Sniffer / сниффер. German uses "Sie" and German decimal commas (2,4 GHz).
- Log lines, code, YAML keys, register names and entity IDs are **never** translated.

## Protocol source
The original 14-byte radio protocol is decoded in the repository
**<https://github.com/SergeyZh/emotion-control-re>**. The key document is
<https://github.com/SergeyZh/emotion-control-re/blob/main/captures/PROTOCOL-FINDINGS.md> (CC2500
register map, packet format, commands). The compact 8-byte addressed variant was derived from
CRC-valid request/response captures of a two-button remote with an L&S Emotion Driver ED 12/30-1;
it uses the same RF settings but a different payload. Do not reinvent the broadcast protocol — take
it from the reverse-engineering repository.

## Hardware
- **ESP32 DevKit V1** (ESP-WROOM-32), `board: esp32dev`.
- **CC2500 module**, **26 MHz** crystal (verified) → the register map from the reverse-engineering repo
  applies as-is, with no RF recalculation. Module pinout — `datasheets/CC2500_module_pinout.md`.
- **Power the module from 3.3 V (pin `3V3`), NOT 5 V/VIN.**
- Two module boards are verified: the small
  [AliExpress pad module](https://de.aliexpress.com/item/1005005501196142.html), which needs soldering
  to nine 1.27 mm pads, and the
  [TinyTronics pin-header module](https://www.tinytronics.nl/en/communication-and-signals/wireless/rf/modules/cc2500-wireless-rf-module-2.4ghz),
  which connects with female Dupont jumpers. Their physical pin order differs; wire by signal name.

### Wiring (module → ESP32)
| Module | Signal | ESP32 |
|---|---|---|
| SI (2) | MOSI | GPIO23 |
| SCLK (3) | SCLK | GPIO18 |
| SO (4) | MISO | GPIO19 |
| CSn (8) | CS | GPIO5 |
| GDO0 (7) | TX done | GPIO4 |
| VDD (1) / VSS (6) | 3V3 / GND | 3V3 / GND |

## Component
`components/cc2500_emotion/` — external component (`AUTO_LOAD = ["light"]`):
- `__init__.py` — hub schema (SPI device, `cs_pin`, optional `gdo0_pin`, `remote_id`).
- `cc2500_emotion.h/.cpp` — the CC2500 driver (reset, register + PATABLE load + calibration, packet
  assembly, ×5 transmission). Public methods `send_on/off/brightness_up/down/color_temp/pair/raw`.
- `light.py` + `emotion_light.h` — the `light` platform (`EmotionControlLight : light::LightOutput`,
  type COLOR_TEMPERATURE). ESPHome auto-includes every `.h` of a component, which is why the light
  class lives in its own header, and `AUTO_LOAD` guarantees the light headers are present.
- Buttons (pairing/brightness/raw) are not a separate platform but `template button`s in YAML → they
  call the hub's public methods.

### Key protocol facts (to avoid stepping on rakes)
- Two formats are supported. Four-byte `remote_id` selects the broadcast format:
  `0E | ID₀ ID₁ ID₂ ID₃ | cmd | val | 00×6 | chk | 16`. Three-byte `remote_id` selects the compact
  addressed format and is stored in request order `68 REMOTE DRIVER`:
  `08 | 68 | REMOTE | DRIVER | cmd | 00 | val | chk | 16`. A driver response swaps the endpoint
  bytes and sets the direction byte to `01`; RX normalizes it back to request order.
- **Checksum:** XOR `0x7E` with every payload byte except the checksum itself. For broadcast this is
  `0x7E ⊕ ID₀ ⊕ ID₁ ⊕ ID₂ ⊕ ID₃ ⊕ 0x16 ⊕ cmd ⊕ val`. The collapsed variant
  `0xBE ⊕ cmd ⊕ val` holds ONLY for ID `68 04 F7 4D` — do not hardcode it. Compact checksum also
  includes the direction and endpoint bytes.
- Commands: `0x01` on (broadcast carries color temperature; compact request carries zero), `0x02`
  off, `0x11`/`0x21` broadcast brightness steps, `0x10`/`0x20` ramp start, `0x40` ramp stop,
  `0x50` color temperature (`0x00` warm … `0xFF` cold), `0x60` pairing (broadcast verified only).
- Brightness is modeled as **5 logical levels** by default. Commands (refined through reception,
  `val=0x00`, `chk` valid):
  - `0x11` one step brighter, `0x21` one step dimmer (broadcast only).
  - `0x10` ("brighter" held) = **start a smooth ramp UP**, `0x20` ("dimmer" held) = **ramp DOWN**; a
    full min↔max sweep ≈ **1 s**. `0x40` (released) = **stop** the ramp.
  - ⚠️ `0x10`/`0x20` are NOT an instant max/min; the level depends on when `0x40` arrives. Broadcast
    TX does not send them (it uses steps). Compact TX repeats them during a timed logical step and
    sends `0x40` at the end.
- **Brightness.** A standard `COLOR_TEMPERATURE` light with a slider. Home Assistant always attaches a
  brightness slider to any entity that has color temperature — "color temp without brightness" is
  impossible on a single light. The model (phase 6b): **absolute, `brightness_levels`=5**.
  `EmotionControlLight` tracks the level (`current_level_`) and steps the difference (`0x11`/`0x21`) on
  change, without flicker; at the end stops it sends +1 step of headroom (the lamp clamps).
  Broadcast TX does not use `0x10`/`0x20`; compact TX must use them because no step commands were
  observed, so each logical step is a timed ramp lasting `step_interval` followed by `0x40`. A ramp received from the remote is estimated from
  elapsed time (`start_ramp_`/`finish_ramp_`/`update_ramp`, `ramp_duration` ≈ 1 s; if the `0x40` is
  lost, it is finalized by timeout). The baseline is the first level ever seen. `send_raw` is for
  experiments.
  - **Pause between steps:** broadcast lamps collapse steps that fly back to back (~14 ms) into one;
    compact lamps need a hold duration. Both go through a **non-blocking queue** in the hub
    (`queue_brightness_steps` → `pending_...`), serviced once per `step_interval` (default **200 ms**)
    from `loop()`. A blocking `delay` is not allowed — it would hang ESPHome.
  - **The "Brighter"/"Dimmer" buttons (`step_brightness`) ALWAYS send a step**, even at an end stop
    (resyncs UI ↔ lamp).
- **State restoration across reboots:** the `restore_mode` default is overridden to
  `RESTORE_DEFAULT_OFF` (the base light defaults to `ALWAYS_OFF`, which lost the state). On boot the
  FIRST `write_state` (restore from flash) only adapts the trackers and shows up in Home Assistant, but
  is **NOT transmitted over the air** (the `initialized_` flag): the lamp remembers its own state,
  while blindly retransmitting could overwrite a change made by remote while the ESP was down.
- **RX → state (phase 6b):** the hub holds a pointer to the light (`set_light`) and, on a received
  command, calls `EmotionControlLight::on_remote_command(cmd,val)`, which updates Home Assistant via
  `make_call` with the `suppress_tx_` flag (inside `write_state` it kills transmission and only adapts
  the trackers). That way reception does not cause a transmission back. `EmotionControlLight` is
  forward-declared in the hub; the full type is in the `.cpp`.
  - ⚠️ **`make_call().perform()` invokes `write_state` LAZILY** (from `LightState::loop()` via
    `next_write_`, not synchronously). Hence `suppress_tx_` is **persistent**: set it to `true` before
    `perform()` and clear it INSIDE `write_state`. Clearing it right after `perform()` would leave the
    flag already `false` by write time → the command would go out over the air again (the lamp would
    get two commands).
- **Brightness: 5 levels → 1%/25%/50%/75%/100%** (the bottom one pinned to 1 %, Home Assistant's floor,
  since 0 % = off). Gamma is disabled (`gamma_correct: 1.0`), otherwise the level ↔ brightness
  round-trip breaks.
- **Compact RX uses the response only.** Requests have direction `00`; replies have direction `01`,
  swapped endpoints, and the driver's authoritative value. Ignoring the request prevents each
  physical press from appearing twice. The response is normalized to `68 REMOTE DRIVER`.
- **Reception dedup — a single ~150 ms sliding window keyed on (ID,cmd,val)** for ALL commands. The ID
  belongs in the key so that two remotes pressed simultaneously do not collapse into one event
  (important for the sniffer); echo suppression of our own TX stamps `rx_last_id_` with our
  `remote_id`. The remote sends every press as a short burst (~32 ms), and brightness commands are
  sometimes duplicated — the window collapses a burst/duplicate into one event, while separate presses
  (>150 ms) count separately. Rotating color temperature changes `val` → it does not collapse. Holding
  a brightness button sends `0x10`/`0x20` rather than a stream of `0x11`, so there are no fast
  legitimate repeats. (Removing dedup for brightness was a mistake made earlier — it produced double
  steps.)
- Transmission repeats (`send_command_(cmd,val,repeats)`): on/off/color temp/pairing — **×5**;
  broadcast brightness `0x11`/`0x21` — **×1**. A compact logical step repeatedly sends `0x10`/`0x20`
  every 40 ms for `step_interval`, then sends `0x40`. The chip does MSK/FEC/CRC/whitening-off,
  preamble and sync in hardware — software only prepares the payload.
- ESPHome SPI: `enable()`/`disable()` open/close a transaction and drive CS. You **must not** call
  `disable()` without a matching `enable()` — that causes an `spi_bus_lock` panic and a boot loop.

### Remote ID and the sniffer (phase 8)
- **`remote_id` has no default and must not have one.** The ID belongs to one specific remote; a
  shipped default would make other people's devices transmit under the author's ID. The option is
  optional: without it `has_remote_id_ == false`, `send_command_` refuses with `ESP_LOGE`, and the hub
  only listens.
- The test ID `68 04 9F 25` (paired with the test lamp) moved into `secrets.yaml`
  (`emotion_remote_id`), where `bench.yaml` picks it up. It never reaches the repository.
- **`components/cc2500_emotion_sniffer/`** — a separate component (`DEPENDENCIES = ["cc2500_emotion"]`)
  bound to the hub via `cv.use_id` + `set_parent`. It does not duplicate the radio: it subscribes to
  the hub through `register_frame_listener()`. It logs ID/cmd/val/RSSI/LQI of every valid frame;
  unknown commands are printed together with all 8 or 14 bytes (broadcast "reserved" zeros may carry data).
- ⚠️ The `EmotionFrameListener` interface is declared **in the hub's header**, not in the sniffer:
  ESPHome copies only the components actually used into the build, so the hub cannot include the
  sniffer's header — otherwise a config without the sniffer would not compile. Same reason
  `EmotionControlLight` is only forward-declared.
- `register_frame_listener()` forcibly sets `receive_ = true` — a listener without reception is
  pointless, and the `to_code` order (hub before sniffer) guarantees this is not overwritten.
- **`PKTLEN` = `0x0E`, NOT the `0xFF` from the captured map — a deliberate deviation.** For the remote
  that value is irrelevant (it only transmits), but it hurts us: at 2436.2 MHz (inside Wi-Fi) the chip
  periodically false-syncs on `0xD391` in noise, takes the next noise byte as the length (the logs show
  `length byte 0x3C/0x88/0x21`) and waits for up to 255 bytes that never come — i.e. it goes deaf until
  the FIFO overflows. Supported Emotion frames are 8 or 14 bytes, so the chip still accepts both and
  discards larger garbage lengths. **Do not "fix" this back to `0xFF`** when cross-checking against
  the reverse-engineering repo.
- **A held button = a dense stream of frames**, not sparse single transmissions. The original remote
  was measured at ~12 ms (`0x20` eight times in 90 ms); compact request/response traffic has its own
  cadence. Dedup collapses the stream into one event, while logging every copy can approach serial
  saturation and slow down `loop()`.
- ⚠️ **AFC was tried — do not.** The hypothesis "flaky reception = crystal mismatch" was DISPROVED by
  measurement. `FREQEST` (status register `0x32`, 1 LSB = XOSC/2¹⁴ = 1.587 kHz) jumps randomly on real
  frames and saturates at ±68 kHz — exactly the FOC limit (`FOCCFG=0x1D` → ±BW/8, BW = 542 kHz from
  `MDMCFG4=0x2D`), and in both directions. A crystal offset would be CONSTANT and one-sided. So the
  estimate is noise, and writing it into `FSCTRL0` would introduce a random detuning. `FREQEST` is kept
  only as log diagnostics; read it ONLY while the chip is still in RX, before `enter_rx_()`.
- **`poll_rx_` is a variable-length FIFO stream parser.** It drains all complete 8- and 14-byte frames
  and retains an incomplete tail until the missing bytes arrive (with a timeout). The old fixed
  17-byte read concatenated an 11-byte compact FIFO packet with the next packet; the older
  "read one then flush" path discarded repeat redundancy.
- **The list of remotes in Home Assistant is the sniffer's `text_sensor.py` platform**
  (`discovered_remotes`). ESPHome entities are static, created at compile time, so "an entity per
  discovered remote" is fundamentally impossible — only a single list string (limit 8 remotes; the log
  still sees all of them). `esp32_ble_tracker` works the same way. The **best** RSSI is published, not
  the latest: the value converges and publishing stops, otherwise a held button (~85 frames/s) would
  make the entity twitch constantly. The `text_sensor` headers are pulled in via `AUTO_LOAD` —
  otherwise a config without text_sensor would not compile.
- **`on_frame` (the sniffer's trigger) emits EVENTS, hence it is deduplicated.** The log sees every
  copy, the trigger fires once per press: a hold produces ~85 frames/s, and as many events onto the HA
  bus would take down its API. The `repeat` flag is computed by the hub and passed to the listener —
  dedup is NOT duplicated inside the sniffer. Arguments: `remote_id` (hex string), `command`,
  `command_name`, `value`, `rssi`, `lqi`.
- **The listener is called BEFORE dedup** (receiving the `repeat` flag), because dedup is a policy of
  the control path, not of diagnostics. The number of copies per press in the sniffer log is the main
  link-quality indicator: 4–5 lines is a healthy channel, 1 line means half the copies are lost.
- The order in `process_packet_` is: length → CRC → variant-specific header/type/checksum → compact
  response normalization → dedup → listener → filter by `remote_id` → action. The checksum is
  computed from the received bytes, so it validates any remote. The ID filter sits after the listener
  so the sniffer can discover other remotes.

## Workflow (dev)
- ESPHome in `./.venv/` (project venv). Verified with 2026.8.
- Bench config: **`bench.yaml`** — no Wi-Fi, logs over USB, the **BOOT (GPIO0)** button toggles the lamp
  on/off (a live transmission test). Needs `secrets.yaml` with `emotion_remote_id`.
- Production config: **`example.yaml`** — with Wi-Fi/API/OTA (needs `secrets.yaml`).
- Sniffer config: **`sniffer.yaml`** — receive only, no `remote_id` and no `light`; for finding a
  remote's ID and unknown commands.
- Flash + logs: `./.venv/bin/esphome run bench.yaml --device /dev/cu.usbserial-0001` (the port may
  differ — check `ls /dev/cu.usbserial*`; if `Connecting...` hangs, hold BOOT on the board).
- Compile only (code check): `./.venv/bin/esphome compile bench.yaml`.
- The ESP32 toolchain is cached in `~/Library/Caches/esphome` → rebuilds are fast.

### Signs of successful initialization in the logs
```
CC2500 PARTNUM=0x80 VERSION=0x03
Radio check: SYNC1=0xD3 MDMCFG2=0x73 CHANNR=0x10
Radio configured and calibrated.
```
