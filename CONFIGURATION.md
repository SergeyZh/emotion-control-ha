[![en](https://img.shields.io/badge/lang-en-blue.svg)](CONFIGURATION.md)
[![ru](https://img.shields.io/badge/lang-ru-green.svg)](CONFIGURATION.ru.md)
[![de](https://img.shields.io/badge/lang-de-yellow.svg)](CONFIGURATION.de.md)

# Configuration reference

Every YAML option, the Home Assistant entities, the protocol commands and the implementation
quirks. If this is your first time here, start with [GETTING-STARTED.md](GETTING-STARTED.md).

## Adding the component

The component is pulled in as an external component, straight from GitHub:

```yaml
external_components:
  - source: github://SergeyZh/emotion-control-ha
    components: [cc2500_emotion, cc2500_emotion_sniffer]
```

Or from a local checkout (for development):

```yaml
external_components:
  - source:
      type: local
      path: components
```

List `cc2500_emotion_sniffer` only if you need it — see [SNIFFER.md](SNIFFER.md).

## Full example

```yaml
spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

cc2500_emotion:
  id: emotion
  cs_pin: GPIO5
  gdo0_pin: GPIO4         # optional
  remote_id: "68049F25"   # your remote's ID (4 bytes hex)
  receive: true           # listen to the air and sync state
  step_interval: 200ms    # pause between brightness step transmissions

light:
  - platform: cc2500_emotion
    name: "L&S Emotion"
    cc2500_emotion_id: emotion
    brightness_levels: 5
    ramp_duration: 1000ms
    cold_white_color_temperature: 6500 K
    warm_white_color_temperature: 2700 K
```

Ready-made configs in the repository: [`example.yaml`](example.yaml) (production — Wi-Fi, API, OTA,
light, buttons, sniffer), [`bench.yaml`](bench.yaml) (bench over USB), [`sniffer.yaml`](sniffer.yaml)
(receive only).

## `cc2500_emotion` — the hub (radio driver)

Owns the CC2500 module: configures it, transmits and receives frames. It is `MULTI_CONF`, so you can
declare several hubs if you want (two modules on separate SPI buses).

| Option | Default | Description |
|---|---|---|
| `cs_pin` | — (**required**) | CS pin (the module's `CSn`) |
| `gdo0_pin` | — (opt.) | `GDO0`, used to detect end of transmission. Without it, the `MARCSTATE` register is polled |
| `remote_id` | — (opt.) | Remote ID: 4 bytes hex, `"68049F25"` or `"68:04:9F:25"`. **Without it transmission is disabled** and the device only listens |
| `receive` | `true` | Keep the chip in receive mode: commands from the physical remote update the state in Home Assistant |
| `step_interval` | `200ms` | Pause between individual brightness step transmissions |
| `id` | (auto) | Identifier used by `light` and by lambdas |

Plus the standard SPI device schema keys.

**Why `remote_id` has no default.** The ID belongs to one specific remote. A shipped default would
make everyone else's device transmit under the author's ID — that is, control someone else's lamps.
So the option is optional and has no default: if it is unset the component refuses to transmit (with
an error in the log) and works as a receiver. How to find your ID — [SNIFFER.md](SNIFFER.md).

**What `step_interval` is for.** The lamp collapses brightness steps that arrive back to back
(~14 ms) into one. So steps go through a non-blocking queue: one transmission per `step_interval`.
If your lamp drops steps when the slider moves fast, increase it; if sweeping across all levels feels
slow, decrease it.

### Public methods (for lambdas)

Available as `id(emotion).…` inside any `lambda`:

| Method | Action |
|---|---|
| `send_on(uint8_t color_temp)` | Turn on at the given color temperature (`0x80` is the middle) |
| `send_off()` | Turn off |
| `send_color_temp(uint8_t value)` | Color temperature: `0x00` warm … `0xFF` cold |
| `brightness_step(int direction)` | Brightness step: `1` brighter, `-1` dimmer. Updates both the lamp and the state in Home Assistant. Always transmits, even at an end stop (resyncs UI ↔ lamp) |
| `send_brightness_up()` / `send_brightness_down()` | A brightness step that bypasses Home Assistant state — transmit only, trackers untouched |
| `send_pair()` | Pairing — announce our `remote_id` to a lamp in learning mode |
| `send_raw(uint8_t cmd, uint8_t val)` | Arbitrary command — for protocol experiments |

For buttons in Home Assistant use `brightness_step()`: it keeps HA state and the lamp together. It
works through the `light` platform, so in a config without a light (for example
[`sniffer.yaml`](sniffer.yaml)) it does nothing — use `send_brightness_up()`/`_down()` there.

## `light` — the `cc2500_emotion` platform

An ordinary ESPHome light of type `COLOR_TEMPERATURE`.

| Option | Default | Description |
|---|---|---|
| `cc2500_emotion_id` | (auto) | Reference to the `cc2500_emotion` hub |
| `brightness_levels` | `5` | Number of discrete brightness levels the lamp has (2…32) |
| `ramp_duration` | `1000ms` | Time for a full min↔max brightness ramp — used to estimate a held button on the remote |
| `cold_white_color_temperature` | `6500 K` | Cold end of the range |
| `warm_white_color_temperature` | `2700 K` | Warm end of the range |
| `restore_mode` | `RESTORE_DEFAULT_OFF` | State restoration across reboots |
| `gamma_correct` | `1.0` | **Do not change** — anything else breaks the mapping between levels and percentages |
| `default_transition_length` | `0s` | A stepped device has no smooth fades |

Plus the standard `light` keys (`name`, `icon`, `id`, …).

**About brightness.** Home Assistant always attaches a brightness slider to any entity that has
color temperature — "color temperature without brightness" is impossible on a single light. So the
model is absolute: the component tracks the current level and steps the difference when it changes,
without flicker. Five levels are presented as **1 / 25 / 50 / 75 / 100 %** (the bottom one is pinned
to 1 % because 0 % means "off" in Home Assistant). At the end stops one extra step is sent as
headroom — the lamp clamps, and that is a resynchronization point.

**About `restore_mode`.** The default is overridden from the base `ALWAYS_OFF`, which lost the state
on every reboot. The restored state is **not transmitted to the lamp**: it remembers its own, and
blindly retransmitting would overwrite whatever was done by remote while the ESP was off.

## Extra buttons

The buttons are not a separate platform but plain `template button`s calling hub methods — so you
can rename them, drop them or add your own without touching the component. Example from
[`example.yaml`](example.yaml):

```yaml
button:
  - platform: template
    name: "Pair"
    on_press:
      lambda: "id(emotion).send_pair();"

  - platform: template
    name: "Brighter"
    on_press:
      lambda: "id(emotion).brightness_step(1);"
  - platform: template
    name: "Dimmer"
    on_press:
      lambda: "id(emotion).brightness_step(-1);"

  # Raw command: cmd and value are typed in Home Assistant, no reflashing.
  - platform: template
    name: "Raw send"
    on_press:
      lambda: |-
        uint8_t cmd = (uint8_t) strtol(id(raw_cmd).state.c_str(), nullptr, 16);
        uint8_t val = (uint8_t) strtol(id(raw_val).state.c_str(), nullptr, 16);
        id(emotion).send_raw(cmd, val);

text:
  - platform: template
    name: "Raw cmd (hex)"
    id: raw_cmd
    optimistic: true
    restore_value: true
    initial_value: "0x11"
    mode: text
    max_length: 4
  - platform: template
    name: "Raw value (hex)"
    id: raw_val
    optimistic: true
    restore_value: true
    initial_value: "0x00"
    mode: text
    max_length: 4
```

The "Brighter"/"Dimmer" buttons **always** transmit a step, even when the slider is already at an end
stop: if Home Assistant and the lamp have drifted apart, this is how you nudge them back together.

## Home Assistant entities

| Entity | What it does |
|---|---|
| **Light** "L&S Emotion" | On/off, brightness (5 levels → 1/25/50/75/100 %), color temperature |
| **Button** "Pair" | Announce `remote_id` to a lamp in learning mode |
| **Button** "Brighter" / "Dimmer" | One brightness step, always transmitted |
| **Button** "Raw send" | Send the command from the fields below |
| **Text** "Raw cmd (hex)" / "Raw value (hex)" | The bytes for "Raw send" |
| **Text sensor** "Discovered remotes" | Diagnostic: remotes heard on the air (when the sniffer is enabled) |

Presses on the **physical remote** are reflected in Home Assistant automatically: on/off and color
temperature exactly, brightness by steps or by ramp estimate.

## Features and limitations

- **There is no feedback from the lamp.** The protocol is one-way: the lamp never answers. The state
  in Home Assistant is an estimate. Trustworthy anchors: absolute commands (on/off, color
  temperature), the brightness end stops, and commands received from the remote.
- **Brightness has 5 discrete levels.** The slider quantizes to 1/25/50/75/100 %. Sweeping end to end
  takes up to ~1 s because steps go out one at a time, spaced by `step_interval`.
- **The remote sends brightness commands as singles** (unlike on/off, which is repeated five times),
  so an individual transmission is occasionally lost in the air. This self-corrects at the end stops
  and on the next action.
- **A held button on the remote** (`0x10`/`0x20`, stopped by `0x40`) is estimated from elapsed time
  via `ramp_duration` — that is, approximately. If the `0x40` is lost, the ramp is finalized by
  timeout.
- **After a reboot** the state is restored from flash and shown in Home Assistant, but not
  transmitted to the lamp. If the lamp was changed by remote while the ESP was down, Home Assistant
  shows the last known state — it realigns on the first action.
- **Half duplex.** One radio does both receive and transmit: while transmitting, the chip is not
  listening. In practice this is invisible — a transmission takes a few milliseconds.
- **2.4 GHz, i.e. inside Wi-Fi.** The band is shared with Wi-Fi and Bluetooth, which costs range.
  For first tests, shorten the distance to the lamp.

## Protocol commands

| cmd | Action | val |
|---|---|---|
| `0x01` | Turn on | current color temperature |
| `0x02` | Turn off | `0x00` |
| `0x11` / `0x21` | Brightness one step up / down | `0x00` |
| `0x10` / `0x20` | Start a smooth ramp up / down (~1 s end to end) | `0x00` |
| `0x40` | Stop the ramp | `0x00` |
| `0x50` | Color temperature (`0x00` warm … `0xFF` cold) | level |
| `0x60` | Pairing | `0x00` |

⚠️ `0x10`/`0x20` are **not** an instant maximum/minimum but the start of a smooth ramp: the final
level depends on when the `0x40` arrives. The component never transmits them — it reaches the end
stops with steps plus headroom instead. On reception it does recognize them: that is what the remote
sends while a button is held.

Holding a button on the remote is not a single transmission but a stream: `0x10`/`0x20` repeat about
**every 12 ms** for as long as the button is down, ending with a single `0x40`.

Commands outside this table are logged by the sniffer as `UNKNOWN` together with all 14 frame bytes —
so if your remote can do something more, you will see it immediately and can try reproducing it via
"Raw send".

### Frame format

```
0E | ID₀ ID₁ ID₂ ID₃ | cmd | val | 00 00 00 00 00 00 | chk | 16
```

`0E` is the length (14 bytes), `chk = 0x7E ⊕ ID₀ ⊕ ID₁ ⊕ ID₂ ⊕ ID₃ ⊕ 0x16 ⊕ cmd ⊕ val`.

Modulation (MSK, 250 kBaud, 2436.2 MHz), the `0xD391` sync word, FEC and the CRC are all handled by
the CC2500 in hardware from the loaded register map — the software only prepares these 14 bytes.

The complete protocol analysis — RF parameters, all 45 registers, `PATABLE`, the reverse-engineering
method — is in
[`captures/PROTOCOL-FINDINGS.md`](https://github.com/SergeyZh/emotion-control-re/blob/main/captures/PROTOCOL-FINDINGS.md)
of the [`emotion-control-re`](https://github.com/SergeyZh/emotion-control-re) repository.

## Development

- ESPHome lives in `./.venv/` (a project venv). Developed against 2026.7.
- Compile only, no flashing: `./.venv/bin/esphome compile bench.yaml`.
- Flash with logs: `./.venv/bin/esphome run bench.yaml --device /dev/cu.usbserial-0001`. The port may
  differ — check `ls /dev/cu.usbserial*`. Stuck at `Connecting...`? Hold BOOT.
- `bench.yaml` takes the remote ID from `secrets.yaml` (`emotion_remote_id`) so a personal ID never
  reaches the repository.
- The ESP32 toolchain is cached in `~/Library/Caches/esphome`, which keeps rebuilds fast.
- Implementation notes, decisions taken and pitfalls hit — [CLAUDE.md](CLAUDE.md); plan and phase
  status — [PLAN.md](PLAN.md).
