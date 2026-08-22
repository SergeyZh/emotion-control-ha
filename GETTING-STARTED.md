[![en](https://img.shields.io/badge/lang-en-blue.svg)](GETTING-STARTED.md)
[![ru](https://img.shields.io/badge/lang-ru-green.svg)](GETTING-STARTED.ru.md)
[![de](https://img.shields.io/badge/lang-de-yellow.svg)](GETTING-STARTED.de.md)

# Getting started: an L&S light in Home Assistant

This guide goes from "I bought an ESP32" to "the light is controlled from Home Assistant". No
ESPHome experience is required. A CC2500 board with fitted header pins can be connected without
soldering; the smaller pad-only board still needs six solder joints. Budget about an hour, most of
which is the first firmware build.

The idea is simple: **the ESP32 impersonates your remote.** It transmits the same commands with the
same identifier, so the lamp obeys it immediately — no pairing, no modification of the lamp. That is
why the first real step is finding out your remote's ID.

## Step 0. Build the hardware

You will need:

| What | Requirements | Notes |
|---|---|---|
| **ESP32** | Any board with USB | Verified on a DevKit V1 (ESP-WROOM-32) |
| **CC2500 module** | 2.4 GHz, **26.000 MHz** crystal | Verified choices: [small pad module](https://de.aliexpress.com/item/1005005501196142.html), or [TinyTronics module with header pins](https://www.tinytronics.nl/en/communication-and-signals/wireless/rf/modules/cc2500-wireless-rf-module-2.4ghz) |
| **Soldering iron** | Optional | Needed only for a module without fitted pins |
| Wires | 6 or 7 female Dupont jumpers | For the pad module, one end is soldered instead |

The small AliExpress board has nine flat pads along the bottom edge of a 12 × 19 mm board on a
**1.27 mm** pitch. It needs six direct solder joints (`SI`, `SCLK`, `SO`, `CSn`, `VDD`, `VSS`);
use thin hookup wire and clamp the board while soldering. The drawing with pad numbering is
[`datasheets/CC2500_module_photo.jpeg`](datasheets/CC2500_module_photo.jpeg). The verified
TinyTronics board has fitted pins, so female Dupont jumpers connect it to the ESP32 without soldering.
Its pin order is different: always follow the signal labels printed on your board.

When you are done, check neighbouring pads for shorts: at this pitch a solder bridge between
`SI`/`SCLK` or `CSn`/`XO` is the single most likely assembly mistake.

⚠️ **Do not confuse CC2500 with CC1101** — they look nearly identical, but CC1101 works on
433/868 MHz and will never talk to this lamp. Check the marking on the chip itself.

⚠️ **The crystal must be 26 MHz.** The RF registers were captured off the remote and are applied
as-is; with a 27 MHz crystal the frequency and data rate drift off and the lamp will not hear you.
The crystal is labelled on the module.

Wire it up as follows (this is an example pinout — any pin can be changed in YAML):

| CC2500 module | Signal | ESP32 |
|---|---|---|
| SI (2) | MOSI | GPIO23 |
| SCLK (3) | SCLK | GPIO18 |
| SO (4) | MISO | GPIO19 |
| CSn (8) | CS | GPIO5 |
| GDO0 (7) | end of transmission | GPIO4 |
| VDD (1) | power | **3V3** |
| VSS (6) | ground | GND |

- **Power from the `3V3` pin only.** 5 V (`VIN`) will kill the module.
- Leave `GDO2` unconnected — it is not used.
- `GDO0` is optional: without it, end of transmission is detected by polling the chip over SPI.
  Connect it if you have a pin to spare — it is slightly more precise.
- Pad numbering is in [`datasheets/CC2500_module_pinout.md`](datasheets/CC2500_module_pinout.md);
  the board drawing with numbers and the application schematic sit next to it.

## Step 1. Install ESPHome

ESPHome is what turns a YAML config into firmware. Install it into its own venv to keep your system
clean:

```bash
git clone https://github.com/SergeyZh/emotion-control-ha
cd emotion-control-ha
python3 -m venv .venv
./.venv/bin/pip install esphome
```

From here on, every command goes through `./.venv/bin/esphome`. Find your board's USB port:

```bash
ls /dev/cu.usbserial*      # macOS
ls /dev/ttyUSB*            # Linux
```

Substitute the port you found for `/dev/cu.usbserial-0001` in the examples below.

## Step 2. Find your remote's ID

`remote_id` identifies the transmitter. It is either a four-byte ID in the 14-byte broadcast
protocol, or a three-byte request route in the 8-byte addressed protocol. Every installation has its
own value, so **there is no default** — it has to be sniffed off the air. The same CC2500 module does
that; nothing else is needed.

Flash the sniffer config. `remote_id` is deliberately unset in it, so the device only listens and
never transmits:

```bash
./.venv/bin/esphome run sniffer.yaml --device /dev/cu.usbserial-0001
```

Wait for the init banner in the log:

```
CC2500 PARTNUM=0x80 VERSION=0x03
Radio check: SYNC1=0xD3 MDMCFG2=0x73 CHANNR=0x10
Radio configured and calibrated.
```

If those lines never appear, jump to [Troubleshooting](#troubleshooting): there is no point going
further while the radio is not responding.

Now **press buttons on the remote** while holding it next to the board. Each press drops several
lines into the log:

```
[I][cc2500_emotion_sniffer]: remote 68049F25  cmd=0x01 (on)  val=0x80  RSSI=-38 dBm  LQI=2  offset=+3 kHz
[I][cc2500_emotion_sniffer]: remote 68ABF2    cmd=0x01 (on)  val=0x7D  RSSI=-65 dBm  LQI=13 offset=-4 kHz
```

The six- or eight-digit value after `remote` is what you are after. **Write it down.** A six-digit
value means the sniffer found the compact 8-byte variant and normalized its request/response route.

The sniffer sees every Emotion remote in range, not just yours (your neighbours may have these
lights too). Yours is identified by `RSSI`: it is in your hand, so its signal is noticeably
stronger — typically −30…−50 dBm versus −70 or worse for someone else's. To be sure, press a button
a few times and confirm the same ID keeps appearing.

More on the sniffer, `LQI`, other people's remotes and the list of discovered IDs inside Home
Assistant — [SNIFFER.md](SNIFFER.md).

## Step 3. Enter Wi-Fi and the ID

The production config [`example.yaml`](example.yaml) takes credentials from `secrets.yaml`, a file
that never lands in git. Create it alongside:

```yaml
# secrets.yaml
wifi_ssid: "YourNetwork"
wifi_password: "password"
emotion_remote_id: "68049F25"                 # the ID or route from step 2
```

The Home Assistant connection is unencrypted by default. To encrypt it, generate a key:

```bash
./.venv/bin/python -c "import os,base64; print(base64.b64encode(os.urandom(32)).decode())"
```

put it into `secrets.yaml` as `emotion_control__encryption_key`, and extend the `api:` block in
`example.yaml`:

```yaml
api:
  encryption:
    key: !secret emotion_control__encryption_key
```

## Step 4. Flash the production config

```bash
./.venv/bin/esphome run example.yaml --device /dev/cu.usbserial-0001
```

The first time has to be over USB. After that the board updates over the air (OTA) and `--device`
is no longer needed.

If flashing hangs at `Connecting...`, hold the **BOOT** button on the board and keep it pressed
until the upload starts.

The log should show:

```
CC2500 PARTNUM=0x80 VERSION=0x03
Radio configured and calibrated.
Listening for remote commands (RX enabled, MARCSTATE=0x0D).
```

## Step 5. Add it to Home Assistant

The device announces itself: go to **Settings → Devices & services** and ESPHome will offer the
discovered `emotion-control` — click "Configure". If you enabled encryption, paste
`emotion_control__encryption_key` from `secrets.yaml` when asked. (Not discovered? "Add integration"
→ ESPHome → host `emotion-control.local`.)

You will get these entities:

- **Light "L&S Emotion"** — on/off, brightness, color temperature.
- **Buttons** "Pair", "Brighter", "Dimmer", "Raw send".
- **Text fields** "Raw cmd (hex)" / "Raw value (hex)" — for protocol experiments.
- **"Discovered remotes"** — a diagnostic list of remotes heard on the air.

## Step 6. Verify

In this order, so you know where things broke if they do:

1. **Turn the lamp on from Home Assistant.** It lights up → transmission works and the ID is
   correct. It doesn't → the lamp is not hearing you: check the ID and the distance (for the first
   test, stand 2–3 metres from the light).
2. **Move the color temperature slider.** It changes smoothly — this is an absolute command, the most
   reliable one.
3. **Move the brightness slider.** It snaps across five levels: 1 / 25 / 50 / 75 / 100 %. That is by
   design; the lamp has exactly five.
4. **Press a button on the physical remote.** The state in Home Assistant should change on its own —
   that means reception works and HA will not drift away from the lamp.

Working? You are done. Optional next stops: [CONFIGURATION.md](CONFIGURATION.md) (all options,
entities, protocol commands) and [SNIFFER.md](SNIFFER.md) (Home Assistant automations on remote
buttons).

## Troubleshooting

### `PARTNUM`/`VERSION` never appear, or read as `0x00`/`0xFF`
The radio is not answering over SPI. Check, in order:
- **Power from `3V3`**, not `VIN`. And that ground is common.
- **MOSI/MISO not swapped** — the classic. The module's `SI` goes to MOSI (GPIO23), `SO` to MISO
  (GPIO19).
- **`CSn` connected** to the pin named in `cs_pin`.
- **The soldering:** buzz out every pad to its ESP32 pin (cold joint?) and neighbouring pads against
  each other (solder bridge?). At a 1.27 mm pitch these are the two most common faults.
- **No wire snapped off at the pad** — thin wire breaks when flexed on a board like this, and the
  break can be invisible. Tug each wire gently.

### The radio initializes but the lamp does not react
- **Check the ID** — by far the most common cause. Reflash `sniffer.yaml`, press a button and
  compare byte by byte.
- **Is `remote_id` set at all?** Without it the component refuses to transmit and logs an error —
  such a config only listens.
- **Distance.** Modules without a decent antenna can be limited to a few metres. Move closer.
- **Rule out the lamp:** press the real remote — does the lamp respond?

### The remote does not show up in the sniffer
- Hold the remote right against the board and press buttons **several times** — a single
  transmission can be lost.
- **Replace the battery in the remote.** A dying one produces exactly the "sometimes gets through"
  behaviour: the lamp still obeys it, while your receiver no longer hears it.
- Reception diagnostics (`LQI`, `CRC failed`, `RX FIFO overflow`) are covered in
  [SNIFFER.md](SNIFFER.md#when-reception-is-flaky).

### The board will not flash
- Hold **BOOT** while it connects.
- Check the port: `ls /dev/cu.usbserial*`. Some boards need a CH340/CP2102 driver.
- Another program is holding the port (an open serial monitor, Arduino IDE) — close it.

### The state in Home Assistant disagrees with the lamp
The state is partly estimated, especially brightness. It realigns on the next action or when you
press the physical remote; the 8-byte ED 12/30-1 also sends addressed replies. Details are in
[CONFIGURATION.md](CONFIGURATION.md#features-and-limitations).

## Alternative for the 14-byte variant: pairing instead of cloning an ID

Cloning the remote's ID is the recommended path: the lamp obeys immediately and remote presses are
visible in Home Assistant. On the 14-byte broadcast variant you can instead give the device its
**own** ID and pair it with the lamp. This has not been verified for the 8-byte variant.

1. Pick any ID (4 bytes hex) and put it in `remote_id`.
2. Put the lamp into learning mode (per the L&S manual — usually holding a button on the receiver).
3. Press the **"Pair"** button in Home Assistant — the device announces its ID and the lamp
   memorizes it.

⚠️ With your own ID, **synchronization with the physical remote will not work**: the device only
listens for the ID set in `remote_id`, while the remote sends its own. If seeing remote presses in
Home Assistant matters to you, clone its ID.

## Bench mode (no Wi-Fi)

To test the radio and transmission without Home Assistant there is [`bench.yaml`](bench.yaml): logs
over USB, no Wi-Fi needed, and the **BOOT** button on the board toggles the lamp on and off. The ID
comes from `secrets.yaml` (`emotion_remote_id`). Handy for separating radio problems from network
problems:

```bash
./.venv/bin/esphome run bench.yaml --device /dev/cu.usbserial-0001
```
