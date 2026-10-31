#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#ifndef FW_VERSION
#define FW_VERSION "0.1.0-dev"
#endif

constexpr uint8_t kEffectCount = 6;
enum EffectId : uint8_t { kAtem, kStrobo, kGlut, kPolar, kFunken, kMosaik };
// The highest point of the staff as it is held: sparks of Funkenflug fly here.
constexpr uint16_t kHighestPixel = 50;
constexpr uint8_t kPaletteMax = 6;
constexpr uint8_t kFxParams = 4;

struct ParamDef {
  const char* key;
  float min, max, def;
};

// What each effect is and what it starts with; mirrored in app/src/protocol.ts.
struct EffectDef {
  const char* id;
  uint8_t variants;
  uint8_t params;
  ParamDef p[kFxParams];
  uint8_t paletteLen;
  uint32_t palette[kPaletteMax];
  float period;  // seconds per palette colour
  float spread;  // 0..100 % of the palette spread along the staff
};
extern const EffectDef kEffects[kEffectCount];

// The settings of one effect: variant, colour palette and the effect's own parameters
// (p[i] is kEffects[effect].p[i]).
struct FxSettings {
  uint8_t variant = 0;
  uint8_t paletteLen = 1;
  uint32_t palette[kPaletteMax] = {0xFF6A1F};
  float period = 12;
  float spread = 0;
  float p[kFxParams] = {0, 0, 0, 0};
  bool white = true;  // strobe flashes in white instead of the palette
};
void fxDefaults(uint8_t effect, FxSettings& f);

// What the light is doing right now. Persisted, so the staff comes back the way it was
// switched off. Every effect keeps its own settings.
struct LightState {
  bool on = true;
  uint8_t bri = 160;   // 0..255, capped by Config::maxBri
  uint8_t effect = kAtem;
  FxSettings fx[kEffectCount];
  LightState() {
    for (uint8_t e = 0; e < kEffectCount; e++) fxDefaults(e, fx[e]);
  }
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

struct Store {
  Config config;
  LightState state;
  LightState scenes[kSceneCount];
  bool sceneUsed[kSceneCount] = {false, false, false, false};
  // The effect the strobe returns to; not stored, a strobe is never what the staff boots into.
  uint8_t back = kAtem;
};

extern Store store;

void storeLoad();
// Marks the store dirty; storeLoop() writes it after a quiet period to spare the flash.
void storeTouch();
void storeLoop();

int effectIndex(const char* id);
String colorToHex(uint32_t c);
bool hexToColor(const char* hex, uint32_t& out);

// `activeOnly` writes only the running effect's settings, as the saved scenes keep them.
void stateToJson(const LightState& s, JsonObject o, bool activeOnly = false);
// Applies the fields present in `o`; the rest stays.
void stateFromJson(LightState& s, JsonObjectConst o);
// Pixels the effects run over: the straight part, one layer of the fold, the tip.
uint16_t logicalLeds(const Config& c);

void configToJson(const Config& c, JsonObject o);  // passwords left out
// Returns true when a field changed that needs a restart (Wi-Fi).
bool configFromJson(Config& c, JsonObjectConst o, bool& ledsChanged);
