[![en](https://img.shields.io/badge/lang-en-blue.svg)](SNIFFER.md)
[![ru](https://img.shields.io/badge/lang-ru-green.svg)](SNIFFER.ru.md)
[![de](https://img.shields.io/badge/lang-de-yellow.svg)](SNIFFER.de.md)

# Sniffer: Fernbedienungen finden und Ereignisse in Home Assistant

Die Komponente `cc2500_emotion_sniffer` hört den Funkverkehr mit und zeigt **alle** Emotion-Control-
Frames in Empfangsreichweite — von jeder Fernbedienung, nicht nur von Ihrer. Sie dient drei Zwecken:

1. **Die ID der eigenen Fernbedienung ermitteln** — ohne sie gibt es nichts, was in `remote_id`
   gehört.
2. **Diagnose der Funkstrecke** — Sie sehen, ob Frames ankommen oder zerstört werden.
3. **HA-Automatisierungen auf Fernbedienungstasten** — auch auf Tasten, die bei Ihnen nichts steuern.

Ein eigenes Funkmodul belegt der Sniffer nicht: Er abonniert den Hub `cc2500_emotion` und erhält von
dort bereits geprüfte Frames. Sie können ihn deshalb in der Produktivkonfiguration neben dem Licht
behalten — er protokolliert nur und beeinflusst die Steuerung der Leuchte nicht.

## Die ID der eigenen Fernbedienung ermitteln

Die fertige Konfiguration ist [`sniffer.yaml`](sniffer.yaml). `remote_id` ist darin absichtlich nicht
gesetzt, das Gerät hört also nur zu und sendet nie (die ID zum Senden kennen Sie noch nicht):

```bash
./.venv/bin/esphome run sniffer.yaml --device /dev/cu.usbserial-0001
```

Drücken Sie Tasten auf der Fernbedienung neben dem Board. Jeder erkannte Frame erzeugt eine Zeile:

```
[I][cc2500_emotion_sniffer]: remote 68049F25  cmd=0x01 (on)  val=0x80  RSSI=-38 dBm  LQI=2  offset=+3 kHz
[I][cc2500_emotion_sniffer]: remote 68ABF2    cmd=0x01 (on)  val=0x7D  RSSI=-65 dBm  LQI=13 offset=-4 kHz
```

Ihre Fernbedienung ist die mit deutlich höherem `RSSI`: Sie halten sie in der Hand. Tragen Sie die
gefundene ID in `remote_id` der Produktivkonfiguration ein.

Die erste Zeile zeigt die 14-Byte-Broadcast-Variante mit achtstelliger ID. Die zweite zeigt die
adressierte 8-Byte-Variante mit sechsstelliger Anforderungsroute: `68`, Fernbedienungsadresse,
Treiberadresse. Der Treiber vertauscht in der Antwort die beiden Endpunkte; die Komponente
normalisiert sie vor dem Loggen. Kopieren Sie daher genau die angezeigten sechs Zeichen.

### Wenn ein älterer Build die Länge `0x08` verwirft

Die Zwei-Tasten-Fernbedienung des **L&S Emotion Driver ED 12/30-1** nutzt die kompakte
8-Byte-Variante. Ein kurzer Druck rechts schaltet ein/aus, Halten regelt die Helligkeit; die linke
Taste ändert die Farbtemperatur. Ältere Versionen kannten nur 14-Byte-Frames und meldeten:

```
Frame rejected: length byte 0x08, expected 0x0E
```

Das ist ein echter Emotion-Frame, keine Störung. Aktualisieren Sie auf eine Version mit Unterstützung
des kompakten Protokolls; der Sniffer zeigt dann eine sechsstellige Route für `remote_id`.

**Die ID muss nicht geraten werden, und zufälliges Rauschen kann keine vortäuschen.** Frames werden
per Hardware-CRC und über eine Prüfsumme geprüft, die **aus den empfangenen ID-Bytes selbst** berechnet
wird — deshalb validiert der Sniffer Frames beliebiger Fernbedienungen, nicht nur bereits bekannter.
Entweder es passt alles zusammen, oder der Frame wird verworfen.

Unbekannte Befehle werden samt vollständigem 8- oder 14-Byte-Frame ausgegeben. Bei 14 Byte können
die sechs „reservierten“ Bytes ungleich null sein. Das ist auch der Modus zum Erweitern der
Befehlstabelle: Kann Ihre Fernbedienung etwas, das in der
[Befehlstabelle](CONFIGURATION.de.md#protokollbefehle) fehlt, zeigt das Log Code und Bytes.

## Optionen

```yaml
cc2500_emotion_sniffer:
  id: sniffer
  cc2500_emotion_id: emotion
  dump_raw: false
```

| Option | Standard | Beschreibung |
|---|---|---|
| `cc2500_emotion_id` | (auto) | Verweis auf den Hub `cc2500_emotion`. Der Empfang (`receive`) wird zwingend eingeschaltet — ein Zuhörer ohne Empfang wäre sinnlos |
| `dump_raw` | `false` | Alle 8 oder 14 Frame-Bytes für **jeden** Befehl ausgeben. Unbekannte werden ohnehin immer vollständig ausgegeben |
| `on_frame` | — | Automatisierung auf einen empfangenen Frame, **einmal pro Tastendruck** — siehe unten |
| `id` | (auto) | Kennung, auf die die `text_sensor`-Plattform verweist |

## Die Liste gefundener Fernbedienungen in Home Assistant

Steckt der Sniffer in einer Konfiguration mit WLAN/API, sind gefundene Fernbedienungen nicht nur im
Log sichtbar, sondern auch in Home Assistant — über eine Diagnose-Entität:

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

| Option | Beschreibung |
|---|---|
| `cc2500_emotion_sniffer_id` | Verweis auf `cc2500_emotion_sniffer` |
| `discovered_remotes` | Entität mit der Liste gefundener Fernbedienungen (Standardschema `text_sensor`) |

Der Zustand umfasst alle seit dem Start gehörten Fernbedienungen, jede mit ihrem **besten** Signalwert:

```
6804F74D -49dBm, 68049F25 -71dBm
```

Drücken Sie eine Taste auf der nächsten Fernbedienung, erscheint sie in der Liste. Die eigene erkennen
Sie an der Signalstärke. Bewusst wird der beste und nicht der letzte Wert behalten: So konvergiert der
Wert und die Entität hört auf zu aktualisieren, anstatt bei jedem Frame zu zappeln — eine gehaltene
Taste erzeugt Dutzende Frames pro Sekunde.

⚠️ **Entitäten entstehen in ESPHome zur Kompilierzeit**, eine Entität pro im Funk entdeckter
Fernbedienung ist daher technisch unmöglich — `esp32_ble_tracker` ist genauso gebaut (in die
Oberfläche gelangen nur vorab im YAML eingetragene Adressen, alles andere lebt im Log). Daher auch das
Limit: Die Liste hält bis zu **8** Fernbedienungen, danach werden keine neuen mehr aufgenommen — im Log
sind aber weiterhin alle zu sehen.

## Der Trigger `on_frame`

Er löst bei jedem empfangenen Frame jeder Fernbedienung in Reichweite aus — **einmal pro Tastendruck**
(Wiederholungen einer Serie werden nicht dupliziert). Darauf bauen HA-Automatisierungen und die
Live-Funktabelle auf.

### Argumente

| Variable | Typ | Bedeutung |
|---|---|---|
| `remote_id` | `std::string` | Fernbedienungs-ID/-Route, 6 oder 8 Hex-Zeichen (`"68ABF2"` oder `"6804F74D"`) |
| `command` | `uint8_t` | Befehlsbyte |
| `command_name` | `std::string` | Aufgelöster Befehlsname oder `"UNKNOWN"` |
| `value` | `uint8_t` | Wertebyte |
| `rssi` | `int` | Signalstärke in dBm |
| `lqi` | `uint8_t` | Zähler der Demodulationsfehler (0 = perfekt) |

⚠️ **`on_frame` löst für jede Emotion-Fernbedienung in Reichweite aus**, auch für die der Nachbarn.
Wollen Sie nur die eigenen, filtern Sie in der Bedingung der Automatisierung nach `remote_id`.

**Warum der Trigger dedupliziert ist, das Log aber nicht.** Das Log sieht jede Kopie eines Frames (das
ist der Indikator für die Verbindungsqualität), der Trigger dagegen erzeugt Ereignisse: Eine gehaltene
Taste liefert Dutzende Frames pro Sekunde, und ebenso viele Ereignisse würden die Home-Assistant-API
lahmlegen. Deshalb gilt: ein Tastendruck, eine Auslösung.

### Eine Live-Funktabelle in Home Assistant

Wer hat was wann und mit welchem Signal gedrückt — auf einem Dashboard.

Schritt 1 — ein Ereignis auf den HA-Bus senden (in [`example.yaml`](example.yaml) bereits enthalten):

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

Schritt 2 — die Ereignisse auf Home-Assistant-Seite in Entitätsattributen sammeln
(`configuration.yaml`). Anders als ein `text_sensor` haben Attribute kein Limit von 255 Zeichen, die
Tabelle kann also vollständig sein:

```yaml
template:
  - trigger:
      - platform: event
        event_type: esphome.emotion_frame
    sensor:
      - name: "Emotion Funk"
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

Schritt 3 — eine Dashboard-Karte:

```yaml
type: markdown
content: |
  | Fernbedienung | Befehl | Wert | RSSI | Gehört |
  |---|---|---|---|---|
  {% for id, r in state_attr('sensor.emotion_funk', 'remotes').items() -%}
  | `{{ id }}` | {{ r.command }} | {{ r.value }} | {{ r.rssi }} dBm | {{ r.last_seen }} |
  {% endfor %}
```

### Eine Automatisierung auf eine Fernbedienungstaste

Derselbe Trigger taugt für gewöhnliche Automatisierungen — etwa um eine HA-Szene an eine
Fernbedienungstaste zu hängen, die keine Ihrer Leuchten steuert:

```yaml
cc2500_emotion_sniffer:
  id: sniffer
  cc2500_emotion_id: emotion
  on_frame:
    # Die „Ein“-Taste einer bestimmten Fernbedienung → ein eigenes Ereignis in HA
    - if:
        condition:
          lambda: 'return remote_id == "6804F74D" && command == 0x01;'
        then:
          - homeassistant.event:
              event: esphome.emotion_button_on
```

Die Trigger-Argumente stehen in allen Lambdas innerhalb von `on_frame` zur Verfügung, auch in
Bedingungen.

## Wenn der Empfang unzuverlässig ist

Die Fernbedienung wiederholt jeden Tastendruck als Serie von etwa **5 identischen Frames** — das ist
der protokolleigene Schutz gegen Verluste. Der Sniffer gibt **jede** Kopie aus, die Zahl der Zeilen pro
Tastendruck ist deshalb ein direktes Maß für die Verbindungsqualität:

| Zeilen pro Tastendruck | Bedeutung |
|---|---|
| 4–5 | Gesunde Verbindung |
| 1–2 | Die Hälfte der Kopien geht verloren — funktioniert, aber grenzwertig |
| 0 | Der Frame wird überhaupt nicht erkannt |

Achten Sie in der Log-Zeile auf `LQI` (Zähler der Demodulationsfehler, **0 = perfekt**) und auf
`offset` (geschätzte Trägerabweichung).

| Symptom | Ursache |
|---|---|
| Viele Zeilen `Frame rejected: CRC failed` | Das Signal wird empfangen, die Frames sind aber beschädigt — schwaches Signal oder Störung |
| Keine Verwerfungen, aber kaum Frames | Der Empfänger erkennt die Sendung nicht: Antenne, Abstand, Abschirmung |
| `offset` konstant groß in eine Richtung | Quarzabweichung zwischen Fernbedienung und Modul |
| `offset` springt ±68 kHz in beide Richtungen | Das ist Rauschen der Schätzung an der Kompensationsgrenze, keine echte Abweichung — es gibt nichts zu korrigieren |
| `RX FIFO overflow` und `length byte 0x…` ohne Tastendruck | Falsche Sync-Erkennung auf WLAN-Rauschen (wir sind mitten in 2,4 GHz). Für die Daten harmlos, kostet aber Empfangszeit; über `PKTLEN` begrenzt |
| Eine Fernbedienung liest schlechter als eine andere | Prüfen Sie deren Batterie — eine schwache erzeugt genau „kommt manchmal durch“. Manchmal hilft es schon, die Batterie neu einzusetzen |

⚠️ Versuchen Sie nicht, einen springenden `offset` durch Schreiben in `FSCTRL0` zu „reparieren“:
Springt die Schätzung, ist sie Rauschen, und eine Kompensation würde eine zufällige Verstimmung
einbringen und den Empfang verschlechtern. Das wurde nachgemessen.

## Wie es intern funktioniert

Nützlich, wenn Sie den Code ändern wollen:

- Es gibt nur ein Funkmodul. Der Sniffer abonniert den Hub über `register_frame_listener()` und
  erzwingt `receive`.
- Der Hub liest **alle** vollständigen Frames auf einmal aus dem FIFO, nicht nur einen: Eine Serie von
  5 Kopien ist genau das, was das Protokoll robust macht — 4 von 5 wegzuwerfen ist keine Option.
- Reihenfolge der Frame-Verarbeitung: Länge → CRC → Typbyte und Prüfsumme (berechnet aus den
  empfangenen ID-Bytes, daher für jede Fernbedienung gültig) → Deduplizierung (Fenster ~150 ms über
  ID+cmd+val) → **der Zuhörer des Sniffers** → Filter auf die eigene `remote_id` → Aktion am Licht.
- Der ID-Filter sitzt **nach** dem Zuhörer — sonst würde der Sniffer fremde Fernbedienungen nie sehen,
  könnte also die gesuchte ID nicht anzeigen.
- Der Zuhörer wird vor der Deduplizierung aufgerufen und erhält ein `repeat`-Flag: Das Log gibt jede
  Kopie aus, der Trigger `on_frame` löst nur für die erste aus.

Mehr Details — [CLAUDE.md](CLAUDE.md).
