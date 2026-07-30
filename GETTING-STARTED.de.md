[![en](https://img.shields.io/badge/lang-en-blue.svg)](GETTING-STARTED.md)
[![ru](https://img.shields.io/badge/lang-ru-green.svg)](GETTING-STARTED.ru.md)
[![de](https://img.shields.io/badge/lang-de-yellow.svg)](GETTING-STARTED.de.md)

# Erste Schritte: eine L&S-Leuchte in Home Assistant

Diese Anleitung führt von „ESP32 gekauft“ bis „die Leuchte wird aus Home Assistant gesteuert“.
ESPHome-Erfahrung ist nicht nötig, ein Lötkolben allerdings schon: Die Leitungen zum CC2500-Modul
müssen gelötet werden (Details in Schritt 0). Rechnen Sie mit etwa einer Stunde, davon geht der
größte Teil für den ersten Firmware-Build drauf.

Die Idee ist einfach: **Der ESP32 gibt sich als Ihre Fernbedienung aus.** Er sendet dieselben Befehle
mit derselben Kennung, deshalb gehorcht die Leuchte ihm sofort — ohne Kopplung und ohne Eingriff in
die Leuchte. Der erste inhaltliche Schritt ist daher, die ID Ihrer Fernbedienung zu ermitteln.

## Schritt 0. Hardware aufbauen

Sie brauchen:

| Was | Anforderungen | Anmerkung |
|---|---|---|
| **ESP32** | Beliebiges Board mit USB | Verifiziert mit einem DevKit V1 (ESP-WROOM-32) |
| **CC2500-Modul** | 2,4 GHz, Quarz **26,000 MHz** | [Dieses von AliExpress](https://de.aliexpress.com/item/1005005501196142.html) ist verifiziert. Verkauft als „CC2500 module 2.4G“ |
| **Lötkolben** | Gewöhnlich, mit nicht zu grober Spitze | Die Leitungen werden an das Modul gelötet, einen Steckverbinder gibt es nicht |
| Leitungen | 6 Stück, ein Ende als Buchse für die ESP32-Stiftleiste | Das andere Ende wird an das Modul gelötet |

⚠️ **Das Modul muss gelötet werden.** Alle auffindbaren CC2500-Module (auch das oben verlinkte) kommen
ohne Stiftleiste: neun flache Pads an der Unterkante einer Platine von 12 × 19 mm im Raster
**1,27 mm**. Eine Stiftleiste mit 2,54 mm passt in dieses Raster nicht, und ein Adapter liegt nie bei
— die Leitungen gehen also direkt auf die Pads. Es sind nur sechs Lötstellen (`SI`, `SCLK`, `SO`,
`CSn`, `VDD`, `VSS`), aber sie sind klein: Nehmen Sie dünne Litze (PTFE-isolierten Schaltdraht oder
eine einzelne Ader aus einem Flachbandkabel) und fixieren Sie die Platine beim Löten. Die Zeichnung mit
der Pad-Nummerierung ist
[`datasheets/CC2500_module_photo.jpeg`](datasheets/CC2500_module_photo.jpeg).

Prüfen Sie danach benachbarte Pads auf Kurzschlüsse: In diesem Raster ist eine Lötbrücke zwischen
`SI`/`SCLK` oder `CSn`/`XO` der wahrscheinlichste Aufbaufehler.

⚠️ **Verwechseln Sie CC2500 nicht mit CC1101** — sie sehen fast gleich aus, aber der CC1101 arbeitet
auf 433/868 MHz und wird mit dieser Leuchte niemals sprechen. Prüfen Sie die Beschriftung auf dem Chip
selbst.

⚠️ **Der Quarz muss 26 MHz haben.** Die HF-Register wurden von der Fernbedienung ausgelesen und werden
unverändert übernommen; mit einem 27-MHz-Quarz verschieben sich Frequenz und Datenrate, und die
Leuchte hört Sie nicht. Der Quarz ist auf dem Modul beschriftet.

Verdrahten Sie wie folgt (Beispielbelegung — jeder Pin ist im YAML änderbar):

| CC2500-Modul | Signal | ESP32 |
|---|---|---|
| SI (2) | MOSI | GPIO23 |
| SCLK (3) | SCLK | GPIO18 |
| SO (4) | MISO | GPIO19 |
| CSn (8) | CS | GPIO5 |
| GDO0 (7) | Sendeende | GPIO4 |
| VDD (1) | Versorgung | **3V3** |
| VSS (6) | Masse | GND |

- **Versorgung ausschließlich vom Pin `3V3`.** 5 V (`VIN`) zerstören das Modul.
- Lassen Sie `GDO2` unbeschaltet — er wird nicht verwendet.
- `GDO0` ist optional: Ohne ihn wird das Sendeende durch Abfragen des Chips über SPI erkannt.
  Schließen Sie ihn an, wenn ein Pin frei ist — das ist etwas genauer.
- Die Pad-Nummerierung steht in
  [`datasheets/CC2500_module_pinout.md`](datasheets/CC2500_module_pinout.md); daneben liegen die
  Platinenzeichnung mit Nummern und der Applikationsschaltplan.

## Schritt 1. ESPHome installieren

ESPHome verwandelt eine YAML-Konfiguration in Firmware. Installieren Sie es in ein eigenes venv, um
das System sauber zu halten:

```bash
git clone https://github.com/SergeyZh/emotion-control-ha
cd emotion-control-ha
python3 -m venv .venv
./.venv/bin/pip install esphome
```

Ab hier läuft jeder Befehl über `./.venv/bin/esphome`. Ermitteln Sie den USB-Port des Boards:

```bash
ls /dev/cu.usbserial*      # macOS
ls /dev/ttyUSB*            # Linux
```

Setzen Sie den gefundenen Port in den Beispielen unten anstelle von `/dev/cu.usbserial-0001` ein.

## Schritt 2. Die ID der Fernbedienung ermitteln

`remote_id` sind die vier Bytes, mit denen Ihre Fernbedienung jede Sendung kennzeichnet; die Leuchte
hat sie sich bei der Kopplung im Werk oder beim Monteur gemerkt. Jede Fernbedienung hat ihre eigenen,
deshalb **gibt es keinen Standardwert** — die ID muss aus dem Funk mitgehört werden. Das erledigt
dasselbe CC2500-Modul, weitere Hardware ist nicht nötig.

Flashen Sie die Sniffer-Konfiguration. `remote_id` ist darin absichtlich nicht gesetzt, das Gerät
hört also nur zu und sendet nie:

```bash
./.venv/bin/esphome run sniffer.yaml --device /dev/cu.usbserial-0001
```

Warten Sie im Log auf das Initialisierungsbanner:

```
CC2500 PARTNUM=0x80 VERSION=0x03
Radio check: SYNC1=0xD3 MDMCFG2=0x73 CHANNR=0x10
Radio configured and calibrated.
```

Erscheinen diese Zeilen nicht, springen Sie zu [Fehlersuche](#fehlersuche): Weiterzumachen ist
sinnlos, solange das Funkmodul nicht antwortet.

Nun **drücken Sie Tasten auf der Fernbedienung**, während Sie sie neben das Board halten. Jeder
Tastendruck erzeugt mehrere Zeilen im Log:

```
[I][cc2500_emotion_sniffer]: remote 68049F25  cmd=0x01 (on)  val=0x80  RSSI=-38 dBm  LQI=2  offset=+3 kHz
```

`68049F25` ist die gesuchte ID. **Notieren Sie sie.**

Der Sniffer sieht jede Emotion-Fernbedienung in Empfangsreichweite, nicht nur Ihre (auch Nachbarn
können solche Leuchten haben). Die eigene erkennen Sie am `RSSI`: Sie halten sie in der Hand, ihr
Signal ist deutlich stärker — typisch −30…−50 dBm gegenüber −70 oder schlechter bei fremden. Zur
Sicherheit drücken Sie mehrmals und prüfen, ob immer dieselbe ID erscheint.

Mehr zum Sniffer, zu `LQI`, zu fremden Fernbedienungen und zur Liste gefundener IDs direkt in Home
Assistant — [SNIFFER.de.md](SNIFFER.de.md).

## Schritt 3. WLAN und ID eintragen

Die Produktivkonfiguration [`example.yaml`](example.yaml) holt Zugangsdaten aus `secrets.yaml` — einer
Datei, die nie in git landet. Legen Sie sie daneben an:

```yaml
# secrets.yaml
wifi_ssid: "IhrNetzwerk"
wifi_password: "Passwort"
emotion_remote_id: "68049F25"                 # die ID aus Schritt 2
```

Die Verbindung zu Home Assistant ist standardmäßig unverschlüsselt. Um sie zu verschlüsseln, erzeugen
Sie einen Schlüssel:

```bash
./.venv/bin/python -c "import os,base64; print(base64.b64encode(os.urandom(32)).decode())"
```

legen ihn als `emotion_control__encryption_key` in `secrets.yaml` ab und ergänzen den Block `api:` in
`example.yaml`:

```yaml
api:
  encryption:
    key: !secret emotion_control__encryption_key
```

## Schritt 4. Produktivkonfiguration flashen

```bash
./.venv/bin/esphome run example.yaml --device /dev/cu.usbserial-0001
```

Das erste Mal muss per USB erfolgen. Danach aktualisiert sich das Board über Funk (OTA), und
`--device` ist nicht mehr nötig.

Hängt das Flashen bei `Connecting...`, halten Sie die **BOOT**-Taste auf dem Board gedrückt, bis der
Upload beginnt.

Im Log sollte erscheinen:

```
CC2500 PARTNUM=0x80 VERSION=0x03
Radio configured and calibrated.
Listening for remote commands (RX enabled, MARCSTATE=0x0D).
```

## Schritt 5. In Home Assistant einbinden

Das Gerät meldet sich selbst: **Einstellungen → Geräte & Dienste**, dort schlägt ESPHome das gefundene
`emotion-control` vor — klicken Sie auf „Konfigurieren“. Haben Sie die Verschlüsselung aktiviert,
fügen Sie auf Anfrage `emotion_control__encryption_key` aus `secrets.yaml` ein. (Nicht gefunden? „Integration
hinzufügen“ → ESPHome → Host `emotion-control.local`.)

Sie erhalten diese Entitäten:

- **Licht „L&S Emotion“** — ein/aus, Helligkeit, Farbtemperatur.
- **Schaltflächen** „Pair“, „Brighter“, „Dimmer“, „Raw send“.
- **Textfelder** „Raw cmd (hex)“ / „Raw value (hex)“ — für Protokollexperimente.
- **„Discovered remotes“** — eine Diagnoseliste der im Funk gehörten Fernbedienungen.

## Schritt 6. Prüfen

In dieser Reihenfolge, damit Sie wissen, wo es klemmt, falls es klemmt:

1. **Schalten Sie die Leuchte aus Home Assistant ein.** Sie leuchtet → das Senden funktioniert und die
   ID stimmt. Sie leuchtet nicht → die Leuchte hört Sie nicht: ID und Abstand prüfen (für den ersten
   Test 2–3 Meter von der Leuchte entfernt stehen).
2. **Bewegen Sie die Farbtemperatur.** Sie ändert sich weich — das ist ein absoluter Befehl, der
   zuverlässigste.
3. **Bewegen Sie die Helligkeit.** Der Schieberegler rastet über fünf Stufen: 1 / 25 / 50 / 75 / 100 %.
   Das ist Absicht, die Leuchte hat genau fünf.
4. **Drücken Sie eine Taste auf der physischen Fernbedienung.** Der Zustand in Home Assistant muss sich
   von selbst ändern — dann funktioniert der Empfang, und HA driftet nicht von der Leuchte weg.

Läuft? Fertig. Optional weiter: [CONFIGURATION.de.md](CONFIGURATION.de.md) (alle Optionen, Entitäten,
Protokollbefehle) und [SNIFFER.de.md](SNIFFER.de.md) (HA-Automatisierungen auf Fernbedienungstasten).

## Fehlersuche

### `PARTNUM`/`VERSION` erscheinen nicht oder lesen sich als `0x00`/`0xFF`
Das Funkmodul antwortet nicht über SPI. Prüfen Sie der Reihe nach:
- **Versorgung von `3V3`**, nicht von `VIN`. Und dass die Masse gemeinsam ist.
- **MOSI/MISO nicht vertauscht** — der Klassiker. `SI` des Moduls geht an MOSI (GPIO23), `SO` an MISO
  (GPIO19).
- **`CSn` angeschlossen** an genau den Pin, der in `cs_pin` steht.
- **Die Lötstellen:** Messen Sie jedes Pad zum ESP32-Pin durch (kalte Lötstelle?) und benachbarte Pads
  gegeneinander (Lötbrücke?). Im Raster 1,27 mm sind das die beiden häufigsten Fehler.
- **Keine Leitung am Pad abgebrochen** — dünne Litze bricht auf so einer Platine beim Biegen, und der
  Bruch ist oft unsichtbar. Ziehen Sie sanft an jeder Leitung.

### Das Funkmodul initialisiert, aber die Leuchte reagiert nicht
- **Prüfen Sie die ID** — die häufigste Ursache. `sniffer.yaml` erneut flashen, Taste drücken und Byte
  für Byte vergleichen.
- **Ist `remote_id` überhaupt gesetzt?** Ohne sie verweigert die Komponente das Senden und schreibt
  einen Fehler ins Log — eine solche Konfiguration hört nur zu.
- **Abstand.** Module ohne brauchbare Antenne reichen manchmal nur wenige Meter. Gehen Sie näher.
- **Schließen Sie die Leuchte als Ursache aus:** Drücken Sie die echte Fernbedienung — reagiert sie?

### Die Fernbedienung erscheint nicht im Sniffer
- Halten Sie die Fernbedienung direkt an das Board und drücken Sie **mehrmals** — eine einzelne
  Sendung kann verloren gehen.
- **Wechseln Sie die Batterie der Fernbedienung.** Eine schwache erzeugt genau das Verhalten „kommt
  manchmal durch“: Die Leuchte gehorcht noch, Ihr Empfänger hört sie schon nicht mehr.
- Die Empfangsdiagnose (`LQI`, `CRC failed`, `RX FIFO overflow`) steht in
  [SNIFFER.de.md](SNIFFER.de.md#wenn-der-empfang-unzuverlässig-ist).

### Das Board lässt sich nicht flashen
- Halten Sie **BOOT** während des Verbindens gedrückt.
- Prüfen Sie den Port: `ls /dev/cu.usbserial*`. Manche Boards brauchen einen CH340/CP2102-Treiber.
- Ein anderes Programm belegt den Port (offener serieller Monitor, Arduino IDE) — schließen Sie es.

### Der Zustand in Home Assistant weicht von der Leuchte ab
Das kommt vor: Es gibt keine Rückmeldung von der Leuchte, Home Assistant zeigt eine Schätzung. Es
richtet sich bei der nächsten Aktion von selbst ein — oder sofort, wenn Sie eine Taste auf der
physischen Fernbedienung drücken (der Empfang zieht den Zustand nach). Details in
[CONFIGURATION.de.md](CONFIGURATION.de.md#eigenschaften-und-einschränkungen).

## Alternative: Kopplung statt ID-Klonen

Das Klonen der Fernbedienungs-ID ist der empfohlene Weg: Die Leuchte gehorcht sofort, und
Tastendrücke sind in Home Assistant sichtbar. Es geht aber auch umgekehrt — geben Sie dem Gerät eine
**eigene** ID und koppeln Sie es mit der Leuchte:

1. Wählen Sie eine beliebige ID (4 Byte hex) und tragen Sie sie in `remote_id` ein.
2. Versetzen Sie die Leuchte in den Lernmodus (laut L&S-Anleitung — meist eine gehaltene Taste am
   Empfänger).
3. Drücken Sie in Home Assistant die Schaltfläche **„Pair“** — das Gerät verkündet seine ID, die
   Leuchte merkt sie sich.

⚠️ Mit eigener ID **funktioniert die Synchronisierung mit der physischen Fernbedienung nicht**: Das
Gerät hört nur auf die in `remote_id` eingetragene ID, die Fernbedienung sendet aber ihre eigene. Wenn
Ihnen Tastendrücke in Home Assistant wichtig sind, klonen Sie deren ID.

## Laborbetrieb (ohne WLAN)

Um Funk und Senden ohne Home Assistant zu testen, gibt es [`bench.yaml`](bench.yaml): Logs über USB,
kein WLAN nötig, und die **BOOT**-Taste auf dem Board schaltet die Leuchte ein und aus. Die ID kommt
aus `secrets.yaml` (`emotion_remote_id`). Praktisch, um Funkprobleme von Netzwerkproblemen zu trennen:

```bash
./.venv/bin/esphome run bench.yaml --device /dev/cu.usbserial-0001
```
