#include "config.h"

#include <EEPROM.h>

const char* const kEffectIds[kEffectCount] = {
    "solid", "rainbow", "breathe", "chase", "sparkle", "fire", "colorloop", "strobe"};

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
  stateToJson(store.state, doc["state"].to<JsonObject>());
  JsonArray scenes = doc["scenes"].to<JsonArray>();
  for (uint8_t i = 0; i < kSceneCount; i++) {
    if (store.sceneUsed[i]) stateToJson(store.scenes[i], scenes.add<JsonObject>());
    else scenes.add(nullptr);
  }
  doc["lastStaIp"] = store.lastStaIp;
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
  configFromJson(store.config, doc["config"], ignored, ignored);
  stateFromJson(store.state, doc["state"]);
  JsonArrayConst scenes = doc["scenes"];
  for (uint8_t i = 0; i < kSceneCount && i < scenes.size(); i++) {
    store.sceneUsed[i] = scenes[i].is<JsonObjectConst>();
    if (store.sceneUsed[i]) stateFromJson(store.scenes[i], scenes[i]);
  }
  store.lastStaIp = doc["lastStaIp"] | "";
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
    if (strcmp(id, kEffectIds[i]) == 0) return i;
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

void stateToJson(const LightState& s, JsonObject o) {
  o["on"] = s.on;
  o["bri"] = s.bri;
  o["effect"] = kEffectIds[s.effect < kEffectCount ? s.effect : 0];
  o["speed"] = s.speed;
  o["color"] = colorToHex(s.color);
  o["color2"] = colorToHex(s.color2);
}

void stateFromJson(LightState& s, JsonObjectConst o) {
  if (o.isNull()) return;
  if (o["on"].is<bool>()) s.on = o["on"];
  if (o["bri"].is<int>()) s.bri = constrain(o["bri"].as<int>(), 0, 255);
  if (o["effect"].is<const char*>()) {
    int i = effectIndex(o["effect"]);
    if (i >= 0) s.effect = i;
  }
  if (o["speed"].is<int>()) s.speed = constrain(o["speed"].as<int>(), 1, 100);
  uint32_t c;
  if (hexToColor(o["color"], c)) s.color = c;
  if (hexToColor(o["color2"], c)) s.color2 = c;
}

void configToJson(const Config& c, JsonObject o) {
  o["leds"] = c.leds;
  o["pin"] = c.pin;
  o["order"] = c.rgbOrder ? "RGB" : "GRB";
  o["maxBri"] = c.maxBri;
  o["apSsid"] = c.apSsid;
  o["staSsid"] = c.staSsid;
}

bool configFromJson(Config& c, JsonObjectConst o, bool& ledsChanged, bool& staChanged) {
  ledsChanged = false;
  bool restart = false;
  if (o.isNull()) return false;
  if (o["leds"].is<int>()) {
    uint16_t v = constrain(o["leds"].as<int>(), 1, 600);
    ledsChanged |= v != c.leds;
    c.leds = v;
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
  auto str = [&](const char* key, String& field, size_t minLen, bool& changed) {
    if (!o[key].is<const char*>()) return;
    String v = o[key].as<const char*>();
    if (v.length() > 63 || (v.length() > 0 && v.length() < minLen)) return;
    if (v != field) changed = true;
    field = v;
  };
  str("apSsid", c.apSsid, 1, restart);
  str("apPass", c.apPass, 8, restart);  // WPA2 needs 8 characters; empty means open
  str("staSsid", c.staSsid, 1, staChanged);
  str("staPass", c.staPass, 1, staChanged);
  str("otaPass", c.otaPass, 4, restart);
  if (c.apSsid.length() == 0) c.apSsid = "Kupferstab";
  return restart;
}
