# Kupferstab

Firmware und Handy-App für den leuchtenden Kupferstab: Ein Wemos D1 mini (ESP8266) steuert einen
NeoPixel-Strip, der durch Löcher in Kupferrohren leuchtet. Gesteuert wird er vom Handy aus, Updates
holt er sich selbst von GitHub.

## So funktioniert es auf der Party

1. **Hotspot am Handy einschalten**, dann den Stab. Er meldet sich innerhalb von ein paar Sekunden
   an. Der Strip zeigt dabei kurz die letzte Zahl seiner Adresse, grüne LEDs je Ziffer und eine
   blaue für 0.
2. Im Browser **http://kupferstab.local** öffnen. Geht das nicht (manche Android-Handys), nimm die
   Adresse aus „Einstellungen → Handy-Hotspot“. Steht sie dort noch nicht, ist es die
   Hotspot-Adresse mit der Zahl vom Strip am Ende.
3. Für das Gefühl einer echten App: im Browser auf „Teilen → Zum Home-Bildschirm“ (iPhone) bzw.
   „⋮ → Zum Startbildschirm hinzufügen“ (Android) tippen. Danach startet sie im Vollbild.

**Beim ersten Mal, oder wenn er den Hotspot nicht findet:** Der Stab öffnet nach etwa 20 Sekunden
sein eigenes WLAN **„Kupferstab“** (Passwort `kupferstab`). Verbinde dich, die App öffnet sich von
selbst (sonst **http://192.168.4.1**). Unter „Einstellungen → Handy-Hotspot“ trägst du Name und
Passwort deines Hotspots ein und tippst auf „Speichern“. Dann trennst du dich vom Stab und schaltest
den Hotspot ein. Der Stab sucht alle 30 Sekunden danach, meldet sich an und schließt sein eigenes
WLAN. Ist der Hotspot später wieder weg, öffnet er nach etwa 30 Sekunden wieder „Kupferstab“.

**Der Stab kann nur 2,4 GHz.** Auf dem iPhone unter „Persönlicher Hotspot“ den Schalter
„Kompatibilität maximieren“ einschalten. Bei Android steht das Band in den Hotspot-Einstellungen.

<img src="docs/screenshots/main-dark.png" alt="Die App: Farbrad, Helligkeit, Effekte, Szenen" width="260"> <img src="docs/screenshots/settings.png" alt="Einstellungen mit Pin-Suche und Update" width="260">

Der Stab braucht kein Internet, nur für Updates. Mehrere Handys können gleichzeitig steuern, wenn
sie im selben Hotspot sind, und sehen die Änderungen der anderen sofort.

**Bluetooth gibt es nicht:** Der ESP8266 hat keinen Bluetooth-Chip, und auf dem iPhone kann kein
Browser Bluetooth-Geräte ansprechen. Deshalb läuft alles über WLAN.

## Erstes Flashen (einmalig, per USB)

Das alte OTA ist nicht mehr erreichbar, deshalb geht der erste Schritt per Micro-USB am D1 mini.
Danach braucht es kein Kabel mehr.

### Vom MacBook aus, Schritt für Schritt

1. **Werkzeuge** im Terminal installieren (einmalig, mit [Homebrew](https://brew.sh)):
   `brew install platformio oven-sh/bun/bun gh`
2. **Repo holen:** `gh repo clone Niklas-Flaig/kupferstab && cd kupferstab`. Solange PR #3 offen
   ist, zusätzlich `git checkout firmware-pwa`.
3. **App bauen:** `bun install && bun run build`. Das Ende der Ausgabe lautet „App gebaut: index.html 7.2 KB gzip …“.
4. **D1 mini per Micro-USB anschließen.** Das Kabel muss Daten übertragen können, reine
   Ladekabel gehen nicht. `ls /dev/cu.*` zeigt dann einen Eintrag wie
   `/dev/cu.usbserial-1410` oder `/dev/cu.wchusbserial1410`. Fehlt er, liegt es am Kabel oder am
   [CH340-Treiber](https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html).
5. **Firmware schreiben:** `cd firmware && pio run -e d1_mini -t upload`. Die Ausgabe endet mit
   `[SUCCESS]`. Findet PlatformIO den Port nicht, hänge `--upload-port /dev/cu.usbserial-1410` an.
6. **App schreiben:** `pio run -e d1_mini -t uploadfs`, ebenfalls bis `[SUCCESS]`.
7. **Prüfen:** Nach dem Neustart läuft das Kupfer-Lauflicht. `pio device monitor` zeigt
   `Kupferstab 0.1.0: Wi-Fi "Kupferstab"`; beenden mit Ctrl+C.

### Weg A: mit PlatformIO (empfohlen)

```sh
# einmalig: PlatformIO und Bun installieren
uv tool install platformio        # oder: pipx install platformio
curl -fsSL https://bun.sh/install | bash

# App bauen, dann Firmware und App auf den D1 mini schreiben
bun install
bun run build
cd firmware
pio run -e d1_mini -t upload      # Firmware
pio run -e d1_mini -t uploadfs    # App (Dateisystem)
```

PlatformIO findet den USB-Port meist selbst. Wenn nicht, gib ihn mit `--upload-port` an:
`/dev/cu.usbserial-*` (Mac) bzw. `COM3` (Windows). Erkennt der Rechner den D1 mini gar nicht,
fehlt der Treiber für den USB-Chip **CH340**.

### Weg B: im Browser, ohne Installation

Mit Chrome oder Edge am Laptop:

1. https://espressif.github.io/esptool-js/ öffnen, auf **Connect** klicken und den D1 mini wählen.
2. Zwei Dateien eintragen: `firmware.bin` an Adresse **0x0**, `littlefs.bin` an **0x200000**.
3. **Program** klicken und danach den D1 mini kurz vom Strom trennen.

Die beiden Dateien entstehen mit `pio run -e d1_mini` und `pio run -e d1_mini -t buildfs` unter
`firmware/.pio/build/d1_mini/`.

### Danach

Beim Start läuft ein **Kupfer-Lauflicht** über den Strip. Läuft es über alle LEDs, stimmen Pin und
Anzahl. Wenn nicht, siehe [Fehlersuche](#fehlersuche).

## Updates

**Von GitHub, ohne Laptop:** Auf GitHub unter [Actions → Release](https://github.com/Niklas-Flaig/kupferstab/actions/workflows/release.yml)
auf **„Run workflow“** tippen. Die Action baut Firmware und App, signiert sie und veröffentlicht
sie als Release mit der nächsten Versionsnummer, nach etwa drei Minuten. Der Stab prüft das selbst:
nach jeder Anmeldung im Hotspot, alle 30 Minuten und wenn du in der App auf „Nach Updates suchen“
tippst. Ist eine neue Version da, steht in der App unter Einstellungen → Update der Knopf
**„Version … installieren“**. Danach startet der Stab neu. Dafür braucht der Hotspot Internet.

Der Stab spielt nur ein, was mit dem Release-Schlüssel signiert ist. Der private Schlüssel liegt als
Secret `SIGNING_KEY` im Repo und als Kopie unter `~/.config/kupferstab/signing.key` auf dem
MacBook. Der öffentliche steht in `firmware/src/signing_key.h`. Geht der Schlüssel verloren:
neues Paar erzeugen, `signing_key.h` und das Secret ersetzen, einmal lokal flashen.

**Über die App, ohne Internet:** Einstellungen → Update → Datei wählen. Die Dateien liegen an jedem
Release.

- `firmware.bin` aktualisiert die Firmware.
- `littlefs.bin` aktualisiert die App.

Der Stab startet danach neu. LED-Anzahl, Pin, WLAN und Szenen bleiben erhalten, auch bei einem
App-Update.

**Notfall-Seite:** `/update` (z. B. http://192.168.4.1/update) ist fest in die Firmware eingebaut. Sie funktioniert
also auch dann, wenn die App fehlt oder kaputt ist.

**Vom Laptop:** Der Laptop muss im WLAN des Stabs sein, oder im selben Hotspot (dann
`--upload-port kupferstab.local` anhängen).

```sh
cd firmware
pio run -e ota -t upload          # Firmware, Passwort: kupferstab
pio run -e ota -t uploadfs        # App
```

## Einstellungen

Alles in der App unter **Einstellungen**, ohne neu zu flashen:

| Einstellung | Standard | Hinweis |
| --- | --- | --- |
| Anzahl LEDs | 60 | 1–600 |
| Daten-Pin | D4 (GPIO2) | „Nächsten Pin testen“ schaltet live um |
| Farbreihenfolge | GRB | WS2812B ist GRB; wenn Rot und Grün vertauscht sind: RGB |
| Helligkeitsgrenze | 40 % | schützt Netzteil und Powerbank |
| Handy-Hotspot | leer | Name und Passwort; gilt sofort, ohne Neustart. Geht auch mit dem Heim-WLAN, wenn es 2,4 GHz hat |
| Eigenes WLAN (Rückfall) | Kupferstab / kupferstab | **Passwort vor der Party ändern**; startet den Stab neu |

Das OTA-Passwort für den Laptop ist `kupferstab`. Ändern lässt es sich über das Protokoll
(`otaPass`), siehe [docs/protocol.md](docs/protocol.md).

## Fehlersuche

| Symptom | Ursache und Abhilfe |
| --- | --- |
| Strip bleibt dunkel, App geht | Falscher Pin: Einstellungen → „Nächsten Pin testen“ antippen, bis das Lauflicht kommt, dann „Speichern“. |
| Nur ein Teil leuchtet | LED-Anzahl zu klein eingestellt, oder eine Daten-Unterbrechung im Strip (ab dem ersten dunklen Pixel). |
| Rot und Grün vertauscht | Farbreihenfolge auf RGB stellen. |
| Flackern, zufällige Farben | Datenleitung zu lang oder Pegel zu niedrig: 330-Ω-Widerstand in die Datenleitung, besser ein Pegelwandler 3,3 V → 5 V (z. B. 74AHCT125). |
| Wird rot/orange bei Weiß, ESP startet neu | Netzteil zu schwach: Helligkeitsgrenze senken oder 5 V auch am anderen Strip-Ende einspeisen. |
| WLAN „Kupferstab“ taucht nicht auf | Der Stab ist wahrscheinlich in deinem Hotspot: `http://kupferstab.local` öffnen. Sonst D1 mini neu starten; nach einer Änderung heißt das Netz so, wie du es eingestellt hast. |
| „Kupferstab“ geht immer wieder auf, obwohl der Hotspot an ist | Hotspot auf 5 GHz: „Kompatibilität maximieren“ (iPhone) bzw. 2,4 GHz einstellen. Sonst Name oder Passwort falsch. |
| `kupferstab.local` öffnet nichts | Android 12 und älter kennen `.local` oft nicht. Die Adresse steht unter Einstellungen → Handy-Hotspot, und der Strip zeigt beim Anmelden die letzte Zahl. |
| „Update auf GitHub nicht richtig signiert“ | Das Secret `SIGNING_KEY` passt nicht zu `firmware/src/signing_key.h`. Das Release-Skript prüft das eigentlich vorher. |
| Passwort vergessen | Neu per USB flashen. Die Einstellungen bleiben dabei erhalten, deshalb vorher mit `pio run -t erase` den Flash löschen. |

## Entwickeln

```sh
bun install
bun run build      # App nach app/dist/ und firmware/data/
bun run mock       # simulierter Stab: http://localhost:8080
bun test           # Protokoll und Hilfsfunktionen
bun run typecheck
bun run fw         # Firmware bauen
bun run fs         # App bauen + Dateisystem-Image
SIGNING_KEY_FILE=~/.config/kupferstab/signing.key bun run release 0.2.0   # Release-Dateien nach release/
```

Der simulierte Stab spricht dasselbe Protokoll wie die Firmware. Damit lässt sich die App ohne
Hardware im Browser ausprobieren.

| Ordner | Inhalt |
| --- | --- |
| `firmware/` | PlatformIO-Projekt (C++, Arduino-Framework) |
| `app/src/` | Handy-App (TypeScript ohne Framework) |
| `app/mock/` | simulierter Stab |
| `scripts/build-app.ts` | baut die App zu einer gzippten Seite plus Manifest und Icons |
| `docs/protocol.md` | Nachrichten zwischen App und Stab |
| `docs/plans/` | Umsetzungspläne |
