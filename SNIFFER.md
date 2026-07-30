[![en](https://img.shields.io/badge/lang-en-blue.svg)](SNIFFER.md)
[![ru](https://img.shields.io/badge/lang-ru-green.svg)](SNIFFER.ru.md)
[![de](https://img.shields.io/badge/lang-de-yellow.svg)](SNIFFER.de.md)

# Sniffer: finding remotes and getting events into Home Assistant

The `cc2500_emotion_sniffer` component listens to the air and shows **every** Emotion Control frame
in range — from any remote, not just yours. It is there for three things:

1. **Finding your remote's ID** — without it there is nothing to put in `remote_id`.
2. **Link diagnostics** — you can see whether frames arrive or get mangled.
3. **Home Assistant automations on remote buttons** — including buttons that control nothing of yours.

The sniffer does not occupy a radio of its own: it subscribes to the `cc2500_emotion` hub and
receives frames that have already been validated. So you can keep it in the production config next to
the light — it only logs and does not affect lamp control.

## Finding your remote's ID

The ready-made config is [`sniffer.yaml`](sniffer.yaml). `remote_id` is deliberately unset in it, so
the device only listens and never transmits (you do not know the ID to transmit with yet):

```bash
./.venv/bin/esphome run sniffer.yaml --device /dev/cu.usbserial-0001
```

Press buttons on the remote next to the board. Every recognized frame is logged as one line:

```
[I][cc2500_emotion_sniffer]: remote 68049F25  cmd=0x01 (on)  val=0x80  RSSI=-38 dBm  LQI=2  offset=+3 kHz
```

Your remote is the one with a noticeably higher `RSSI`: it is in your hand. Put the ID you found into
`remote_id` in the production config.

**You do not have to guess the ID, and random noise cannot fake one.** Frames are checked by the
hardware CRC and by a checksum computed **from the received ID bytes themselves** — which is why the
sniffer validates frames from any remote, not only from a previously known one. Either everything
adds up or the frame is dropped.

Unknown commands are printed together with the full frame (all 14 bytes) — the six "reserved" bytes
may turn out to be non-zero. This is also the mode for extending the command map: if your remote can
do something missing from the [command table](CONFIGURATION.md#protocol-commands), the log will show
both the code and the bytes.

## Options

```yaml
cc2500_emotion_sniffer:
  id: sniffer
  cc2500_emotion_id: emotion
  dump_raw: false
```

| Option | Default | Description |
|---|---|---|
| `cc2500_emotion_id` | (auto) | Reference to the `cc2500_emotion` hub. Reception (`receive`) is forced on — a listener without reception is pointless |
| `dump_raw` | `false` | Print all 14 frame bytes for **every** command. Unknown ones are always printed in full |
| `on_frame` | — | Automation on a received frame, **once per press** — see below |
| `id` | (auto) | Identifier referenced by the `text_sensor` platform |

## The list of discovered remotes in Home Assistant

If the sniffer sits in a config with Wi-Fi/API, discovered remotes are visible not only in the log
but in Home Assistant as well, through a diagnostic entity:

```yaml
cc2500_emotion_sniffer:
  id: sniffer
  cc2500_emotion_id: emotion

text_sensor:
  - platform: cc2500_emotion_sniffer
    cc2500_emotion_sniffer_id: sniffer
    discovered_remotes:
      name: "Discovered remotes"
```

| Option | Description |
|---|---|
| `cc2500_emotion_sniffer_id` | Reference to `cc2500_emotion_sniffer` |
| `discovered_remotes` | Entity holding the list of discovered remotes (standard `text_sensor` schema) |

The state is every remote heard since boot, each with its **best** signal level:

```
6804F74D -49dBm, 68049F25 -71dBm
```

Press a button on another remote and it appears in the list. Yours is identified by signal strength.
The best rather than the latest level is kept on purpose: that way the value converges and the entity
stops updating, instead of twitching on every frame — a held button produces about 85 of them per
second.

⚠️ **ESPHome entities are created at compile time**, so an entity per remote discovered on the air is
technically impossible — `esp32_ble_tracker` works the same way (only addresses written into the YAML
beforehand reach the UI, everything else lives in the logs). Hence the limit as well: the list holds
up to **8** remotes, after which new ones are not added — but the log still shows all of them.

## The `on_frame` trigger

Fires on every frame received from any remote in range — **once per press** (repeats within a burst
are not duplicated). This is what Home Assistant automations and the live air table are built on.

### Arguments

| Variable | Type | Value |
|---|---|---|
| `remote_id` | `std::string` | Remote ID, 8 hex characters (`"6804F74D"`) |
| `command` | `uint8_t` | Command byte |
| `command_name` | `std::string` | Decoded command name, or `"UNKNOWN"` |
| `value` | `uint8_t` | Value byte |
| `rssi` | `int` | Signal level, dBm |
| `lqi` | `uint8_t` | Demodulation error count (0 = perfect) |

⚠️ **`on_frame` fires for every Emotion remote in range**, including your neighbours'. If you only
want your own, filter by `remote_id` in the automation's condition.

**Why the trigger is deduplicated while the log is not.** The log sees every copy of a frame (that is
the link-quality indicator), whereas the trigger emits events: a held button produces ~85 frames per
second, and as many events would take down the Home Assistant API. So one press equals one firing.

### A live air table in Home Assistant

Who pressed what, when, and with what signal — on a dashboard.

Step 1 — send an event onto the HA bus (already included in [`example.yaml`](example.yaml)):

```yaml
cc2500_emotion_sniffer:
  id: sniffer
  cc2500_emotion_id: emotion
  on_frame:
    - homeassistant.event:
        event: esphome.emotion_frame
        variables:
          rid: !lambda "return remote_id;"
          cmd: !lambda "return command;"
          cname: !lambda "return command_name;"
          val: !lambda "return value;"
          sig: !lambda "return rssi;"
        data_template:
          remote_id: "{{ rid }}"
          command: "{{ cmd }}"
          command_name: "{{ cname }}"
          value: "{{ val }}"
          rssi: "{{ sig }}"
```

Step 2 — accumulate the events in entity attributes on the Home Assistant side
(`configuration.yaml`). Unlike a `text_sensor`, attributes have no 255-character limit, so the table
can be complete:

```yaml
template:
  - trigger:
      - platform: event
        event_type: esphome.emotion_frame
    sensor:
      - name: "Emotion air"
        state: "{{ trigger.event.data.remote_id }}"
        attributes:
          remotes: >
            {% set old = this.attributes.get('remotes', {}) %}
            {% set d = trigger.event.data %}
            {{ dict(old, **{d.remote_id: {
                 'command': d.command_name,
                 'value': d.value,
                 'rssi': d.rssi,
                 'last_seen': now().strftime('%H:%M:%S')}}) }}
```

Step 3 — a dashboard card:

```yaml
type: markdown
content: |
  | Remote | Command | Value | RSSI | Last seen |
  |---|---|---|---|---|
  {% for id, r in state_attr('sensor.emotion_air', 'remotes').items() -%}
  | `{{ id }}` | {{ r.command }} | {{ r.value }} | {{ r.rssi }} dBm | {{ r.last_seen }} |
  {% endfor %}
```

### An automation on a remote button

The same trigger works for ordinary automations — for example, hanging a Home Assistant scene off a
remote button that controls no lamp of yours:

```yaml
cc2500_emotion_sniffer:
  id: sniffer
  cc2500_emotion_id: emotion
  on_frame:
    # The "on" button of one specific remote → a custom event in HA
    - if:
        condition:
          lambda: 'return remote_id == "6804F74D" && command == 0x01;'
        then:
          - homeassistant.event:
              event: esphome.emotion_button_on
```

Trigger arguments are available in every lambda inside `on_frame`, conditions included.

## When reception is flaky

The remote repeats each press as a burst of about **5 identical frames** — that is the protocol's own
protection against losses. The sniffer prints **every** copy, so the number of lines per press is a
direct measure of link quality:

| Lines per press | Meaning |
|---|---|
| 4–5 | Healthy link |
| 1–2 | Half the copies are lost — works, but marginally |
| 0 | The frame is not detected at all |

In the log line, watch `LQI` (demodulation error count, **0 = perfect**) and `offset` (the estimated
carrier mismatch).

| Symptom | Cause |
|---|---|
| Many `Frame rejected: CRC failed` lines | The signal is being picked up but frames are damaged — weak signal or interference |
| No rejections, but hardly any frames | The receiver is not detecting the transmission: antenna, distance, shielding |
| `offset` consistently large in one direction | Crystal mismatch between the remote and the module |
| `offset` jumping ±68 kHz both ways | That is estimator noise at the compensation limit, not a real offset — there is nothing to correct |
| `RX FIFO overflow` and `length byte 0x…` with no button presses | False sync on Wi-Fi noise (we are inside 2.4 GHz). Harmless to data, but it steals reception time; bounded via `PKTLEN` |
| One remote reads worse than another | Check its battery — a dying one produces exactly "sometimes gets through". Sometimes simply reseating the battery helps |

⚠️ Do not try to "fix" a jumping `offset` by writing to `FSCTRL0`: when the estimate jumps, it is
noise, and compensating it would introduce a random detuning and make reception worse. This has been
measured.

## How it works inside

Useful if you intend to modify the code:

- There is one radio. The sniffer subscribes to the hub via `register_frame_listener()` and forces
  `receive` on.
- The hub drains **all** complete frames from the FIFO at once, not just one: a burst of 5 copies is
  precisely what makes the protocol robust, so throwing away 4 of 5 is not an option.
- Frame processing order: length → CRC → type byte and checksum (computed from the received ID bytes,
  hence valid for any remote) → deduplication (~150 ms window keyed on ID+cmd+val) → **the sniffer's
  listener** → filter by our own `remote_id` → action on the light.
- The ID filter sits **after** the listener — otherwise the sniffer would never see other remotes,
  i.e. could not show you the ID you are looking for.
- The listener is called before deduplication and receives a `repeat` flag: the log prints every copy,
  while the `on_frame` trigger fires only for the first.

More detail — [CLAUDE.md](CLAUDE.md).
