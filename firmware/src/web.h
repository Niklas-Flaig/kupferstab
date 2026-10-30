#pragma once

namespace web {

// Wi-Fi access point (plus home network if configured), captive portal,
// app, WebSocket control, update upload and ArduinoOTA.
void begin();
void loop();
// Sends a notice to every connected phone and a fresh snapshot after it.
void notifyAll(const char* message);

}  // namespace web
