#include "config.h"

#include <EEPROM.h>

const EffectDef kEffects[kEffectCount] = {
    {"atem", 3, 3, {{"breath", 3, 14, 7}, {"wave", 1.5, 12, 4.5}, {"width", 4, 30, 12}}, 3,
     {0x4A5BFF, 0x14C9A8, 0xB04CFF}, 14, 35},
    {"strobo", 2, 4, {{"charge", 0, 20, 2}, {"build", 3, 20, 4}, {"hold", 0, 20, 5}, {"after", 2, 15, 5}}, 2,
     {0x14C9A8, 0x2A5BFF}, 10, 90},
    {"glut", 1, 1, {{"tempo", 5, 100, 50}}, 1, {0xFF6A1F}, 12, 30},
    {"polarlicht", 1, 1, {{"tempo", 5, 100, 50}}, 1, {0xFF6A1F}, 12, 30},
    {"funkenflug", 1, 1, {{"tempo", 5, 100, 50}}, 1, {0xFF6A1F}, 12, 30},
    {"mosaik", 3, 2, {{"tempo", 5, 100, 50}, {"density", 5, 100, 50}}, 4,
     {0xFF2A6D, 0xB04CFF, 0x2A5BFF, 0x14C9A8}, 16, 50},
};

void fxDefaults(uint8_t effect, FxSettings& f) {
  const EffectDef& d = kEffects[effect];
  f.variant = 0;
  f.paletteLen = d.paletteLen;
  for (uint8_t i = 0; i < kPaletteMax; i++) f.palette[i] = i < d.paletteLen ? d.palette[i] : 0;
  f.period = d.period;
  f.spread = d.spread;
  for (uint8_t i = 0; i < kFxParams; i++) f.p[i] = i < d.params ? d.p[i].def : 0;
  f.white = true;
}

Store store;

namespace {

constexpr size_t kEepromSize = 4096;
constexpr uint32_t kMagic = 0x4B535431;  // "KST1"
constexpr uint32_t kSaveDelayMs = 2000;

bool dirty = false;
uint32_t dirtySince = 0;

void storeToJson(JsonDocument& doc) {
  JsonObject c = doc["config"].to<JsonObject>();
  configToJson(store.config, c);
  c["apPass"] = store.config.apPass;
  c["staPass"] = store.config.staPass;
  c["otaPass"] = store.config.otaPass;
  JsonObject state = doc["state"].to<JsonObject>();
  stateToJson(store.state, state);
  if (store.state.effect == kStrobo) state["effect"] = kEffects[store.back].id;
  JsonArray scenes = doc["scenes"].to<JsonArray>();
  for (uint8_t i = 0; i < kSceneCount; i++) {
    if (store.sceneUsed[i]) stateToJson(store.scenes[i], scenes.add<JsonObject>(), true);
    else scenes.add(nullptr);
  }
}

void storeSave() {
  JsonDocument doc;
  storeToJson(doc);
  String text;
  serializeJson(doc, text);
  if (text.length() + 6 > kEepromSize) return;
  EEPROM.put(0, kMagic);
  uint16_t len = text.length();
  EEPROM.put(4, len);
  for (uint16_t i = 0; i < len; i++) EEPROM.write(6 + i, text[i]);
  EEPROM.commit();
}

}  // namespace

void storeLoad() {
  EEPROM.begin(kEepromSize);
  uint32_t magic = 0;
  EEPROM.get(0, magic);
  if (magic != kMagic) return;
  uint16_t len = 0;
  EEPROM.get(4, len);
  if (len == 0 || len + 6 > kEepromSize) return;
  String text;
  text.reserve(len);
  for (uint16_t i = 0; i < len; i++) text += char(EEPROM.read(6 + i));
  JsonDocument doc;
  if (deserializeJson(doc, text)) return;

  bool ignored;
  configFromJson(store.config, doc["config"], ignored);
  stateFromJson(store.state, doc["state"]);
  JsonArrayConst scenes = doc["scenes"];
  for (uint8_t i = 0; i < kSceneCount && i < (int)scenes.size(); i++) {
    store.sceneUsed[i] = scenes[i].is<JsonObjectConst>();
    if (store.sceneUsed[i]) stateFromJson(store.scenes[i], scenes[i]);
  }
}

void storeTouch() {
  if (!dirty) dirtySince = millis();
  dirty = true;
}

void storeLoop() {
  if (dirty && millis() - dirtySince > kSaveDelayMs) {
    dirty = false;
    storeSave();
  }
}

int effectIndex(const char* id) {
  if (!id) return -1;
  for (uint8_t i = 0; i < kEffectCount; i++)
    if (strcmp(id, kEffects[i].id) == 0) return i;
  return -1;
}

String colorToHex(uint32_t c) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%06X", (unsigned)(c & 0xFFFFFF));
  return String(buf);
}

bool hexToColor(const char* hex, uint32_t& out) {
  if (!hex || hex[0] != '#' || strlen(hex) != 7) return false;
  char* end = nullptr;
  unsigned long v = strtoul(hex + 1, &end, 16);
  if (!end || *end != '\0') return false;
  out = v & 0xFFFFFF;
  return true;
}

namespace {

float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void fxToJson(uint8_t e, const FxSettings& f, JsonObject o) {
  o["variant"] = f.variant;
  JsonArray pal = o["palette"].to<JsonArray>();
  for (uint8_t i = 0; i < f.paletteLen; i++) pal.add(colorToHex(f.palette[i]));
  o["period"] = f.period;
  o["spread"] = f.spread;
  JsonObject p = o["p"].to<JsonObject>();
  for (uint8_t i = 0; i < kEffects[e].params; i++) p[kEffects[e].p[i].key] = f.p[i];
  if (e == kStrobo) o["white"] = f.white;
}

void fxFromJson(uint8_t e, FxSettings& f, JsonObjectConst o) {
  const EffectDef& d = kEffects[e];
  if (o["variant"].is<int>()) f.variant = constrain(o["variant"].as<int>(), 0, d.variants - 1);
  JsonArrayConst pal = o["palette"];
  if (!pal.isNull()) {
    uint32_t colors[kPaletteMax];
    uint8_t n = 0;
    for (JsonVariantConst c : pal)
      if (n < kPaletteMax && hexToColor(c.as<const char*>(), colors[n])) n++;
    if (n) {
      f.paletteLen = n;
      for (uint8_t i = 0; i < n; i++) f.palette[i] = colors[i];
    }
  }
  if (o["period"].is<float>()) f.period = clampf(o["period"], 3, 60);
  if (o["spread"].is<float>()) f.spread = clampf(o["spread"], 0, 100);
  JsonObjectConst p = o["p"];
  for (uint8_t i = 0; i < d.params; i++)
    if (p[d.p[i].key].is<float>()) f.p[i] = clampf(p[d.p[i].key], d.p[i].min, d.p[i].max);
  if (o["white"].is<bool>()) f.white = o["white"];
}

}  // namespace

void stateToJson(const LightState& s, JsonObject o, bool activeOnly) {
  o["on"] = s.on;
  o["bri"] = s.bri;
  o["effect"] = kEffects[s.effect < kEffectCount ? s.effect : 0].id;
  JsonObject fx = o["fx"].to<JsonObject>();
  for (uint8_t e = 0; e < kEffectCount; e++)
    if (!activeOnly || e == s.effect) fxToJson(e, s.fx[e], fx[kEffects[e].id].to<JsonObject>());
}

void stateFromJson(LightState& s, JsonObjectConst o) {
  if (o.isNull()) return;
  if (o["on"].is<bool>()) s.on = o["on"];
  if (o["bri"].is<int>()) s.bri = constrain(o["bri"].as<int>(), 0, 255);
  if (o["effect"].is<const char*>()) {
    int i = effectIndex(o["effect"]);
    if (i >= 0) s.effect = i;
  }
  JsonObjectConst fx = o["fx"];
  for (uint8_t e = 0; e < kEffectCount; e++)
    if (fx[kEffects[e].id].is<JsonObjectConst>()) fxFromJson(e, s.fx[e], fx[kEffects[e].id]);
}

uint16_t logicalLeds(const Config& c) { return c.leds - c.fold; }

void configToJson(const Config& c, JsonObject o) {
  o["leds"] = c.leds;
  o["fold"] = c.fold;
  o["tip"] = c.tip;
  o["pin"] = c.pin;
  o["order"] = c.rgbOrder ? "RGB" : "GRB";
  o["maxBri"] = c.maxBri;
  o["apSsid"] = c.apSsid;
  o["staSsid"] = c.staSsid;
}

bool configFromJson(Config& c, JsonObjectConst o, bool& ledsChanged) {
  ledsChanged = false;
  bool restart = false;
  if (o.isNull()) return false;
  if (o["leds"].is<int>()) {
    uint16_t v = constrain(o["leds"].as<int>(), 1, 600);
    ledsChanged |= v != c.leds;
    c.leds = v;
  }
  if (o["fold"].is<int>()) {
    uint16_t v = constrain(o["fold"].as<int>(), 0, 300);
    ledsChanged |= v != c.fold;
    c.fold = v;
  }
  if (o["tip"].is<bool>()) {
    bool v = o["tip"];
    ledsChanged |= v != c.tip;
    c.tip = v;
  }
  // A fold that does not fit on the strip falls back to a plain strip.
  if (2 * c.fold + (c.tip ? 1 : 0) > c.leds) {
    c.fold = 0;
    c.tip = false;
  }
  if (o["pin"].is<int>()) {
    uint8_t v = o["pin"].as<int>();
    // GPIOs a D1 mini exposes and that can drive a strip.
    if (v == 0 || v == 1 || v == 2 || v == 3 || v == 4 || v == 5 || (v >= 12 && v <= 15)) {
      ledsChanged |= v != c.pin;
      c.pin = v;
    }
  }
  if (o["order"].is<const char*>()) {
    bool v = strcmp(o["order"].as<const char*>(), "RGB") == 0;
    ledsChanged |= v != c.rgbOrder;
    c.rgbOrder = v;
  }
  if (o["maxBri"].is<int>()) c.maxBri = constrain(o["maxBri"].as<int>(), 10, 255);
  auto str = [&](const char* key, String& field, size_t minLen, bool needsRestart) {
    if (!o[key].is<const char*>()) return;
    String v = o[key].as<const char*>();
    if (v.length() > 63 || (v.length() > 0 && v.length() < minLen)) return;
    if (v != field && needsRestart) restart = true;
    field = v;
  };
  str("apSsid", c.apSsid, 1, true);
  str("apPass", c.apPass, 8, true);  // WPA2 needs 8 characters; empty means open
  str("staSsid", c.staSsid, 1, true);
  str("staPass", c.staPass, 1, true);
  str("otaPass", c.otaPass, 4, true);
  if (c.apSsid.length() == 0) c.apSsid = "Kupferstab";
  return restart;
}
