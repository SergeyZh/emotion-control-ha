[![en](https://img.shields.io/badge/lang-en-blue.svg)](PLAN.md)
[![ru](https://img.shields.io/badge/lang-ru-green.svg)](PLAN.ru.md)
[![de](https://img.shields.io/badge/lang-de-yellow.svg)](PLAN.de.md)

# Plan: L&S Emotion Control Leuchten aus ESPHome steuern

Ein ESPHome-`light`-Gerät, das L&S-Leuchten über das Funkprotokoll **Emotion Control** steuert, mittels
eines **CC2500**-Moduls (2,4 GHz), das per SPI am ESP32 hängt. Das Protokoll stammt aus dem Reverse
Engineering in [`emotion-control-re`](https://github.com/SergeyZh/emotion-control-re) (vollständig
entschlüsselt; HF-Parameter und Paketformat durch Senden an eine echte Leuchte bestätigt).

## Ergebnis
Ein ESPHome-Licht mit:
- Ein/Aus,
- Steuerung der Farbtemperatur,
- Steuerung der Helligkeit (gestuft — siehe „Steuerungsmodell“).

## Hardware
- **CC2500-Modul** (2,4 GHz; **nicht** CC1101, das arbeitet unter 1 GHz), Quarz **26,000 MHz**
  (bestätigt — die Registerkarte aus dem Reverse-Engineering-Repo wird unverändert übernommen, ohne
  HF-Neuberechnung). Pinbelegung des Moduls —
  [`datasheets/CC2500_module_pinout.md`](datasheets/CC2500_module_pinout.md).
- **ESP32**, SPI-Bus.

### Verdrahtung (ESP32 ↔ CC2500-Modul)
| Modul | Signal | ESP32 (Beispiel, VSPI) |
|---|---|---|
| SI (2) | MOSI | GPIO23 |
| SCLK (3) | SCLK | GPIO18 |
| SO (4) | MISO | GPIO19 |
| CSn (8) | CS | GPIO5 |
| GDO0 (7) | Sendeende (opt.) | GPIO4 |
| VDD (1) | 3,3 V | 3V3 |
| VSS (6) | GND | GND |

GDO2 wird nicht verwendet. GDO0 ist optional: Das Sendeende lässt sich darüber, durch Abfragen von
`MARCSTATE` oder mit einer festen Verzögerung erkennen.

## Steuerungsmodell
- **Ein normales `light` vom Typ `COLOR_TEMPERATURE`:** ein/aus + Helligkeit + Farbtemperatur, ohne
  Krücken — wie eine gewöhnliche Lampe in Home Assistant.
- Ein/aus (`0x01`/`0x02`) und Farbtemperatur (`0x50`) sind absolute Befehle und passen 1:1.
- **Helligkeit als absoluter Regler über 5 Stufen.** Wir verfolgen die aktuelle Stufe und schritten bei
  einer Änderung die Differenz (`0x11`/`0x21`), ohne Flackern; die Endanschläge werden über Schritte mit
  Reserve erreicht, was gleichzeitig als Resynchronisierungspunkt dient. Die Stufenzahl ist
  `brightness_levels` (Standard 5).
- **Synchronisierung von der Fernbedienung (RX):** über Funk empfangene Befehle der physischen
  Fernbedienung aktualisieren den Zustand in Home Assistant (ein/aus, Farbtemperatur, Helligkeitsstufe),
  ohne zurückzusenden → HA und Leuchte laufen nicht auseinander.
- Zusätzlich: **„Pair“** (`0x60`) und **Raw** (beliebiger Befehl, aus HA editierbar) als
  `template button` im YAML.

## Protokoll (kurz; vollständig im Reverse-Engineering-Repo)
- HF: MSK, 250 kBaud, Träger 2436,2 MHz, Sync-Wort `0xD391`, FEC ein, Whitening aus, CRC per Hardware.
  All das erledigt der CC2500 per Hardware anhand der geladenen Registerkarte.
- Die Karte mit 45 Registern plus `PATABLE = FF FE BB A9 7F 6E 97 C5` stammt aus
  [`captures/PROTOCOL-FINDINGS.md`](https://github.com/SergeyZh/emotion-control-re/blob/main/captures/PROTOCOL-FINDINGS.md).
- Paket (im TX-FIFO): `0E | ID₀ ID₁ ID₂ ID₃ | cmd | val | 00×6 | chk | 16`, wobei `0E` die Länge ist
  (14). `chk = 0x7E ⊕ ID₀ ⊕ ID₁ ⊕ ID₂ ⊕ ID₃ ⊕ 0x16 ⊕ cmd ⊕ val` (die allgemeine Formel; die verkürzte
  `0xBE ⊕ cmd ⊕ val` aus dem Reverse-Engineering-Repo gilt nur für die ID `68 04 F7 4D`). Die
  Fernbedienungs-ID wird in der Konfiguration gesetzt; einen Standardwert gibt es nicht — die eigene ID
  ermitteln Nutzer mit dem Sniffer (Phase 8).
- Befehle: `0x01` ein (trägt die aktuelle Farbtemperatur), `0x02` aus, `0x11` heller (Schritt),
  `0x21` dunkler (Schritt), `0x50` Farbtemperatur (`0x00` warm … `0xFF` kalt), `0x60` Kopplung.
- Helligkeit und ihre Befehle (durch Empfang präzisiert, Phase 6):
  - **Gestuft — 5 Stufen:** `0x11` einen Schritt heller, `0x21` einen Schritt dunkler.
  - **Weich (Ramp):** `0x10` („heller“ gehalten) — **weiches ERHÖHEN starten**, `0x20` („dunkler“
    gehalten) — **weiches VERRINGERN starten**. Der Weg von min bis max dauert etwa **1 Sekunde**.
  - `0x40` (Taste losgelassen) — die weiche Änderung auf der aktuellen Stufe **stoppen**.
  - ⇒ `0x10`/`0x20` sind **kein** sofortiges max/min: Die tatsächliche Stufe hängt davon ab, wann das
    `0x40` eintrifft. Absolute Endanschläge erreicht man zuverlässiger über SCHRITTE (`0x11`/`0x21` mit
    Reserve — die Leuchte begrenzt selbst).
  - Helligkeitsbefehle sind Einzelsendungen (nicht ×5) und können verloren gehen.
- Senden: `SIDLE(0x36) → SFTX(0x3B) → burst write FIFO(0x7F) → STX(0x35)`, **×5 Wiederholungen**, danach
  `SPWD(0x39)`. Die Fernbedienung (und wir) sendet nur, einen RX-Zweig hat sie nicht.

## Phasen
1. **SPI-Prüfung.** ✅ Auf Hardware bestätigt (`PARTNUM=0x80 VERSION=0x03`).
2. **Funkinitialisierung.** ✅ Auf Hardware bestätigt (`SYNC1=0xD3 MDMCFG2=0x73 CHANNR=0x10`,
   „Radio configured and calibrated“).
3. **Senden.** ✅ An der Leuchte bestätigt. Payload-Aufbau, `chk` nach der allgemeinen Formel,
   TX-Zyklus ×5 (`SIDLE→SFTX→FIFO→STX`, Ende über GDO0/MARCSTATE erkannt), Chip bleibt in IDLE. Die
   `send_*`-Methoden.
4. **`light`-Plattform + Schaltflächen.** ✅ In Home Assistant bestätigt. `EmotionControlLight` (Typ
   COLOR_TEMPERATURE): `write_state()` → aus `0x02`, ein `0x01`+CT, CT `0x50` (Mired ↔ `0xFF` warm,
   invertiert). Kopplungs- und Raw-Schaltflächen als `template button`; Raw ist aus HA editierbar. Das
   Helligkeitsmodell wurde in Phase 6b überarbeitet (absolut, 5 Stufen).
5. **Kopplung und Live-Test.** ✅ Alle Befehle (ein/aus, Farbtemperatur, Helligkeit, Kopplung, Raw) an
   einer echten Leuchte geprüft.
6. **Empfang von Fernbedienungsbefehlen (RX) und Zustandssynchronisierung.**
   - **6a — Empfang.** ✅ Auf Hardware bestätigt. Der Chip bleibt in RX (RXOFF_MODE=RX); `loop()` fragt
     `RXBYTES` ab (zweimal lesen gegen den Zähler-Erratum), liest einen 17-Byte-Frame, dann Flush und
     RX-Neustart (Schutz gegen FIFO-Überlauf bei einer ×5-Serie). Validierung von
     CRC/Länge/`remote_id`/`chk`, Deduplizierung, Unterdrückung des eigenen TX-Echos. Dabei wurden auch
     die Helligkeitsbefehle gefunden (5 Stufen, `0x10`/`0x20` für die Enden, `0x40` zum Freigeben).
   - **6b — Synchronisierung + absolute Helligkeit.** ✅ In HA und an der Leuchte bestätigt. Ein
     empfangener Fernbedienungsbefehl → `EmotionControlLight::on_remote_command()` aktualisiert den
     Zustand über `make_call` mit unterdrücktem Senden. Absolute Helligkeit (5 Stufen). Präzisierungen
     unterwegs: `0x10`/`0x20` sind ein weicher Ramp (kein sofortiger Endanschlag, Schätzung über
     `ramp_duration`); Helligkeitsschritte ×1; Pause `step_interval` ≈ 200 ms zwischen Schritten;
     Schaltflächen senden immer; Deduplizierung als einheitliches Fenster von ~150 ms.
7. **README + Dokumentation.** ✅ Fertig. [`README.de.md`](README.de.md) (Überblick, Hardware,
   Schnellstart), [`GETTING-STARTED.de.md`](GETTING-STARTED.de.md) (Schritt für Schritt für neue
   Nutzer), [`CONFIGURATION.de.md`](CONFIGURATION.de.md) (Optionen, HA-Entitäten, Befehle,
   Einschränkungen), [`SNIFFER.de.md`](SNIFFER.de.md) (Fernbedienungen, `on_frame`, Empfangsdiagnose),
   [`CLAUDE.md`](CLAUDE.md) (Notizen für Agenten) und dieser Plan. Zustandserhalt über Neustarts —
   `restore_mode=RESTORE_DEFAULT_OFF`, beim Booten wird nicht gesendet.
8. **Sniffer: die Fernbedienungs-ID finden.** ✅ Eine eigene Komponente `cc2500_emotion_sniffer`, an den
   Hub als Eltern gebunden (das Funkmodul wird nicht dupliziert). Der Hub validiert einen Frame per CRC
   plus `chk`, berechnet aus den empfangenen ID-Bytes, und übergibt ihn dem `EmotionFrameListener`
   **vor** dem `remote_id`-Filter; Aktionen laufen weiterhin nur für die eigene ID. Die Deduplizierung
   wanderte über den Filter und wird über ID+cmd+val geschlüsselt. `remote_id` wurde optional und ohne
   Standardwert — ohne sie hört der Hub nur zu. Fertige Konfiguration — [`sniffer.yaml`](sniffer.yaml).
   Gefundene Fernbedienungen sind auch in Home Assistant sichtbar — über die `text_sensor`-Plattform des
   Sniffers (`discovered_remotes`, eine Liste mit dem besten RSSI jeder Fernbedienung).
9. **Trigger `on_frame`.** ✅ Automatisierung auf einen empfangenen Frame beliebiger Fernbedienungen,
   eine Auslösung pro Tastendruck (das `repeat`-Flag berechnet der Hub; eine gehaltene Taste bedeutet
   ~85 Frames/s, was als Ereignisse die HA-API überflutet hätte). Daraus entsteht eine Live-Funktabelle
   in HA — Rezept in [`SNIFFER.de.md`](SNIFFER.de.md). Ein eigenes Panel wie
   `/config/bluetooth/advertisement-monitor` ist mit ESPHome-Mitteln nicht möglich: Dieses registriert
   die eingebaute Integration `bluetooth`, in der ESPHome nur den Transport stellt
   (`bluetooth_proxy`).

## Status: funktional abgeschlossen
Alle angekündigten Funktionen laufen an einer echten Leuchte und in Home Assistant. Was bleibt, ist
optional: `ramp_duration`/`step_interval` für eine konkrete Leuchte kalibrieren, Aufräumen und
Refactoring, Veröffentlichung.

## Risiken (geschlossen)
- ✅ **FEC bei variabler Länge im EMPFANG durch den CC2500 per Hardware** — auf Hardware bestätigt
  (Phase 6a).
- ✅ **TX/RX-Halbduplex** — nach dem Senden geht es zurück in RX, das eigene Echo unterdrückt die
  Deduplizierung.
- ✅ **Drift der Helligkeitsstufe** — eine Baseline entsteht aus dem ersten Zustand; die Endanschläge und
  der Empfang von der Fernbedienung resynchronisieren sie; Schritte sind um `step_interval` versetzt,
  sodass die Leuchte sie unterscheidet.
