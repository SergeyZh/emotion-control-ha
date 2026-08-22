[![en](https://img.shields.io/badge/lang-en-blue.svg)](README.md)
[![ru](https://img.shields.io/badge/lang-ru-green.svg)](README.ru.md)
[![de](https://img.shields.io/badge/lang-de-yellow.svg)](README.de.md)

# ESPHome ↔ L&S Emotion Control (CC2500)

Control **L&S Emotion Control** furniture lights from **Home Assistant** — no vendor gateway
needed. An ESP32 with a **CC2500** radio module (2.4 GHz) impersonates your remote: it sends the
same commands and **listens to the air**, so presses on the physical remote show up in Home
Assistant too.

The lamp appears in Home Assistant as an ordinary light: on/off, brightness, color temperature.

**Status: working**, verified against a real lamp. The protocol was reverse-engineered in a separate
repository, [`emotion-control-re`](https://github.com/SergeyZh/emotion-control-re).

Two protocol variants are supported:

- the original 14-byte broadcast protocol with a four-byte remote ID;
- an 8-byte addressed protocol, verified with an **L&S Emotion Driver ED 12/30-1** and its
  two-button remote (right button: on/off and held brightness; left button: color temperature).

## Features
- **On/off** and **color temperature** (warm ↔ cold).
- **Brightness** — exposed as a normal Home Assistant slider (native steps on the 14-byte variant,
  timed short ramps on the 8-byte variant).
- **Remote synchronization:** use the physical remote and the state in Home Assistant follows.
- **State restored** across ESP reboots.
- **Remote ID discovery** over the air (sniffer component) — yours and any others in range.
- **Pairing** directly with the lamp on the 14-byte variant, if you would rather not clone an
  existing remote. Pairing has not yet been verified for the 8-byte variant.

## What you need
| Part | Requirements |
|---|---|
| **ESP32** | Any board. Verified on a DevKit V1 (ESP-WROOM-32) |
| **CC2500 module** | 2.4 GHz only (**not** CC1101, which is sub-GHz), **26.000 MHz** crystal. Both the [small pad module](https://de.aliexpress.com/item/1005005501196142.html) and this [pin-header module from TinyTronics](https://www.tinytronics.nl/en/communication-and-signals/wireless/rf/modules/cc2500-wireless-rf-module-2.4ghz) are verified |
| **Soldering iron** | Only needed for a module without fitted header pins |
| **L&S remote** | Your existing remote; its ID is what the ESP32 borrows. Or a lamp in pairing mode |

The small AliExpress module has nine flat pads on a **1.27 mm** pitch and requires six wires to be
soldered directly. The verified TinyTronics alternative has header pins and can be connected to an
ESP32 header with female Dupont jumpers, so the radio side needs no soldering. Pin order differs
between module boards: follow the signal labels (`SI`, `SCLK`, `SO`, `CSn`, `GDO0`, `VCC`, `GND`),
not the pad numbers in the table below.

Power the module from **3.3 V**, not 5 V. Pinout, board drawing and schematic are in
[`datasheets/`](datasheets/CC2500_module_pinout.md).

### Wiring (CC2500 module → ESP32, SPI)
| Module | Signal | ESP32 (example, VSPI) |
|---|---|---|
| SI (2) | MOSI | GPIO23 |
| SCLK (3) | SCLK | GPIO18 |
| SO (4) | MISO | GPIO19 |
| CSn (8) | CS | GPIO5 |
| GDO0 (7) | end of transmission (opt.) | GPIO4 |
| VDD (1) / VSS (6) | 3V3 / GND | 3V3 / GND |

`GDO2` is unused. `GDO0` is optional — without it, end of transmission is detected by polling the
chip.

## Quick start
```bash
# 1. ESPHome (example: a venv inside the project directory)
python3 -m venv .venv && ./.venv/bin/pip install esphome

# 2. Find your remote's ID: flash the sniffer and press buttons on the remote
./.venv/bin/esphome run sniffer.yaml --device /dev/cu.usbserial-0001
#    → [I][cc2500_emotion_sniffer]: remote 68049F25  cmd=0x01 (on)  val=0x80  RSSI=-38 dBm  LQI=2 …

# 3. Wi-Fi into secrets.yaml, the discovered ID into remote_id in example.yaml
printf 'wifi_ssid: "YourNetwork"\nwifi_password: "password"\nemotion_remote_id: "68049F25"\n' > secrets.yaml

# 4. Flash the production config (over USB the first time, OTA afterwards)
./.venv/bin/esphome run example.yaml --device /dev/cu.usbserial-0001
```

Sign that the radio is alive:
```
CC2500 PARTNUM=0x80 VERSION=0x03
Radio configured and calibrated.
Listening for remote commands (RX enabled, MARCSTATE=0x0D).
```

Step by step, with explanations — **[GETTING-STARTED.md](GETTING-STARTED.md)**.

## Minimal configuration
```yaml
external_components:
  - source: github://SergeyZh/emotion-control-ha
    components: [cc2500_emotion]

spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

cc2500_emotion:
  id: emotion
  cs_pin: GPIO5
  gdo0_pin: GPIO4
  remote_id: "68049F25"   # 4-byte ID, or a 3-byte route such as "68ABF2"

light:
  - platform: cc2500_emotion
    name: "L&S Emotion"
    cc2500_emotion_id: emotion
```
Every option is documented in [CONFIGURATION.md](CONFIGURATION.md). Ready-made configs:
[`example.yaml`](example.yaml) (production, with Wi-Fi), [`bench.yaml`](bench.yaml) (bench over
USB), [`sniffer.yaml`](sniffer.yaml) (receive only).

## How it works
The CC2500 is set up from the register map captured off the remote (MSK, 250 kBaud, 2436.2 MHz, sync
word `0xD391`, FEC, hardware CRC) — modulation, coding and the frame CRC are done by the chip
itself. The software assembles either an 8-byte addressed payload or a 14-byte broadcast payload.

The chip stays in receive mode and returns to it after every transmission. Incoming frames are
validated (CRC, length, checksum): those carrying your `remote_id` update the state in Home
Assistant **without transmitting anything back**, while the rest are handed to the sniffer
component — which is how a remote's ID is discovered in the first place.

## Limitations
- **Feedback depends on the variant.** The 14-byte lamp does not answer, so its HA state is an
  estimate. The 8-byte ED 12/30-1 replies to addressed commands, but does not publish independent
  state changes; physical-remote traffic still keeps HA aligned.
- **Brightness is modeled as configurable levels** (five by default). The 14-byte variant uses
  native steps; the 8-byte variant approximates each level change with a timed ramp.
- **A held remote button** is estimated from elapsed time, hence approximate.
- **After an ESP reboot** the state is restored but not transmitted to the lamp (it remembers its
  own) — if the lamp was changed by remote in the meantime, Home Assistant catches up on the first
  action.

The full list — [CONFIGURATION.md](CONFIGURATION.md#features-and-limitations).

## Documentation
| Document | Contents |
|---|---|
| **[GETTING-STARTED.md](GETTING-STARTED.md)** | Step by step, from parts to a light in Home Assistant |
| **[CONFIGURATION.md](CONFIGURATION.md)** | Every YAML option, HA entities, protocol commands, limitations |
| **[SNIFFER.md](SNIFFER.md)** | Finding remotes, the `on_frame` trigger, a live air table in HA, reception diagnostics |
| [PLAN.md](PLAN.md) | Project plan and development phase status |
| [CLAUDE.md](CLAUDE.md) | Notes for AI agents: pitfalls, decisions, workflow (English only) |
| [emotion-control-re](https://github.com/SergeyZh/emotion-control-re) | Protocol reverse engineering: RF parameters, registers, frame format |

Documentation is maintained in **English (primary), Russian and German** — see the language badges
at the top of each file.

## Project layout
| Path | Contents |
|---|---|
| `components/cc2500_emotion/` | CC2500 driver + the `light` platform |
| `components/cc2500_emotion_sniffer/` | Logs every Emotion frame on the air, finds remote IDs |
| `example.yaml` | Production config: Wi-Fi/API/OTA, light, buttons, sniffer |
| `bench.yaml` | Bench config: USB logs, no Wi-Fi, the BOOT button toggles the lamp |
| `sniffer.yaml` | Receive only — for discovering a remote's ID |
| `datasheets/` | Pinout, board drawing and schematic of the CC2500 module + link to the verified module |

## License
[Apache License 2.0](LICENSE).

This project is not affiliated with or endorsed by L&S. "Emotion Control" is a trademark of its
owner and is used here only to state compatibility.
