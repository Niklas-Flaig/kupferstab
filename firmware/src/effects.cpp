#include "effects.h"

#include <Adafruit_NeoPixel.h>

#include <vector>

// The four effects, ported from the simulation the scenes were designed in. They render into
// a buffer of logical pixels (foot to tip, see config.h); put() spreads them onto both layers
// of the fold. The ESP8266 has no FPU, so per-pixel work avoids libm: a sine table and an
// integer hash stand in for sinf and random noise.

namespace effects {

namespace {

constexpr uint32_t kFrameMs = 16;       // ~60 frames per second
constexpr uint32_t kStrobeFrameMs = 8;  // finer steps while flashing, so 13 Hz stays even
constexpr uint32_t kTestMs = 1800;
constexpr uint32_t kTestColor = 0xFF6A1F;
constexpr float kTau = 6.2831853f;
constexpr float kStrobeHz = 13;
constexpr float kStrobeDuty = 0.22f;

struct Rgb {
  float r, g, b;
};
struct Hs {
  float h, s;
};

Adafruit_NeoPixel strip;
std::vector<Rgb> frame;    // logical pixels
std::vector<Rgb> overlay;  // physical pixels, added on top (sparks that follow the strip)
uint16_t n = 1;          // logical pixel count
uint16_t foldStart = 0;  // first logical pixel of the fold
uint32_t lastFrame = 0;
uint32_t testUntil = 0;
uint32_t testStart = 0;
float t = 0;  // seconds of effect time

// The strobe run: from the moment the effect turns to strobo until it returns by itself.
uint8_t lastEffect = 255;
uint32_t runStart = 0;
float beat = 0;
bool flashing = false;
bool changed = false;

// ---------- maths ----------

float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
float smooth(float x) { return x * x * (3 - 2 * x); }
float frac(float x) { return x - floorf(x); }
float frand() { return random(100000) / 100000.0f; }

float sinTable[257];
void initSin() {
  for (int i = 0; i <= 256; i++) sinTable[i] = sinf(kTau * i / 256);
}
float fsin(float x) {
  float f = frac(x / kTau) * 256;
  int i = (int)f;
  return sinTable[i] + (sinTable[i + 1] - sinTable[i]) * (f - i);
}
float fcos(float x) { return fsin(x + kTau / 4); }
float gauss(float d) {
  float q = d * d;
  return q > 9 ? 0 : expf(-q);
}
float breathe(float time, float period) { return smooth(0.5f - 0.5f * fcos(kTau * time / period)); }

float hash1(int32_t v) {
  uint32_t x = (uint32_t)v * 0x27d4eb2dU;
  x ^= x >> 15;
  x *= 0x2c1b3c6dU;
  x ^= x >> 12;
  x *= 0x297a2d39U;
  x ^= x >> 15;
  return (x & 0xFFFFFF) / 16777216.0f;
}
float noise2(float x, float y) {
  float fx = floorf(x), fy = floorf(y);
  int32_t xi = (int32_t)fx, yi = (int32_t)fy;
  float u = smooth(x - fx), v = smooth(y - fy);
  auto h = [](int32_t a, int32_t b) { return hash1(a + b * 57); };
  float a = h(xi, yi), b = h(xi + 1, yi), c = h(xi, yi + 1), d = h(xi + 1, yi + 1);
  return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
float fbm(float x, float y) { return 0.62f * noise2(x, y) + 0.38f * noise2(x * 2.13f + 5.2f, y * 2.07f + 1.3f); }

Rgb hsv(float h, float s, float v) {
  h = fmodf(h, 360);
  if (h < 0) h += 360;
  float c = v * s, x = c * (1 - fabsf(fmodf(h / 60, 2) - 1)), m = v - c;
  float r, g, b;
  if (h < 60) r = c, g = x, b = 0;
  else if (h < 120) r = x, g = c, b = 0;
  else if (h < 180) r = 0, g = c, b = x;
  else if (h < 240) r = 0, g = x, b = c;
  else if (h < 300) r = x, g = 0, b = c;
  else r = c, g = 0, b = x;
  return {r + m, g + m, b + m};
}
Hs toHs(uint32_t c) {
  float r = ((c >> 16) & 255) / 255.0f, g = ((c >> 8) & 255) / 255.0f, b = (c & 255) / 255.0f;
  float mx = max(r, max(g, b)), mn = min(r, min(g, b)), d = mx - mn, h = 0;
  if (d > 0) {
    if (mx == r) h = fmodf((g - b) / d, 6);
    else if (mx == g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h *= 60;
    if (h < 0) h += 360;
  }
  return {h, max(0.5f, mx > 0 ? d / mx : 0)};
}
Rgb tint(Rgb c, float v, float white) {
  return {clamp01((c.r + (1 - c.r) * white) * v), clamp01((c.g + (1 - c.g) * white) * v),
          clamp01((c.b + (1 - c.b) * white) * v)};
}
Rgb add(Rgb a, Rgb b, float f) { return {clamp01(a.r + b.r * f), clamp01(a.g + b.g * f), clamp01(a.b + b.b * f)}; }
Rgb mix(Rgb a, Rgb b, float f) { return {a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f}; }

// ---------- palette ----------

Hs pal[kPaletteMax];
uint8_t palN = 1;
float palPeriod = 12, palSpread = 0;

void loadPalette(const FxSettings& f) {
  palN = max<uint8_t>(1, f.paletteLen);
  for (uint8_t i = 0; i < palN; i++) pal[i] = toHs(f.palette[i]);
  palPeriod = f.period;
  palSpread = f.spread;
}

// The colours take turns, each held a while and then blended into the next; the spread lays
// several of them along the staff at once.
Hs palAt(float x) {
  if (palN == 1) return pal[0];
  float pos = t / palPeriod + x * (palSpread / 100) * (palN - 1);
  uint8_t i = (uint32_t)floorf(pos) % palN, j = (i + 1) % palN;
  float f = smooth(clamp01((frac(pos) - 0.45f) / 0.55f));
  float dh = pal[j].h - pal[i].h;
  if (dh > 180) dh -= 360;
  if (dh < -180) dh += 360;
  return {pal[i].h + dh * f, pal[i].s + (pal[j].s - pal[i].s) * f};
}
Hs palPick() {
  Hs c = pal[random(palN)];
  if (palN == 1) c.h += (frand() - 0.5f) * 30;
  return c;
}

float xOf(uint16_t k) { return n > 1 ? (float)k / (n - 1) : 0; }

// ---------- Atem & Welle ----------

void renderAtem(const FxSettings& f) {
  const float breath = f.p[0], wave = f.p[1], width = f.p[2] / 100;
  const float b = breathe(t, breath);
  const float c = -2 * width + frac(t / wave) * (1 + 4 * width);
  for (uint16_t k = 0; k < n; k++) {
    const float x = xOf(k);
    const Hs p = palAt(x);
    if (f.variant == 0) {
      float g = gauss((x - c) / width);
      frame[k] = hsv(p.h + g * 14, p.s * (1 - 0.35f * g), clamp01(0.1f + 0.42f * b + g * (0.45f + 0.25f * b)));
    } else if (f.variant == 1) {
      float a1 = fsin(kTau * (x * 1.6f - t / wave));
      float a2 = fsin(kTau * (x * 3.1f + t / (wave * 1.7f)));
      float a3 = fsin(kTau * (x * 0.7f - t / (wave * 2.9f)));
      float sum = clamp01((a1 * 0.5f + a2 * 0.3f + a3 * 0.4f + 1.2f) / 2.4f);
      float cap = clamp01((sum - 0.8f) * 5);
      frame[k] = hsv(p.h + 18 * a2, p.s * (1 - 0.6f * cap),
                     clamp01((0.1f + 0.9f * sum * sum) * (0.45f + 0.55f * b) + cap * 0.25f));
    } else {
      float v = clamp01((fbm(x * (1.5f + (30 - f.p[2]) / 6) - t / (wave * 2.2f), t * 0.07f) - 0.18f) / 0.64f);
      frame[k] = hsv(p.h + (v - 0.5f) * 50, p.s * (0.9f + 0.1f * v), (0.05f + 0.95f * v * sqrtf(v)) * (0.4f + 0.6f * b));
    }
  }
}

// ---------- Glut, Polarlicht, Funkenflug ----------

// Sparks fly along the strip itself, not mirrored: the end of the staff is bent, so pixel
// order there is the way up. A spark runs `pos` 0..1 along its path from `from` to `to`.
struct Spark {
  uint16_t from, to;
  float pos, v, life;
};
std::vector<Spark> sparks;
float peakFlash = 0;

uint16_t tipPixel() {
  const Config& c = store.config;
  return c.fold && c.tip ? c.leds - c.fold - 1 : c.leds - 1;
}

void moveSparks(float dt, float drag, float fade, bool flashAtEnd) {
  for (size_t i = 0; i < sparks.size();) {
    Spark& q = sparks[i];
    q.pos += q.v * dt;
    q.v *= powf(drag, dt);
    q.life -= dt * fade;
    if (q.pos >= 1 && flashAtEnd) peakFlash = 1;
    if (q.pos >= 1 || q.life <= 0) {
      sparks[i] = sparks.back();
      sparks.pop_back();
    } else {
      i++;
    }
  }
}

void drawSparks(Rgb c) {
  for (const Spark& q : sparks) {
    float at = q.from + (float(q.to) - q.from) * q.pos;
    for (int d = -3; d <= 3; d++) {
      int px = (int)roundf(at) + d;
      if (px < 0 || px >= (int)overlay.size()) continue;
      overlay[px] = add(overlay[px], c, gauss((px - at) / 1.4f) * q.life);
    }
  }
}

void renderGlut(uint8_t effect, float dt, float tempo) {
  const float sp = 0.25f + tempo / 100 * 1.8f;
  const uint16_t leds = store.config.leds;
  peakFlash = max(0.0f, peakFlash - dt * 1.6f);

  if (effect == kGlut) {
    // now and then a single spark leaves the tip and fades out along the way down the fold
    const uint16_t from = tipPixel(), to = leds - 1;
    if (to > from && sparks.size() < 4 && frand() < 0.5f * sp * dt)
      sparks.push_back({from, to, 0, (0.5f + frand() * 0.4f) * sp, 1});
    for (Spark& q : sparks) q.life = min(q.life, 1 - q.pos);  // fades as it flies
    moveSparks(dt, 0.6f, 0.4f, false);
  } else if (effect == kFunken) {
    // sparks rise from the foot and from the tip to the highest point, which flashes on arrival
    const uint16_t peak = min<uint16_t>(kHighestPixel, leds - 1), tip = tipPixel();
    if (sparks.size() < 24 && frand() < 2.8f * sp * dt) {
      bool fromTip = tip > peak && random(2);
      sparks.push_back({fromTip ? tip : (uint16_t)0, peak, 0, (0.35f + frand() * 0.5f) * sp, 1});
    }
    moveSparks(dt, 0.72f, 0.45f, true);
  } else {
    sparks.clear();
  }

  for (uint16_t k = 0; k < n; k++) {
    const float x = xOf(k);
    const Hs p = palAt(x);
    float v, h = p.h, s = p.s;
    if (effect == kGlut) {
      float heat = clamp01((fbm(x * 7.5f, t * 0.6f * sp) - 0.2f) / 0.6f);
      float fl = noise2(k * 3.1f, t * 5 * sp);
      v = 0.06f + 0.8f * heat * sqrtf(heat) * (0.8f + 0.25f * fl);
      h = p.h + (heat - 0.6f) * 30;
      s = p.s * (1 - max(0.0f, heat - 0.85f) * 2);
    } else if (effect == kPolar) {
      float cur = fbm(x * 2.2f + fsin(t * 0.1f * sp) * 0.6f, t * 0.16f * sp);
      float band = noise2(x * 4.5f - t * 0.25f * sp, 7.3f);
      float c = clamp01((cur - 0.3f) * 2);
      v = 0.04f + 0.9f * c * sqrtf(c);
      h = p.h + 70 * (band - 0.5f) + 35 * x;
      s = 0.85f;
    } else {
      v = 0.03f;
      h = p.h - 8;
    }
    frame[k] = hsv(h, s, clamp01(v));
  }

  const Hs p = palAt(0.5f);
  if (effect == kFunken) {
    // embers glow where the sparks start
    const uint16_t tip = tipPixel(), peak = min<uint16_t>(kHighestPixel, leds - 1);
    const Rgb ember = hsv(p.h - 8, p.s, 1);
    for (uint16_t q = 0; q < leds; q++) {
      float d = min((float)q, fabsf((float)q - tip));
      float g = 0.4f * expf(-d / 6) * (0.75f + 0.25f * noise2(q * 0.7f, t * 3 * sp));
      overlay[q] = add(overlay[q], ember, g);
    }
    overlay[peak] = add(overlay[peak], hsv(p.h + 22, p.s * 0.55f, 1), peakFlash);
  }
  drawSparks(hsv(p.h + 22, p.s * 0.55f, 1));
}

// ---------- Mosaik ----------

struct Tile {
  Rgb from, to;
  float f, next;
};
struct Twinkle {
  float lv;
  Rgb c;
};
struct Shard {
  uint16_t a, len;
  Rgb c;
  float life, decay;
};
std::vector<Tile> tiles;
std::vector<Twinkle> twinkles;
std::vector<Shard> shards;

void renderMosaik(const FxSettings& f, float dt) {
  const float sp = 0.25f + f.p[0] / 100 * 1.8f, dens = f.p[1] / 100;
  if (f.variant == 0) {
    const uint16_t size = 3, count = (n + size - 1) / size;
    if (tiles.size() != count) {
      tiles.resize(count);
      for (Tile& tl : tiles) {
        Hs c = palPick();
        tl.from = tl.to = hsv(c.h, c.s, 0.25f + frand() * 0.6f);
        tl.f = 1;
        tl.next = frand() * 3;
      }
    }
    for (Tile& tl : tiles) {
      tl.next -= dt * sp;
      if (tl.next <= 0) {
        Hs c = palPick();
        tl.from = mix(tl.from, tl.to, smooth(tl.f));
        tl.to = hsv(c.h, c.s, 0.15f + frand() * 0.75f);
        tl.f = 0;
        tl.next = (0.6f + frand() * 4) * (1.4f - dens);
      }
      tl.f = min(1.0f, tl.f + dt * 1.3f * sp);
    }
    for (uint16_t k = 0; k < n; k++) {
      const Tile& tl = tiles[k / size];
      frame[k] = mix(tl.from, tl.to, smooth(tl.f));
    }
  } else if (f.variant == 1) {
    if (twinkles.size() != n) twinkles.assign(n, Twinkle{0, {0, 0, 0}});
    const float decay = expf(-dt * 2.2f * sp);
    for (uint16_t k = 0; k < n; k++) {
      Twinkle& tw = twinkles[k];
      if (frand() < dt * sp * (0.05f + 0.6f * dens)) {
        Hs c = palPick();
        tw.lv = 1;
        tw.c = hsv(c.h, c.s * 0.9f, 1);
      }
      tw.lv *= decay;
      const Hs g = palAt(xOf(k));
      frame[k] = add(hsv(g.h, g.s, 0.05f), tw.c, tw.lv * sqrtf(tw.lv));
    }
  } else {
    if (shards.size() < 40 && frand() < dt * sp * (0.6f + 4 * dens)) {
      Hs c = palPick();
      shards.push_back({(uint16_t)random(n), (uint16_t)(2 + random(9)), hsv(c.h, c.s, 1), 1, 0.4f + frand() * 0.6f});
    }
    for (size_t i = 0; i < shards.size();) {
      shards[i].life -= dt * shards[i].decay * sp;
      if (shards[i].life <= 0) {
        shards[i] = shards.back();
        shards.pop_back();
      } else {
        i++;
      }
    }
    for (uint16_t k = 0; k < n; k++) {
      const Hs g = palAt(xOf(k));
      frame[k] = hsv(g.h, g.s, 0.03f);
    }
    for (const Shard& q : shards)
      for (uint16_t k = q.a; k < q.a + q.len && k < n; k++) frame[k] = add(frame[k], q.c, q.life * q.life);
  }
}

// ---------- Strobo ----------

// Charge and build-up are one continuous rise: the charge climbs from the foot to the tip,
// speeding up and easing out, while everything lit pulses faster and brighter until the pulses
// are the strobe. Then the full strobe, a quiet afterglow, and back to the effect before.
float runTotal(const FxSettings& f) { return f.p[0] + f.p[1] + f.p[2] + f.p[3]; }

void renderStrobo(const FxSettings& f, float dt, float e) {
  const float charge = f.p[0], ramp = f.p[0] + f.p[1], hold = f.p[2];
  const float wm = f.white ? 1 : 0;
  const float f0 = 0.3f, r = kStrobeHz / f0, lnr = logf(r);
  // pulse phase over the ramp; its rate rises from f0 to exactly kStrobeHz at the end
  auto phiAt = [&](float s) { return f0 * ramp * (powf(r, s / ramp) - 1) / lnr; };
  auto flash = [](float phase) { return frac(phase) < kStrobeDuty ? 1.0f : 0.0f; };
  auto fill = [&](float v, float white) {
    for (uint16_t k = 0; k < n; k++) {
      Hs g = palAt(xOf(k));
      frame[k] = tint(hsv(g.h, g.s, 1), v, white);
    }
  };
  flashing = false;

  if (e >= ramp + hold) {
    const float s = e - ramp - hold;
    return fill(clamp01(s / 0.8f) * (0.07f + 0.1f * breathe(s, 3)), 0);
  }
  if (e >= ramp) {
    flashing = true;
    const float phase = f.variant == 1 ? kStrobeHz * e : phiAt(ramp) + kStrobeHz * (e - ramp);
    return fill(flash(phase), wm);
  }

  const float u = ramp > 0 ? clamp01(e / ramp) : 1;
  float v, white;
  if (f.variant == 1) {
    beat += 50 * powf(420.0f / 50, u) / 60 * dt;
    if (u > 0.75f) {
      v = flash(kStrobeHz * e);
      flashing = true;
    } else {
      float fb = frac(beat);
      v = clamp01(0.06f + 0.9f * gauss((fb - 0.06f) / 0.07f) + 0.6f * gauss((fb - 0.32f) / 0.07f));
    }
    white = wm * smooth(clamp01((u - 0.3f) / 0.45f));
  } else {
    const float fr = frac(phiAt(e));
    if (u < 0.7f) {
      v = (0.35f + 0.65f * (u / 0.7f)) * powf(0.5f - 0.5f * fcos(kTau * fr), 1 + 2 * u);
    } else {
      v = fr < 0.5f - (0.5f - kStrobeDuty) * ((u - 0.7f) / 0.3f) ? 1 : 0;
      flashing = true;
    }
    white = wm * smooth(clamp01((u - 0.4f) / 0.6f));
  }
  const float steady = 0.3f * (1 - smooth(clamp01(u / 0.45f)));
  const float level = steady + v * (1 - steady);
  const float c = charge > 0 ? clamp01(e / charge) : 1;
  const float front = smooth(powf(c, 1.25f)) * (n - 1);
  const float headAmp = 0.85f * (1 - smooth(clamp01((c - 0.55f) / 0.45f)));
  for (uint16_t k = 0; k < n; k++) {
    Hs g = palAt(xOf(k));
    float lit = clamp01(front - k + 1);
    float vv = lit * level + (1 - lit) * 0.012f + headAmp * gauss((k - front) / 2.2f);
    frame[k] = tint(hsv(g.h, g.s, 1), clamp01(vv), white);
  }
}

// ---------- output ----------

void put(uint16_t i, uint32_t c) {
  const Config& cfg = store.config;
  strip.setPixelColor(i, c);
  if (i >= foldStart && i < foldStart + cfg.fold) strip.setPixelColor(cfg.leds - 1 - (i - foldStart), c);
}

// The effects think in perceived brightness; LEDs are linear, so square it. Faint glows keep
// at least the lowest step instead of going black.
uint8_t level(float v, float scale) {
  v = clamp01(v);
  float o = v * v * 255 * scale;
  if (o < 1 && v > 0.03f) return 1;
  return (uint8_t)(o + 0.5f);
}

}  // namespace

void reconfigure() {
  const Config& c = store.config;
  strip.updateType((c.rgbOrder ? NEO_RGB : NEO_GRB) + NEO_KHZ800);
  strip.updateLength(c.leds);
  strip.setPin(c.pin);
  strip.begin();
  n = max<uint16_t>(1, logicalLeds(c));
  foldStart = c.leds - 2 * c.fold - (c.tip ? 1 : 0);
  frame.assign(n, Rgb{0, 0, 0});
  overlay.assign(c.leds, Rgb{0, 0, 0});
  tiles.clear();
  twinkles.clear();
  shards.clear();
  sparks.clear();
  strip.clear();
  strip.show();
}

void runTest() {
  testStart = millis();
  testUntil = testStart + kTestMs;
}

void begin() {
  initSin();
  randomSeed(ESP.getCycleCount());
  reconfigure();
  runTest();
}

void blackout() {
  strip.clear();
  strip.show();
  testUntil = 0;
}

void restartRun() {
  runStart = millis();
  beat = 0;
}

float runElapsed() {
  return store.state.effect == kStrobo && lastEffect == kStrobo ? (millis() - runStart) / 1000.0f : -1;
}

bool takeChange() {
  bool c = changed;
  changed = false;
  return c;
}

void loop() {
  uint32_t now = millis();
  if (now - lastFrame < (flashing ? kStrobeFrameMs : kFrameMs)) return;
  const float dt = min(0.05f, (now - lastFrame) / 1000.0f);
  lastFrame = now;
  LightState& s = store.state;

  if (testUntil && (int32_t)(testUntil - now) > 0) {
    // Over the logical pixels: with the layout right, both layers of the fold climb together
    // and the tip lights last.
    uint32_t lit = (uint32_t)(now - testStart) * n / kTestMs + 1;
    strip.setBrightness(min<uint8_t>(80, store.config.maxBri));
    for (uint16_t i = 0; i < n; i++) put(i, i < lit ? kTestColor : 0);
    strip.show();
    return;
  }
  testUntil = 0;

  if (s.effect >= kEffectCount) s.effect = kAtem;
  if (s.effect != lastEffect) {
    if (s.effect == kStrobo) {
      store.back = lastEffect < kEffectCount ? lastEffect : (uint8_t)kAtem;
      restartRun();
    }
    lastEffect = s.effect;
  }
  t += dt;

  if (!s.on || s.bri == 0) {
    flashing = false;
    strip.clear();
    strip.show();
    return;
  }

  const FxSettings& f = s.fx[s.effect];
  loadPalette(f);
  for (Rgb& o : overlay) o = {0, 0, 0};
  switch (s.effect) {
    case kAtem: renderAtem(f); break;
    case kGlut:
    case kPolar:
    case kFunken: renderGlut(s.effect, dt, f.p[0]); break;
    case kMosaik: renderMosaik(f, dt); break;
    case kStrobo: {
      float e = (now - runStart) / 1000.0f;
      if (e >= runTotal(f)) {
        s.effect = store.back;
        storeTouch();
        changed = true;
      }
      renderStrobo(f, dt, e);
      break;
    }
  }

  strip.setBrightness(255);
  const float scale = min(s.bri, store.config.maxBri) / 255.0f;
  const uint16_t leds = store.config.leds;
  for (uint16_t q = 0; q < leds; q++) {
    // the way down the fold shows the same logical pixel as the way up beside it
    const uint16_t k = q < n ? q : foldStart + (leds - 1 - q);
    const Rgb c = add(frame[k], overlay[q], 1);
    strip.setPixelColor(q, Adafruit_NeoPixel::Color(level(c.r, scale), level(c.g, scale), level(c.b, scale)));
  }
  strip.show();
}

}  // namespace effects
