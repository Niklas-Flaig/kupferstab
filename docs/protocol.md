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
  "state": { "on": true, "bri": 160, "effect": "rainbow", "speed": 50, "color": "#FF6A1F", "color2": "#1A0A00" },
  "scenes": [{ "on": true, "bri": 160, "effect": "fire", "speed": 60, "color": "#FF6A1F", "color2": "#1A0A00" }, null, null, null],
  "config": { "leds": 60, "pin": 2, "order": "GRB", "maxBri": 102, "apSsid": "Kupferstab", "staSsid": "" },
  "info": { "fw": "0.1.0", "heap": 23456, "uptime": 42, "apIp": "192.168.4.1", "staIp": "", "clients": 1, "app": true }
}
```

Passwörter schickt der Stab nie zurück.

### `notice` / `error`

```json
{ "t": "notice", "message": "Gespeichert. Der Stab startet neu, verbinde dich danach wieder." }
```

## App → Stab

| Nachricht | Wirkung |
| --- | --- |
| `{"t":"get"}` | Stab antwortet mit `state` |
| `{"t":"set","state":{…}}` | übernimmt die vorhandenen Felder von `state` |
| `{"t":"scene","slot":0,"op":"save"}` | legt den aktuellen Stand auf Platz 0–3; `op` auch `apply` und `clear` |
| `{"t":"config","config":{…}}` | Einstellungen; `pin`, `leds` und `order` wirken sofort (mit Test-Lauflicht), WLAN- und Passwort-Felder starten den Stab neu |
| `{"t":"test"}` | spielt das Test-Lauflicht |
| `{"t":"reboot"}` | Neustart |

### Felder

| Feld | Werte |
| --- | --- |
| `on` | `true` / `false` |
| `bri` | 0–255, gedeckelt durch `maxBri` |
| `effect` | `solid`, `rainbow`, `breathe`, `chase`, `sparkle`, `fire`, `colorloop`, `strobe` |
| `speed` | 1–100 |
| `color`, `color2` | `#RRGGBB`; `color2` ist der Hintergrund bei `chase` und `sparkle` |
| `leds` | 1–600 |
| `pin` | GPIO 0, 1, 2, 3, 4, 5, 12, 13, 14, 15 |
| `order` | `GRB` oder `RGB` |
| `maxBri` | 10–255 |
| `apSsid`, `apPass` | eigenes WLAN; Passwort leer (offen) oder 8–63 Zeichen |
| `staSsid`, `staPass` | Heim-WLAN, optional |
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
