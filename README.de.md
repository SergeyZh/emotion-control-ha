[![en](https://img.shields.io/badge/lang-en-blue.svg)](README.md)
[![ru](https://img.shields.io/badge/lang-ru-green.svg)](README.ru.md)
[![de](https://img.shields.io/badge/lang-de-yellow.svg)](README.de.md)

# ESPHome ↔ L&S Emotion Control (CC2500)

Steuern Sie **L&S Emotion Control** Möbelleuchten aus **Home Assistant** — ohne Gateway des
Herstellers. Ein ESP32 mit einem **CC2500**-Funkmodul (2,4 GHz) gibt sich als Ihre Fernbedienung aus:
Er sendet dieselben Befehle und **hört den Funkverkehr mit**, sodass auch Tastendrücke auf der
physischen Fernbedienung in Home Assistant sichtbar werden.

Die Leuchte erscheint in Home Assistant als gewöhnliches Licht: ein/aus, Helligkeit, Farbtemperatur.

**Status: funktionsfähig**, an einer echten Leuchte verifiziert. Das Protokoll wurde in einem
separaten Repository entschlüsselt:
[`emotion-control-re`](https://github.com/SergeyZh/emotion-control-re).

Zwei Protokollvarianten werden unterstützt:

- das ursprüngliche 14-Byte-Broadcast-Protokoll mit einer vier Byte langen Fernbedienungs-ID;
- ein adressiertes 8-Byte-Protokoll, verifiziert mit einem **L&S Emotion Driver ED 12/30-1** und
  seiner Fernbedienung mit zwei Tasten (rechts: Ein/Aus und gehalten Helligkeit; links: Farbtemperatur).

## Funktionen
- **Ein/Aus** und **Farbtemperatur** (warm ↔ kalt).
- **Helligkeit** — als normaler Schieberegler in Home Assistant (native Schritte bei 14 Byte,
  kurze zeitgesteuerte Rampen bei 8 Byte).
- **Synchronisierung mit der Fernbedienung:** Bedienen Sie die physische Fernbedienung, folgt der
  Zustand in Home Assistant.
- **Zustand bleibt** über Neustarts des ESP hinweg erhalten.
- **Ermittlung der Fernbedienungs-ID** über Funk (Sniffer-Komponente) — der eigenen und aller anderen
  in Empfangsreichweite.
- **Kopplung** direkt mit der Leuchte bei der 14-Byte-Variante, falls Sie keine bestehende
  Fernbedienung klonen möchten. Für die 8-Byte-Variante ist die Kopplung noch nicht verifiziert.

## Was Sie brauchen
| Bauteil | Anforderungen |
|---|---|
| **ESP32** | Beliebiges Board. Verifiziert mit einem DevKit V1 (ESP-WROOM-32) |
| **CC2500-Modul** | Ausschließlich 2,4 GHz (**nicht** CC1101, das arbeitet unter 1 GHz), Quarz **26,000 MHz**. Verifiziert sind das [kleine Pad-Modul](https://de.aliexpress.com/item/1005005501196142.html) und dieses [TinyTronics-Modul mit Stiftleiste](https://www.tinytronics.nl/en/communication-and-signals/wireless/rf/modules/cc2500-wireless-rf-module-2.4ghz) |
| **Lötkolben** | Nur für ein Modul ohne montierte Stiftleiste erforderlich |
| **L&S-Fernbedienung** | Ihre vorhandene Fernbedienung; deren ID übernimmt der ESP32. Alternativ eine Leuchte im Kopplungsmodus |

Das kleine AliExpress-Modul hat neun Pads im Raster **1,27 mm**; sechs Leitungen müssen direkt
angelötet werden. Die verifizierte TinyTronics-Alternative besitzt bereits Stifte und lässt sich mit
Dupont-Buchsenleitungen ohne Lötarbeit an der Funkseite mit dem ESP32 verbinden. Die Pinreihenfolge
unterscheidet sich je nach Modulplatine: Richten Sie sich nach den Signalbezeichnungen (`SI`, `SCLK`,
`SO`, `CSn`, `GDO0`, `VCC`, `GND`), nicht nach den Pad-Nummern der Tabelle unten.

Versorgen Sie das Modul mit **3,3 V**, nicht mit 5 V. Pinbelegung, Platinenzeichnung und Schaltplan
liegen in [`datasheets/`](datasheets/CC2500_module_pinout.md).

### Verdrahtung (CC2500-Modul → ESP32, SPI)
| Modul | Signal | ESP32 (Beispiel, VSPI) |
|---|---|---|
| SI (2) | MOSI | GPIO23 |
| SCLK (3) | SCLK | GPIO18 |
| SO (4) | MISO | GPIO19 |
| CSn (8) | CS | GPIO5 |
| GDO0 (7) | Sendeende (opt.) | GPIO4 |
| VDD (1) / VSS (6) | 3V3 / GND | 3V3 / GND |

`GDO2` wird nicht verwendet. `GDO0` ist optional — ohne diesen Pin wird das Sendeende durch Abfragen
des Chips erkannt.

## Schnellstart
```bash
# 1. ESPHome (Beispiel: venv im Projektverzeichnis)
python3 -m venv .venv && ./.venv/bin/pip install esphome

# 2. ID der eigenen Fernbedienung ermitteln: Sniffer flashen und Tasten drücken
./.venv/bin/esphome run sniffer.yaml --device /dev/cu.usbserial-0001
#    → [I][cc2500_emotion_sniffer]: remote 68049F25  cmd=0x01 (on)  val=0x80  RSSI=-38 dBm  LQI=2 …

# 3. WLAN in secrets.yaml, die gefundene ID in remote_id in example.yaml
printf 'wifi_ssid: "IhrNetzwerk"\nwifi_password: "Passwort"\nemotion_remote_id: "68049F25"\n' > secrets.yaml

# 4. Produktivkonfiguration flashen (erstes Mal per USB, danach OTA)
./.venv/bin/esphome run example.yaml --device /dev/cu.usbserial-0001
```

Anzeichen, dass das Funkmodul lebt:
```
CC2500 PARTNUM=0x80 VERSION=0x03
Radio configured and calibrated.
Listening for remote commands (RX enabled, MARCSTATE=0x0D).
```

Schritt für Schritt und mit Erklärungen — **[GETTING-STARTED.de.md](GETTING-STARTED.de.md)**.

## Minimalkonfiguration
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
  remote_id: "68049F25"   # 4-Byte-ID oder 3-Byte-Route wie "68ABF2"

light:
  - platform: cc2500_emotion
    name: "L&S Emotion"
    cc2500_emotion_id: emotion
```
Alle Optionen sind in [CONFIGURATION.de.md](CONFIGURATION.de.md) dokumentiert. Fertige
Konfigurationen: [`example.yaml`](example.yaml) (produktiv, mit WLAN), [`bench.yaml`](bench.yaml)
(Laborbetrieb über USB), [`sniffer.yaml`](sniffer.yaml) (nur Empfang).

## Funktionsweise
Der CC2500 wird mit der von der Fernbedienung ausgelesenen Registerkarte konfiguriert (MSK,
250 kBaud, 2436,2 MHz, Sync-Wort `0xD391`, FEC, Hardware-CRC) — Modulation, Kodierung und die
Frame-CRC erledigt der Chip selbst. Die Software stellt entweder ein adressiertes 8-Byte-Payload oder
ein 14-Byte-Broadcast-Payload zusammen.

Der Chip bleibt im Empfang und kehrt nach jedem Senden dorthin zurück. Empfangene Frames werden
geprüft (CRC, Länge, Prüfsumme): Frames mit Ihrer `remote_id` aktualisieren den Zustand in Home
Assistant, **ohne etwas zurückzusenden**; alle übrigen erhält die Sniffer-Komponente — genau so wird
die ID einer Fernbedienung überhaupt gefunden.

## Einschränkungen
- **Die Rückmeldung hängt von der Variante ab.** Eine 14-Byte-Leuchte antwortet nicht, ihr HA-Zustand
  ist daher eine Schätzung. Der ED 12/30-1 antwortet auf adressierte 8-Byte-Befehle, veröffentlicht
  Zustandsänderungen aber nicht selbstständig; der Funkverkehr der Fernbedienung hält HA synchron.
- **Die Helligkeit wird als konfigurierbare Anzahl von Stufen modelliert** (standardmäßig fünf).
  Die 14-Byte-Variante nutzt native Schritte; bei 8 Byte wird jeder Schritt durch eine kurze
  zeitgesteuerte Rampe angenähert.
- **Eine gehaltene Taste** der Fernbedienung wird über die Zeit geschätzt und ist daher ungenau.
- **Nach einem Neustart des ESP** wird der Zustand wiederhergestellt, aber nicht an die Leuchte
  gesendet (sie kennt ihren eigenen) — wurde die Leuchte zwischenzeitlich per Fernbedienung
  verändert, zieht Home Assistant bei der ersten Aktion nach.

Die vollständige Liste — [CONFIGURATION.de.md](CONFIGURATION.de.md#eigenschaften-und-einschränkungen).

## Dokumentation
| Dokument | Inhalt |
|---|---|
| **[GETTING-STARTED.de.md](GETTING-STARTED.de.md)** | Schritt für Schritt von den Bauteilen bis zum Licht in Home Assistant |
| **[CONFIGURATION.de.md](CONFIGURATION.de.md)** | Alle YAML-Optionen, HA-Entitäten, Protokollbefehle, Einschränkungen |
| **[SNIFFER.de.md](SNIFFER.de.md)** | Fernbedienungen finden, Trigger `on_frame`, Funktabelle in HA, Empfangsdiagnose |
| [PLAN.de.md](PLAN.de.md) | Projektplan und Status der Entwicklungsphasen |
| [CLAUDE.md](CLAUDE.md) | Notizen für KI-Agenten: Fallstricke, Entscheidungen, Arbeitsablauf (nur Englisch) |
| [emotion-control-re](https://github.com/SergeyZh/emotion-control-re) | Reverse Engineering des Protokolls: HF-Parameter, Register, Frame-Format |

Die Dokumentation wird in **Englisch (Hauptsprache), Russisch und Deutsch** gepflegt — siehe die
Sprach-Badges am Anfang jeder Datei.

## Projektstruktur
| Pfad | Inhalt |
|---|---|
| `components/cc2500_emotion/` | CC2500-Treiber + `light`-Plattform |
| `components/cc2500_emotion_sniffer/` | Protokolliert jeden Emotion-Frame im Funk, findet Fernbedienungs-IDs |
| `example.yaml` | Produktivkonfiguration: WLAN/API/OTA, Licht, Schaltflächen, Sniffer |
| `bench.yaml` | Laborkonfiguration: Logs über USB, kein WLAN, die BOOT-Taste schaltet die Leuchte |
| `sniffer.yaml` | Nur Empfang — zum Ermitteln der Fernbedienungs-ID |
| `datasheets/` | Pinbelegung, Platinenzeichnung und Schaltplan des CC2500-Moduls + Link zum verifizierten Modul |

## Lizenz
[Apache License 2.0](LICENSE).

Dieses Projekt steht in keiner Verbindung zu L&S und wird von dort nicht unterstützt.
„Emotion Control“ ist eine Marke ihres Inhabers und wird hier ausschließlich zur Angabe der
Kompatibilität verwendet.
