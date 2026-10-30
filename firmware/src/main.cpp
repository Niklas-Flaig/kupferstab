// Kupferstab: a Wemos D1 mini drives the NeoPixel strip inside the copper staff.
// It joins the phone's hotspot, or opens its own Wi-Fi when that is not there; the phone app
// talks to it over either (see docs/protocol.md). New versions come from GitHub releases.

#include <Arduino.h>

#include "config.h"
#include "effects.h"
#include "selfupdate.h"
#include "web.h"

void setup() {
  Serial.begin(115200);
  storeLoad();
  effects::begin();
  web::begin();
  Serial.printf("\nKupferstab %s: hotspot \"%s\", own Wi-Fi \"%s\"\n", FW_VERSION, store.config.staSsid.c_str(),
                store.config.apSsid.c_str());
}

void loop() {
  web::loop();
  effects::loop();
  selfupdate::loop();
  storeLoop();
}
