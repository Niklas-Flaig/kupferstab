# Protokoll zwischen App und Stab

Die App spricht über einen WebSocket `ws://<stab>/ws` mit dem Stab. Jede Nachricht ist ein
JSON-Objekt mit dem Feld `t` (Typ). Die Firmware setzt das in `firmware/src/web.cpp` um, die App
in `app/src/protocol.ts`, der simulierte Stab in `app/mock/server.ts`.

## Stab → App

### `state`: der ganze Stand

Kommt beim Verbinden und nach jeder Änderung an alle verbundenen Handys.

```json
{
  "t": "state",
  "state": {
    "on": true, "bri": 160, "effect": "atem",
    "fx": {
      "atem": { "variant": 0, "palette": ["#4A5BFF", "#14C9A8", "#B04CFF"], "period": 14, "spread": 35, "p": { "breath": 7, "wave": 4.5, "width": 12 } },
      "strobo": { "variant": 0, "palette": ["#14C9A8", "#2A5BFF"], "period": 10, "spread": 90, "p": { "charge": 2, "build": 4, "hold": 5, "after": 5 }, "white": true },
      "glut": { "…": "…" }, "polarlicht": { "…": "…" }, "funkenflug": { "…": "…" }, "mosaik": { "…": "…" }
    }
  },
  "scenes": [{ "on": true, "bri": 160, "effect": "glut", "fx": { "glut": { "variant": 0, "palette": ["#FF6A1F"], "period": 12, "spread": 30, "p": { "tempo": 50 } } } }, null, null, null],
  "run": null,
  "config": { "leds": 164, "fold": 50, "tip": true, "pin": 2, "order": "GRB", "maxBri": 102, "apSsid": "Kupferstab", "staSsid": "" },
  "info": { "fw": "0.1.0", "heap": 23456, "uptime": 42, "apIp": "192.168.4.1", "staIp": "", "update": "", "clients": 1, "app": true }
}
```

Eine gespeicherte Szene enthält nur die Einstellungen ihres eigenen Effekts; beim Abrufen behalten
die anderen Effekte ihre. `run` ist `{ "t": 3.2, "back": "atem" }`, solange Strobo läuft: Sekunden
seit dem Start und der Effekt, in den Strobo danach zurückspringt.

Passwörter schickt der Stab nie zurück. `info.update` ist eine neuere Version auf GitHub, leer wenn
keine da ist oder noch nicht geprüft wurde.

### `notice` / `error`

```json
{ "t": "notice", "message": "Gespeichert. Der Stab startet neu, verbinde dich danach wieder." }
```

## App → Stab

| Nachricht | Wirkung |
| --- | --- |
| `{"t":"get"}` | Stab antwortet mit `state` |
| `{"t":"set","state":{…}}` | übernimmt die vorhandenen Felder von `state`; `fx` wird Feld für Feld gemischt, z. B. `{"fx":{"glut":{"p":{"tempo":80}}}}` |
| `{"t":"run"}` | startet Strobo, oder startet es neu, wenn es schon läuft |
| `{"t":"scene","slot":0,"op":"save"}` | legt den aktuellen Stand auf Platz 0–3; `op` auch `apply` und `clear` |
| `{"t":"config","config":{…}}` | Einstellungen; `pin`, `leds`, `fold`, `tip` und `order` wirken sofort (mit Test-Lauflicht), WLAN- und Passwort-Felder starten den Stab neu |
| `{"t":"test"}` | spielt das Test-Lauflicht |
| `{"t":"reboot"}` | Neustart |
| `{"t":"checkUpdate"}` | fragt GitHub (nur im Heim-WLAN); das Ergebnis kommt als `notice` und in `info.update` |
| `{"t":"installUpdate"}` | installiert `info.update` (App-Dateien, dann Firmware) und startet neu; Fehler kommen als `notice` |

### Felder

| Feld | Werte |
| --- | --- |
| `on` | `true` / `false` |
| `bri` | 0–255, gedeckelt durch `maxBri` |
| `effect` | `atem`, `strobo`, `glut`, `polarlicht`, `funkenflug`, `mosaik` |
| `fx.<effect>.variant` | `atem`: 0 Atem + Welle, 1 Gezeiten, 2 Nebel · `strobo`: 0 Immer schneller, 1 Herzschlag · `mosaik`: 0 Kacheln, 1 Funkeln, 2 Scherben · sonst 0 |
| `fx.<effect>.palette` | 1–6 × `#RRGGBB` |
| `fx.<effect>.period` | 3–60 s je Palettenfarbe |
| `fx.<effect>.spread` | 0–100 %, wie weit sich die Palette über den Stab verteilt |
| `fx.atem.p` | `breath` 3–14 s, `wave` 1,5–12 s, `width` 4–30 % |
| `fx.strobo.p` | `charge` 0–20 s (Aufladen bis zur Spitze), `build` 3–20 s (Aufbau danach), `hold` 0–20 s (Strobo voll an), `after` 2–15 s (Ausklang) |
| `fx.strobo.white` | Blitze in Weiß statt in den Palettenfarben |
| `fx.glut.p`, `fx.polarlicht.p`, `fx.funkenflug.p` | `tempo` 5–100 % |
| `fx.mosaik.p` | `tempo` 5–100 %, `density` 5–100 % |
| `leds` | 1–600, alle Pixel auf dem Strip |
| `fold` | 0–300, Pixel je Lage der Doppellage oben; 0 für einen geraden Strip |
| `tip` | `true` / `false`: ein Pixel zwischen Hin- und Rückweg. Passen zwei Lagen und Spitze nicht auf `leds`, gilt der Strip als gerade |
| `pin` | GPIO 0, 1, 2, 3, 4, 5, 12, 13, 14, 15 |
| `order` | `GRB` oder `RGB` |
| `maxBri` | 10–255 |
| `apSsid`, `apPass` | eigenes WLAN; Passwort leer (offen) oder 8–63 Zeichen |
| `staSsid`, `staPass` | Heim-WLAN, optional; nötig für Updates von GitHub |
| `otaPass` | Passwort für Updates vom Laptop (`pio run -e ota`), mindestens 4 Zeichen |

Werte außerhalb der Grenzen werden begrenzt, unbekannte Felder ignoriert.

## HTTP

| Pfad | |
| --- | --- |
| `GET /` | die App (`index.html.gz` aus LittleFS); ohne App die Notfall-Seite |
| `GET /api/state` | wie die Nachricht `state` |
| `GET /update` | Notfall-Seite zum Hochladen, fest in der Firmware |
| `POST /update?target=firmware` | `multipart/form-data`, Feld `file`: Firmware-Image |
| `POST /update?target=fs` | dasselbe für das Dateisystem-Image (die App) |

Auf `POST /update` antwortet der Stab mit `200 OK` oder `500` und dem Grund. Danach startet er neu.
Fremde Hosts und die Captive-Portal-Prüfungen der Handys (`/generate_204`,
`/hotspot-detect.html`, …) leitet er auf `http://192.168.4.1/` um.

## Releases auf GitHub

Der Stab liest `https://github.com/Niklas-Flaig/kupferstab/releases/latest/download/manifest.json`
und danach alles Weitere aus dem Release mit dem Tag aus dem Manifest:

```json
{ "version": "0.2.0", "tag": "v0.2.0", "firmware": "firmware.bin",
  "files": [{ "name": "index.html.gz", "size": 8159, "sha256": "…" }] }
```

- `manifest.sig`: RSA-2048-Signatur (PKCS#1 v1.5, SHA-256) über die Bytes von `manifest.json`.
- `firmware.bin`: Image + Signatur + Länge der Signatur (u32, little-endian), das Format von
  `Update.installSignature` im ESP8266-Core.
- Die App-Dateien prüft der Stab gegen `sha256` aus dem signierten Manifest.
- `littlefs.bin` liegt nur für den Upload von Hand dabei.
