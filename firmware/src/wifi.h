#pragma once

#include <Arduino.h>

namespace wifi {

// Where the staff is on the air.
//   Connecting   at start-up: trying the phone's hotspot, own Wi-Fi off
//   Station      in the hotspot, own Wi-Fi off; reachable as kupferstab.local
//   AccessPoint  own Wi-Fi "Kupferstab" with captive portal; looks for the hotspot every 30 s
//   Joining      the hotspot showed up in a scan, own Wi-Fi stays open until it works
enum class Mode { Connecting, Station, AccessPoint, Joining };

// Tries the hotspot first if one is stored, else opens the own Wi-Fi.
void begin();
void loop();
// The hotspot credentials changed in the settings; applies them without a restart.
void credentialsChanged();

Mode mode();
const char* modeName();  // "connecting", "sta", "ap", "joining" as in docs/protocol.md
bool online();           // in the hotspot, with an address
String stationIp();      // empty unless online()

}  // namespace wifi
