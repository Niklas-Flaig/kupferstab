#include "effects.h"

#include <Adafruit_NeoPixel.h>

#include <vector>

namespace effects {

namespace {

constexpr uint32_t kFrameMs = 16;  // ~60 frames per second
constexpr uint32_t kTestMs = 1800;
constexpr uint32_t kTestColor = 0xFF6A1F;

Adafruit_NeoPixel strip;
std::vector<uint8_t> level;  // per-pixel scratch: sparkle brightness, fire heat
uint32_t lastFrame = 0;
uint32_t testUntil = 0;
uint32_t testStart = 0;
uint32_t phase = 0;  // advances with speed, in 1/65536 of a cycle

uint8_t r8(uint32_t c) { return (c >> 16) & 0xFF; }
uint8_t g8(uint32_t c) { return (c >> 8) & 0xFF; }
uint8_t b8(uint32_t c) { return c & 0xFF; }

uint32_t scale(uint32_t c, uint8_t f) {
  return Adafruit_NeoPixel::Color(r8(c) * f / 255, g8(c) * f / 255, b8(c) * f / 255);
}

uint32_t blend(uint32_t a, uint32_t b, uint8_t t) {
  auto mix = [t](uint8_t x, uint8_t y) { return uint8_t(x + (int(y) - int(x)) * t / 255); };
  return Adafruit_NeoPixel::Color(mix(r8(a), r8(b)), mix(g8(a), g8(b)), mix(b8(a), b8(b)));
}

// Black, red, yellow, white along 0..255, like glowing embers.
uint32_t heatColor(uint8_t h) {
  uint8_t t = (h * 191) / 255;
  uint8_t ramp = (t & 0x3F) << 2;
  if (t > 0x80) return Adafruit_NeoPixel::Color(255, 255, ramp);
  if (t > 0x40) return Adafruit_NeoPixel::Color(255, ramp, 0);
  return Adafruit_NeoPixel::Color(ramp, 0, 0);
}

void render(uint32_t now) {
  const LightState& s = store.state;
  const uint16_t n = strip.numPixels();
  // speed 1..100 -> 0.05 .. 2 cycles per second
  const uint32_t step = (uint32_t)(65536.0f * (0.05f + 1.95f * (s.speed - 1) / 99.0f) * kFrameMs / 1000.0f);
  phase += step;

  switch (s.effect) {
    case 0:  // solid
      strip.fill(s.color);
      break;
    case 1:  // rainbow along the staff
      for (uint16_t i = 0; i < n; i++)
        strip.setPixelColor(i, strip.gamma32(strip.ColorHSV((phase + i * 65536UL / n) & 0xFFFF)));
      break;
    case 2: {  // breathe
      float x = (sinf((phase & 0xFFFF) * 2.0f * PI / 65536.0f) + 1.0f) / 2.0f;
      strip.fill(scale(s.color, 8 + uint8_t(x * x * 247)));
      break;
    }
    case 3: {  // chase: a comet with a fading tail over color2
      uint16_t len = max<uint16_t>(3, n / 6);
      uint32_t head = ((phase & 0xFFFF) * (uint32_t)(n + len)) >> 16;
      for (uint16_t i = 0; i < n; i++) {
        int d = int(head) - int(i);
        if (d >= 0 && d < len) strip.setPixelColor(i, blend(s.color2, s.color, 255 - d * 255 / len));
        else strip.setPixelColor(i, s.color2);
      }
      break;
    }
    case 4: {  // sparkle
      uint8_t fade = 4 + s.speed / 6;
      uint16_t chance = 2 + s.speed / 4;
      for (uint16_t i = 0; i < n; i++) {
        level[i] = level[i] > fade ? level[i] - fade : 0;
        if (random(1000) < chance) level[i] = 255;
        strip.setPixelColor(i, blend(s.color2, s.color, level[i]));
      }
      break;
    }
    case 5: {  // fire, rising from pixel 0
      uint8_t cooling = 40 + (100 - s.speed) / 2;
      for (uint16_t i = 0; i < n; i++) {
        uint8_t cool = random(0, (cooling * 10) / n + 2);
        level[i] = level[i] > cool ? level[i] - cool : 0;
      }
      for (uint16_t i = n - 1; i >= 2; i--) level[i] = (level[i - 1] + level[i - 2] * 2) / 3;
      if (random(255) < 90 + s.speed) {
        uint16_t y = random(min<uint16_t>(7, n));
        level[y] = min(255, level[y] + (int)random(160, 255));
      }
      for (uint16_t i = 0; i < n; i++) strip.setPixelColor(i, heatColor(level[i]));
      break;
    }
    case 6:  // colorloop: the whole staff in one colour that wanders
      strip.fill(strip.gamma32(strip.ColorHSV(phase & 0xFFFF)));
      break;
    case 7: {  // strobe
      uint32_t period = 1000 / (1 + s.speed / 5);
      strip.fill((now % period) < 30 ? s.color : 0);
      break;
    }
  }
}

}  // namespace

void reconfigure() {
  const Config& c = store.config;
  strip.updateType((c.rgbOrder ? NEO_RGB : NEO_GRB) + NEO_KHZ800);
  strip.updateLength(c.leds);
  strip.setPin(c.pin);
  strip.begin();
  level.assign(c.leds, 0);
  strip.clear();
  strip.show();
}

void runTest() {
  testStart = millis();
  testUntil = testStart + kTestMs;
}

void begin() {
  reconfigure();
  runTest();
}

void blackout() {
  strip.clear();
  strip.show();
  testUntil = 0;
}

void loop() {
  uint32_t now = millis();
  if (now - lastFrame < kFrameMs) return;
  lastFrame = now;
  const LightState& s = store.state;

  if (testUntil && (int32_t)(testUntil - now) > 0) {
    uint16_t n = strip.numPixels();
    uint32_t lit = (uint32_t)(now - testStart) * n / kTestMs + 1;
    strip.setBrightness(min<uint8_t>(80, store.config.maxBri));
    for (uint16_t i = 0; i < n; i++) strip.setPixelColor(i, i < lit ? kTestColor : 0);
    strip.show();
    return;
  }
  testUntil = 0;

  if (!s.on || s.bri == 0) {
    strip.clear();
  } else {
    strip.setBrightness(min(s.bri, store.config.maxBri));
    render(now);
  }
  strip.show();
}

}  // namespace effects
