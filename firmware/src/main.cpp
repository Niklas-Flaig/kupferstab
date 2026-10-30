// Kupferstab: a Wemos D1 mini drives the NeoPixel strip inside the copper staff.
// The phone app talks to it over the staff's own Wi-Fi; see docs/protocol.md.

#include <Arduino.h>

#include "config.h"
#include "effects.h"
#include "web.h"

void setup() {
  Serial.begin(115200);
  storeLoad();
  effects::begin();
  web::begin();
  Serial.printf("\nKupferstab %s: Wi-Fi \"%s\", http://192.168.4.1\n", FW_VERSION, store.config.apSsid.c_str());
}

void loop() {
  web::loop();
  effects::loop();
  storeLoop();
}
