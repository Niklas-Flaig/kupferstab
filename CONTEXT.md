# Kontext

## Begriffe

| Begriff | Bedeutung |
| --- | --- |
| **Stab** | Der Kupferstab mit Wemos D1 mini (ESP8266) und NeoPixel-Strip (WS2812B) im Inneren. |
| **App** | Die Handy-Seite, die der Stab selbst ausliefert. Lässt sich zum Home-Bildschirm hinzufügen (PWA ohne Service Worker, weil der Stab nur HTTP spricht). |
| **Zustand** (`state`) | Was das Licht gerade tut: an/aus, Helligkeit, Effekt, Tempo, zwei Farben. |
| **Effekt** | Eine Animation der Firmware, z. B. Regenbogen oder Feuer. Die Liste steht in `kEffectIds` (Firmware) und `EFFECTS` (App), beide in derselben Reihenfolge. |
| **Szene** | Ein gespeicherter Zustand auf einem von vier Plätzen. |
| **Einstellungen** (`config`) | Hardware und Netz: LED-Anzahl, Pin, Farbreihenfolge, Helligkeitsgrenze, WLAN. |
| **Captive Portal** | Der Stab beantwortet jede DNS-Anfrage mit seiner eigenen IP. Dadurch öffnet das Handy beim Verbinden die App von selbst. |
| **Release** | Eine Version auf GitHub, gebaut von `.github/workflows/release.yml`: signierte Firmware, die App-Dateien einzeln, `manifest.json` mit Signatur. Der Stab prüft und installiert sie selbst (`selfupdate.*`), sobald er im Heim-WLAN ist. |
| **Notfall-Seite** | `/update`, fest in der Firmware. Darüber geht ein Update auch ohne App. |

## Aufbau

- `firmware/`: PlatformIO, Arduino-Framework. Zum Stab gehören `config.*` (Zustand, Einstellungen, Speicher), `effects.*` (Animationen), `web.*` (WLAN, Server, WebSocket, OTA), `selfupdate.*` (Releases von GitHub) und `main.cpp`.
- `app/`: TypeScript ohne Framework. `protocol.ts` ist der einzige Ort für Nachrichtenformate und reine Hilfsfunktionen. `main.ts` enthält die Oberfläche.
- Einstellungen, Zustand und Szenen liegen im **EEPROM-Bereich** des Flash, nicht auf LittleFS. Ein App-Update überschreibt sonst das ganze Dateisystem und damit die Einstellungen.
- Ein Update von GitHub schreibt die App **dateiweise** (erst `.tmp`, nach SHA-256-Prüfung umbenannt), nie das ganze LittleFS-Image. Nur dieser Weg verlangt eine Signatur; der Upload in der App und `espota` bleiben unsigniert, sie gehen nur aus dem eigenen WLAN oder dem Heim-WLAN.
- Flash-Aufteilung 4m2m: Sketch und OTA-Puffer je ca. 1 MB, LittleFS ab `0x200000` mit 2 MB.
