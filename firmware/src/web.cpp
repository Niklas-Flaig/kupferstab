#include "web.h"

#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <flash_hal.h>

#include "config.h"
#include "effects.h"
#include "selfupdate.h"

namespace web {

namespace {

const IPAddress kApIp(192, 168, 4, 1);
constexpr const char* kHostname = "kupferstab";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dns;

bool fsMounted = false;
bool broadcastDue = false;
uint32_t restartAt = 0;
uint32_t lastClientCleanup = 0;

// Update upload in progress
bool updateFailed = false;
String updateError;

// The page that is always there, even when the app on LittleFS is missing or broken.
const char kRescuePage[] PROGMEM = R"html(<!doctype html><html lang="de"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Kupferstab Update</title>
<style>body{font:16px system-ui,sans-serif;margin:0;padding:24px;background:#16110e;color:#f4e9e1}
h1{font-size:22px}form{display:grid;gap:10px;margin:18px 0;padding:16px;border:1px solid #4a3426;border-radius:10px}
button{font:inherit;padding:10px;border-radius:8px;border:0;background:#d9783f;color:#16110e;font-weight:700}
p{color:#c9b3a4}</style><h1>Kupferstab · Update</h1>
<p>Firmware: <code>firmware.bin</code> · App: <code>littlefs.bin</code>. Nach dem Hochladen startet der Stab neu.</p>
<form method="post" action="/update?target=firmware" enctype="multipart/form-data"><b>Firmware</b>
<input type="file" name="file" accept=".bin,.gz" required><button>Firmware hochladen</button></form>
<form method="post" action="/update?target=fs" enctype="multipart/form-data"><b>App (Dateisystem)</b>
<input type="file" name="file" accept=".bin" required><button>App hochladen</button></form>
<p><a href="/" style="color:#d9783f">Zur App</a></p></html>)html";

void fillFull(JsonDocument& doc) {
  doc["t"] = "state";
  stateToJson(store.state, doc["state"].to<JsonObject>());
  JsonArray scenes = doc["scenes"].to<JsonArray>();
  for (uint8_t i = 0; i < kSceneCount; i++) {
    if (store.sceneUsed[i]) stateToJson(store.scenes[i], scenes.add<JsonObject>());
    else scenes.add(nullptr);
  }
  configToJson(store.config, doc["config"].to<JsonObject>());
  JsonObject info = doc["info"].to<JsonObject>();
  info["fw"] = FW_VERSION;
  info["heap"] = ESP.getFreeHeap();
  info["uptime"] = millis() / 1000;
  info["apIp"] = WiFi.softAPIP().toString();
  info["staIp"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String();
  info["update"] = selfupdate::available();
  info["clients"] = ws.count();
  info["app"] = fsMounted && (LittleFS.exists("/index.html") || LittleFS.exists("/index.html.gz"));
}

String fullJson() {
  JsonDocument doc;
  fillFull(doc);
  String out;
  serializeJson(doc, out);
  return out;
}

void reply(AsyncWebSocketClient* client, const char* type, const char* message) {
  JsonDocument doc;
  doc["t"] = type;
  doc["message"] = message;
  String out;
  serializeJson(doc, out);
  client->text(out);
}

// Messages from the app; see docs/protocol.md.
void handleMessage(AsyncWebSocketClient* client, const char* text, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, text, len)) return reply(client, "error", "Nachricht nicht lesbar");
  const char* t = doc["t"] | "";

  if (strcmp(t, "get") == 0) {
    client->text(fullJson());
  } else if (strcmp(t, "set") == 0) {
    stateFromJson(store.state, doc["state"]);
    storeTouch();
    broadcastDue = true;
  } else if (strcmp(t, "scene") == 0) {
    int slot = doc["slot"] | -1;
    const char* op = doc["op"] | "";
    if (slot < 0 || slot >= kSceneCount) return reply(client, "error", "Szene gibt es nicht");
    if (strcmp(op, "save") == 0) {
      store.scenes[slot] = store.state;
      store.sceneUsed[slot] = true;
    } else if (strcmp(op, "apply") == 0 && store.sceneUsed[slot]) {
      store.state = store.scenes[slot];
      store.state.on = true;
    } else if (strcmp(op, "clear") == 0) {
      store.sceneUsed[slot] = false;
    }
    storeTouch();
    broadcastDue = true;
  } else if (strcmp(t, "config") == 0) {
    bool ledsChanged = false;
    bool restart = configFromJson(store.config, doc["config"], ledsChanged);
    storeTouch();
    if (ledsChanged) {
      effects::reconfigure();
      effects::runTest();
    }
    if (restart) {
      reply(client, "notice", "Gespeichert. Der Stab startet neu, verbinde dich danach wieder.");
      restartAt = millis() + 2500;  // after the store was written
    }
    broadcastDue = true;
  } else if (strcmp(t, "test") == 0) {
    effects::runTest();
  } else if (strcmp(t, "reboot") == 0) {
    restartAt = millis() + 2500;
  } else if (strcmp(t, "checkUpdate") == 0) {
    selfupdate::requestCheck();
  } else if (strcmp(t, "installUpdate") == 0) {
    selfupdate::requestInstall();
  } else {
    reply(client, "error", "Unbekannter Befehl");
  }
}

void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data,
               size_t len) {
  if (type == WS_EVT_CONNECT) {
    client->text(fullJson());
  } else if (type == WS_EVT_DATA) {
    auto* info = static_cast<AwsFrameInfo*>(arg);
    // The app sends small single-frame text messages; anything else is ignored.
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
      handleMessage(client, reinterpret_cast<const char*>(data), len);
  }
}

void onUpload(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len,
              bool final) {
  if (index == 0) {
    updateFailed = false;
    updateError = "";
    bool fs = request->hasParam("target") && request->getParam("target")->value() == "fs";
    if (!fs && len > 0 && data[0] != 0xE9 && data[0] != 0x1F) {
      updateFailed = true;
      updateError = "Das ist keine Firmware-Datei (firmware.bin).";
      return;
    }
    effects::blackout();
    Update.runAsync(true);
    bool ok;
    if (fs) {
      LittleFS.end();
      fsMounted = false;
      ok = Update.begin(FS_PHYS_SIZE, U_FS);
    } else {
      ok = Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000, U_FLASH);
    }
    if (!ok) {
      updateFailed = true;
      updateError = Update.getErrorString();
      return;
    }
  }
  if (updateFailed) return;
  if (len && Update.write(data, len) != len) {
    updateFailed = true;
    updateError = Update.getErrorString();
    return;
  }
  if (final && !Update.end(true)) {
    updateFailed = true;
    updateError = Update.getErrorString();
  }
}

void onUploadDone(AsyncWebServerRequest* request) {
  bool ok = !updateFailed && !Update.hasError() && Update.isFinished();
  if (!ok && updateError.length() == 0) updateError = "Update unvollständig.";
  AsyncWebServerResponse* res =
      request->beginResponse(ok ? 200 : 500, "text/plain; charset=utf-8", ok ? "OK" : updateError);
  res->addHeader("Connection", "close");
  request->send(res);
  // Restart either way: a failed filesystem update has unmounted LittleFS.
  restartAt = millis() + 1500;
}

bool isOurHost(AsyncWebServerRequest* request) {
  String host = request->host();
  return host == kApIp.toString() || host == WiFi.localIP().toString() || host.startsWith(kHostname);
}

void setupWifi() {
  const Config& c = store.config;
  WiFi.persistent(false);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.hostname(kHostname);
  WiFi.mode(c.staSsid.length() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(c.apSsid.c_str(), c.apPass.length() >= 8 ? c.apPass.c_str() : nullptr);
  if (c.staSsid.length()) WiFi.begin(c.staSsid.c_str(), c.staPass.c_str());
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", kApIp);
  MDNS.begin(kHostname);
  MDNS.addService("http", "tcp", 80);
}

void setupOta() {
  ArduinoOTA.setHostname(kHostname);
  ArduinoOTA.setPassword(store.config.otaPass.c_str());
  ArduinoOTA.onStart([]() {
    effects::blackout();
    if (ArduinoOTA.getCommand() == U_FS) {
      LittleFS.end();
      fsMounted = false;
    }
  });
  ArduinoOTA.begin();
}

void setupServer() {
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/api/state", HTTP_GET,
            [](AsyncWebServerRequest* request) { request->send(200, "application/json", fullJson()); });
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html; charset=utf-8", FPSTR(kRescuePage));
  });
  server.on("/update", HTTP_POST, onUploadDone, onUpload);

  // Phones probe these to detect a captive portal; answering with a redirect
  // makes them open the app on their own.
  for (const char* probe : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                            "/connecttest.txt", "/ncsi.txt", "/fwlink", "/redirect"}) {
    server.on(probe, HTTP_ANY,
              [](AsyncWebServerRequest* request) { request->redirect("http://192.168.4.1/"); });
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (fsMounted && (LittleFS.exists("/index.html.gz") || LittleFS.exists("/index.html"))) {
      AsyncWebServerResponse* res = request->beginResponse(LittleFS, "/index.html", "text/html");
      res->addHeader("Cache-Control", "no-cache");
      request->send(res);
    } else {
      request->send(200, "text/html; charset=utf-8", FPSTR(kRescuePage));
    }
  });
  server.serveStatic("/", LittleFS, "/").setCacheControl("max-age=86400");

  server.onNotFound([](AsyncWebServerRequest* request) {
    if (!isOurHost(request)) return request->redirect("http://192.168.4.1/");
    request->send(404, "text/plain", "Nicht gefunden");
  });

  server.begin();
}

}  // namespace

void begin() {
  fsMounted = LittleFS.begin();
  setupWifi();
  setupOta();
  setupServer();
}

void notifyAll(const char* message) {
  JsonDocument doc;
  doc["t"] = "notice";
  doc["message"] = message;
  String out;
  serializeJson(doc, out);
  ws.textAll(out);
  broadcastDue = true;  // the snapshot carries info.update
}

void loop() {
  dns.processNextRequest();
  MDNS.update();
  ArduinoOTA.handle();

  if (broadcastDue && ws.availableForWriteAll()) {
    broadcastDue = false;
    ws.textAll(fullJson());
  }
  if (millis() - lastClientCleanup > 1000) {
    lastClientCleanup = millis();
    ws.cleanupClients();
  }
  if (restartAt && (int32_t)(millis() - restartAt) > 0) {
    effects::blackout();
    ESP.restart();
  }
}

}  // namespace web
