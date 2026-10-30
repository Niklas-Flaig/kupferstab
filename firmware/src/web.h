#pragma once

namespace web {

// The app, WebSocket control, update upload and ArduinoOTA, on top of wifi::.
void begin();
void loop();
// Sends a notice to every connected phone and a fresh snapshot after it.
void notifyAll(const char* message);

}  // namespace web
