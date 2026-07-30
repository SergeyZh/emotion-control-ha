[![en](https://img.shields.io/badge/lang-en-blue.svg)](CONFIGURATION.md)
[![ru](https://img.shields.io/badge/lang-ru-green.svg)](CONFIGURATION.ru.md)
[![de](https://img.shields.io/badge/lang-de-yellow.svg)](CONFIGURATION.de.md)

# Konfigurationsreferenz

Alle YAML-Optionen, die Entitäten in Home Assistant, die Protokollbefehle und die Besonderheiten der
Implementierung. Wenn Sie zum ersten Mal hier sind, beginnen Sie mit
[GETTING-STARTED.de.md](GETTING-STARTED.de.md).

## Komponente einbinden

Die Komponente wird als External Component eingebunden — direkt von GitHub:

```yaml
external_components:
  - source: github://SergeyZh/emotion-control-ha
    components: [cc2500_emotion, cc2500_emotion_sniffer]
```

Oder aus einer lokalen Kopie des Repositorys (für die Entwicklung):

```yaml
external_components:
  - source:
      type: local
      path: components
```

Geben Sie `cc2500_emotion_sniffer` nur an, wenn Sie ihn brauchen — siehe
[SNIFFER.de.md](SNIFFER.de.md).

## Vollständiges Beispiel

```yaml
spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

cc2500_emotion:
  id: emotion
  cs_pin: GPIO5
  gdo0_pin: GPIO4         # optional
  remote_id: "68049F25"   # ID Ihrer Fernbedienung (4 Byte hex)
  receive: true           # Funk mithören und Zustand synchronisieren
  step_interval: 200ms    # Pause zwischen Helligkeitsschritten

light:
  - platform: cc2500_emotion
    name: "L&S Emotion"
    cc2500_emotion_id: emotion
    brightness_levels: 5
    ramp_duration: 1000ms
    cold_white_color_temperature: 6500 K
    warm_white_color_temperature: 2700 K
```

Fertige Konfigurationen im Repository: [`example.yaml`](example.yaml) (produktiv — WLAN, API, OTA,
Licht, Schaltflächen, Sniffer), [`bench.yaml`](bench.yaml) (Laborbetrieb über USB),
[`sniffer.yaml`](sniffer.yaml) (nur Empfang).

## `cc2500_emotion` — der Hub (Funktreiber)

Besitzt das CC2500-Modul: konfiguriert es, sendet und empfängt Frames. Die Komponente ist
`MULTI_CONF`, Sie können also mehrere Hubs deklarieren (zwei Module an getrennten SPI-Bussen).

| Option | Standard | Beschreibung |
|---|---|---|
| `cs_pin` | — (**erforderlich**) | CS-Pin (`CSn` des Moduls) |
| `gdo0_pin` | — (opt.) | `GDO0` zur Erkennung des Sendeendes. Ohne ihn wird das Register `MARCSTATE` abgefragt |
| `remote_id` | — (opt.) | Fernbedienungs-ID: 4 Byte hex, `"68049F25"` oder `"68:04:9F:25"`. **Ohne sie ist das Senden deaktiviert**, das Gerät hört nur zu |
| `receive` | `true` | Chip im Empfang halten: Befehle der physischen Fernbedienung aktualisieren den Zustand in Home Assistant |
| `step_interval` | `200ms` | Pause zwischen einzelnen Sendungen von Helligkeitsschritten |
| `id` | (auto) | Kennung für Verweise aus `light` und aus Lambdas |

Dazu die Standardschlüssel des SPI-Geräteschemas.

**Warum `remote_id` keinen Standardwert hat.** Die ID gehört zu einer bestimmten Fernbedienung. Ein
mitgelieferter Standardwert würde fremde Geräte unter der ID des Autors senden lassen — also fremde
Leuchten steuern. Deshalb ist die Option optional und ohne Standardwert: Ist sie nicht gesetzt,
verweigert die Komponente das Senden (mit Fehler im Log) und arbeitet als Empfänger. Wie Sie Ihre ID
ermitteln — [SNIFFER.de.md](SNIFFER.de.md).

**Wozu `step_interval` dient.** Die Leuchte fasst Helligkeitsschritte, die unmittelbar aufeinander
folgen (~14 ms), zu einem zusammen. Deshalb laufen Schritte über eine nicht blockierende
Warteschlange: eine Sendung pro `step_interval`. Verliert Ihre Leuchte bei schneller Reglerbewegung
Schritte, erhöhen Sie den Wert; wirkt der Durchlauf über alle Stufen zu langsam, verkleinern Sie ihn.

### Öffentliche Methoden (für Lambdas)

Verfügbar als `id(emotion).…` in jedem `lambda`:

| Methode | Wirkung |
|---|---|
| `send_on(uint8_t color_temp)` | Mit der angegebenen Farbtemperatur einschalten (`0x80` ist die Mitte) |
| `send_off()` | Ausschalten |
| `send_color_temp(uint8_t value)` | Farbtemperatur: `0x00` warm … `0xFF` kalt |
| `brightness_step(int direction)` | Helligkeitsschritt: `1` heller, `-1` dunkler. Aktualisiert Leuchte und Zustand in Home Assistant. Sendet **immer**, auch am Endanschlag (Resynchronisierung UI ↔ Leuchte) |
| `send_brightness_up()` / `send_brightness_down()` | Helligkeitsschritt am HA-Zustand vorbei — nur senden, Tracker bleiben unberührt |
| `send_pair()` | Kopplung — die eigene `remote_id` einer Leuchte im Lernmodus verkünden |
| `send_raw(uint8_t cmd, uint8_t val)` | Beliebiger Befehl — für Protokollexperimente |

Für Schaltflächen in Home Assistant brauchen Sie `brightness_step()`: Es hält HA-Zustand und Leuchte
zusammen. Die Methode arbeitet über die `light`-Plattform, in einer Konfiguration ohne Licht (etwa
[`sniffer.yaml`](sniffer.yaml)) tut sie daher nichts — nehmen Sie dort
`send_brightness_up()`/`_down()`.

## `light` — die Plattform `cc2500_emotion`

Ein gewöhnliches ESPHome-Licht vom Typ `COLOR_TEMPERATURE`.

| Option | Standard | Beschreibung |
|---|---|---|
| `cc2500_emotion_id` | (auto) | Verweis auf den Hub `cc2500_emotion` |
| `brightness_levels` | `5` | Anzahl der diskreten Helligkeitsstufen der Leuchte (2…32) |
| `ramp_duration` | `1000ms` | Dauer eines vollen Helligkeitsdurchlaufs min↔max — zur Schätzung einer gehaltenen Taste auf der Fernbedienung |
| `cold_white_color_temperature` | `6500 K` | Kaltes Ende des Bereichs |
| `warm_white_color_temperature` | `2700 K` | Warmes Ende des Bereichs |
| `restore_mode` | `RESTORE_DEFAULT_OFF` | Wiederherstellung des Zustands nach Neustart |
| `gamma_correct` | `1.0` | **Nicht ändern** — sonst bricht die Zuordnung von Stufen zu Prozentwerten |
| `default_transition_length` | `0s` | Ein gestuftes Gerät kennt keine weichen Übergänge |

Dazu die Standardschlüssel von `light` (`name`, `icon`, `id`, …).

**Zur Helligkeit.** Home Assistant hängt an jede Entität mit Farbtemperatur zwingend einen
Helligkeitsregler — „Farbtemperatur ohne Helligkeit“ ist an einem einzelnen Licht nicht möglich.
Deshalb ist das Modell absolut: Die Komponente verfolgt die aktuelle Stufe und schrittet bei einer
Änderung die Differenz, ohne Flackern. Fünf Stufen erscheinen als
**1 / 25 / 50 / 75 / 100 %** (die unterste liegt auf 1 %, weil 0 % in Home Assistant „aus“ bedeutet).
An den Endanschlägen wird ein zusätzlicher Schritt als Reserve gesendet — die Leuchte begrenzt selbst,
und das ist ein Resynchronisierungspunkt.

**Zu `restore_mode`.** Der Standardwert weicht bewusst vom Basiswert `ALWAYS_OFF` ab, der den Zustand
bei jedem Neustart verlor. Der wiederhergestellte Zustand wird **nicht an die Leuchte gesendet**: Sie
kennt ihren eigenen, und blindes Nachsenden würde überschreiben, was in der Zwischenzeit per
Fernbedienung eingestellt wurde.

## Zusätzliche Schaltflächen

Die Schaltflächen sind keine eigene Plattform, sondern gewöhnliche `template button`, die Hub-Methoden
aufrufen — so können Sie sie umbenennen, entfernen oder eigene ergänzen, ohne den Komponentencode zu
ändern. Beispiel aus [`example.yaml`](example.yaml):

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

  # Roher Befehl: cmd und value werden direkt in Home Assistant eingegeben, ohne neu zu flashen.
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

Die Schaltflächen „Brighter“/„Dimmer“ senden **immer** einen Schritt, auch wenn der Regler schon am
Endanschlag steht: Sind Home Assistant und Leuchte auseinandergelaufen, richten Sie sie damit von Hand
wieder aus.

## Entitäten in Home Assistant

| Entität | Funktion |
|---|---|
| **Licht** „L&S Emotion“ | Ein/aus, Helligkeit (5 Stufen → 1/25/50/75/100 %), Farbtemperatur |
| **Schaltfläche** „Pair“ | `remote_id` einer Leuchte im Lernmodus verkünden |
| **Schaltfläche** „Brighter“ / „Dimmer“ | Ein Helligkeitsschritt, wird immer gesendet |
| **Schaltfläche** „Raw send“ | Den Befehl aus den Feldern unten senden |
| **Text** „Raw cmd (hex)“ / „Raw value (hex)“ | Die Bytes für „Raw send“ |
| **Text-Sensor** „Discovered remotes“ | Diagnose: im Funk gehörte Fernbedienungen (wenn der Sniffer aktiv ist) |

Tastendrücke auf der **physischen Fernbedienung** erscheinen automatisch in Home Assistant: ein/aus
und Farbtemperatur exakt, Helligkeit über Schritte oder über die Ramp-Schätzung.

## Eigenschaften und Einschränkungen

- **Es gibt keine Rückmeldung von der Leuchte.** Das Protokoll ist unidirektional: Die Leuchte
  antwortet nie. Der Zustand in Home Assistant ist eine Schätzung. Verlässliche Fixpunkte: absolute
  Befehle (ein/aus, Farbtemperatur), die Endanschläge der Helligkeit und die von der Fernbedienung
  empfangenen Befehle.
- **Die Helligkeit hat 5 diskrete Stufen.** Der Regler rastet auf 1/25/50/75/100 %. Ein Durchlauf von
  Anschlag zu Anschlag dauert bis zu ~1 s, weil die Schritte einzeln und um `step_interval` versetzt
  gesendet werden.
- **Helligkeitsbefehle sendet die Fernbedienung einzeln** (anders als ein/aus, das fünfmal wiederholt
  wird), deshalb geht eine einzelne Sendung im Funk gelegentlich verloren. Das korrigiert sich an den
  Endanschlägen und bei der nächsten Aktion.
- **Eine gehaltene Taste auf der Fernbedienung** (`0x10`/`0x20`, gestoppt mit `0x40`) wird über die
  Zeit anhand von `ramp_duration` geschätzt — also ungenau. Geht das `0x40` verloren, wird der
  Durchlauf per Timeout beendet.
- **Nach einem Neustart** wird der Zustand aus dem Flash geladen und in Home Assistant angezeigt, aber
  nicht an die Leuchte gesendet. Wurde die Leuchte während des Ausfalls per Fernbedienung verändert,
  zeigt Home Assistant den letzten bekannten Zustand — er richtet sich bei der ersten Aktion ein.
- **Halbduplex.** Ein Funkmodul für Empfang und Senden: Während des Sendens hört der Chip nicht. In der
  Praxis unbemerkbar — eine Sendung dauert wenige Millisekunden.
- **2,4 GHz, also mitten im WLAN.** Das Band wird mit WLAN und Bluetooth geteilt, das kostet Reichweite.
  Verkürzen Sie für die ersten Tests den Abstand zur Leuchte.

## Protokollbefehle

| cmd | Wirkung | val |
|---|---|---|
| `0x01` | Einschalten | aktuelle Farbtemperatur |
| `0x02` | Ausschalten | `0x00` |
| `0x11` / `0x21` | Helligkeit einen Schritt hoch / runter | `0x00` |
| `0x10` / `0x20` | Weichen Durchlauf hoch / runter starten (~1 s von Anschlag zu Anschlag) | `0x00` |
| `0x40` | Durchlauf stoppen | `0x00` |
| `0x50` | Farbtemperatur (`0x00` warm … `0xFF` kalt) | Stufe |
| `0x60` | Kopplung | `0x00` |

⚠️ `0x10`/`0x20` sind **kein** sofortiges Maximum/Minimum, sondern der Start eines weichen Durchlaufs:
Die Endstufe hängt davon ab, wann das `0x40` eintrifft. Die Komponente sendet sie nie — sie erreicht
die Endanschläge stattdessen über Schritte mit Reserve. Beim Empfang erkennt sie sie sehr wohl: Genau
das sendet die Fernbedienung bei gehaltener Taste.

Eine gehaltene Taste ist keine einzelne Sendung, sondern ein Strom: `0x10`/`0x20` wiederholen sich
etwa **alle 12 ms**, solange die Taste unten ist, und enden mit einem einzelnen `0x40`.

Befehle außerhalb dieser Tabelle protokolliert der Sniffer als `UNKNOWN` samt aller 14 Frame-Bytes —
kann Ihre Fernbedienung also mehr, sehen Sie es sofort und können es über „Raw send“ nachbauen.

### Frame-Format

```
0E | ID₀ ID₁ ID₂ ID₃ | cmd | val | 00 00 00 00 00 00 | chk | 16
```

`0E` ist die Länge (14 Byte), `chk = 0x7E ⊕ ID₀ ⊕ ID₁ ⊕ ID₂ ⊕ ID₃ ⊕ 0x16 ⊕ cmd ⊕ val`.

Modulation (MSK, 250 kBaud, 2436,2 MHz), das Sync-Wort `0xD391`, FEC und die CRC erledigt der CC2500
per Hardware anhand der geladenen Registerkarte — die Software stellt nur diese 14 Byte zusammen.

Die vollständige Protokollanalyse — HF-Parameter, alle 45 Register, `PATABLE`, Vorgehen beim Reverse
Engineering — steht in
[`captures/PROTOCOL-FINDINGS.md`](https://github.com/SergeyZh/emotion-control-re/blob/main/captures/PROTOCOL-FINDINGS.md)
des Repositorys [`emotion-control-re`](https://github.com/SergeyZh/emotion-control-re).

## Entwicklung

- ESPHome liegt in `./.venv/` (Projekt-venv). Entwickelt gegen 2026.7.
- Nur kompilieren, nicht flashen: `./.venv/bin/esphome compile bench.yaml`.
- Flashen mit Logs: `./.venv/bin/esphome run bench.yaml --device /dev/cu.usbserial-0001`. Der Port kann
  abweichen — prüfen Sie `ls /dev/cu.usbserial*`. Hängt es bei `Connecting...`? BOOT gedrückt halten.
- `bench.yaml` holt die Fernbedienungs-ID aus `secrets.yaml` (`emotion_remote_id`), damit eine
  persönliche ID nicht ins Repository gelangt.
- Die ESP32-Toolchain wird in `~/Library/Caches/esphome` zwischengespeichert, wiederholte Builds sind
  daher schnell.
- Implementierungsnotizen, getroffene Entscheidungen und aufgetretene Fallstricke —
  [CLAUDE.md](CLAUDE.md); Plan und Phasenstatus — [PLAN.de.md](PLAN.de.md).
