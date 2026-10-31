#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#ifndef FW_VERSION
#define FW_VERSION "0.1.0-dev"
#endif

// What the light is doing right now. Persisted, so the staff comes back the
// way it was switched off.
struct LightState {
  bool on = true;
  uint8_t bri = 160;     // 0..255, capped by Config::maxBri
  uint8_t effect = 0;    // index into kEffectIds
  uint8_t speed = 50;    // 1..100
  uint32_t color = 0xFF6A1F;   // copper
  uint32_t color2 = 0x1A0A00;  // background for chase and sparkle
};

// Hardware and network settings. Stored apart from LittleFS, so a filesystem
// update never wipes them.
struct Config {
  uint16_t leds = 164;   // physical pixels on the strip
  // The strip runs straight up, then folds back down beside itself: `fold` pixels up, an
  // optional single tip pixel, `fold` pixels down. Effects see the straight part, one layer of
  // the fold and the tip; the way down mirrors the way up. fold 0 means a plain strip.
  uint16_t fold = 50;
  bool tip = true;
  uint8_t pin = 2;       // GPIO number; 2 is D4 on the D1 mini
  bool rgbOrder = false; // false: GRB (WS2812B), true: RGB
  uint8_t maxBri = 102;  // 40 %
  String apSsid = "Kupferstab";
  String apPass = "kupferstab";
  String staSsid;
  String staPass;
  String otaPass = "kupferstab";
};

constexpr uint8_t kSceneCount = 4;
constexpr uint8_t kEffectCount = 8;
extern const char* const kEffectIds[kEffectCount];

struct Store {
  Config config;
  LightState state;
  LightState scenes[kSceneCount];
  bool sceneUsed[kSceneCount] = {false, false, false, false};
};

extern Store store;

void storeLoad();
// Marks the store dirty; storeLoop() writes it after a quiet period to spare the flash.
void storeTouch();
void storeLoop();

int effectIndex(const char* id);
String colorToHex(uint32_t c);
bool hexToColor(const char* hex, uint32_t& out);

void stateToJson(const LightState& s, JsonObject o);
// Applies the fields present in `o`; the rest stays.
void stateFromJson(LightState& s, JsonObjectConst o);
// Pixels the effects run over: the straight part, one layer of the fold, the tip.
uint16_t logicalLeds(const Config& c);

void configToJson(const Config& c, JsonObject o);  // passwords left out
// Returns true when a field changed that needs a restart (Wi-Fi).
bool configFromJson(Config& c, JsonObjectConst o, bool& ledsChanged);
