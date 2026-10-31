// The messages between the app and the staff; mirrors firmware/src/config.cpp
// and web.cpp. See docs/protocol.md.

// The four effects and what can be set on each; mirrors kEffects in firmware/src/config.cpp.
export const EFFECTS = [
  {
    id: "atem",
    name: "Atem & Welle",
    variants: ["Atem + Welle", "Gezeiten", "Nebel"],
    params: [
      { key: "breath", label: "Atem-Dauer", min: 3, max: 14, step: 0.5, unit: "s" },
      { key: "wave", label: "Welle alle", min: 1.5, max: 12, step: 0.5, unit: "s" },
      { key: "width", label: "Wellen-Breite", min: 4, max: 30, step: 1, unit: "%" },
    ],
  },
  {
    id: "strobo",
    name: "Strobo",
    variants: ["Immer schneller", "Herzschlag"],
    params: [
      { key: "charge", label: "Aufladen bis zur Spitze", min: 0, max: 20, step: 0.5, unit: "s" },
      { key: "build", label: "Aufbau nach dem Aufladen", min: 3, max: 20, step: 1, unit: "s" },
      { key: "hold", label: "Strobo voll an", min: 0, max: 20, step: 0.5, unit: "s" },
      { key: "after", label: "Ausklang", min: 2, max: 15, step: 1, unit: "s" },
    ],
  },
  { id: "glut", name: "Glut", variants: [], params: [{ key: "tempo", label: "Tempo", min: 5, max: 100, step: 1, unit: "%" }] },
  { id: "polarlicht", name: "Polarlicht", variants: [], params: [{ key: "tempo", label: "Tempo", min: 5, max: 100, step: 1, unit: "%" }] },
  { id: "funkenflug", name: "Funkenflug", variants: [], params: [{ key: "tempo", label: "Tempo", min: 5, max: 100, step: 1, unit: "%" }] },
  {
    id: "mosaik",
    name: "Mosaik",
    variants: ["Kacheln", "Funkeln", "Scherben"],
    params: [
      { key: "tempo", label: "Tempo", min: 5, max: 100, step: 1, unit: "%" },
      { key: "density", label: "Dichte", min: 5, max: 100, step: 1, unit: "%" },
    ],
  },
] as const;

export type EffectId = (typeof EFFECTS)[number]["id"];
export const effectDef = (id: EffectId) => EFFECTS.find((e) => e.id === id)!;

export const PALETTE_MAX = 6;
export const PALETTE_PRESETS = [
  { name: "Kupfer", colors: ["#FF6A1F"] },
  { name: "Mystisch", colors: ["#4A5BFF", "#14C9A8", "#B04CFF"] },
  { name: "Tiefsee", colors: ["#14C9A8", "#2A5BFF"] },
  { name: "Glut", colors: ["#FF6A1F", "#FF2A10", "#FFB030"] },
  { name: "Party", colors: ["#FF2A6D", "#B04CFF", "#2A5BFF", "#14C9A8"] },
];

/** One effect's settings: variant, colour palette, and its own parameters in `p`. */
export interface Fx {
  variant: number;
  palette: string[]; // 1..6 × #RRGGBB
  period: number; // seconds per palette colour, 3..60
  spread: number; // 0..100 %
  p: Record<string, number>;
  white?: boolean; // strobo: flashes in white
}

export interface LightState {
  on: boolean;
  bri: number; // 0..255
  effect: EffectId;
  fx: Record<EffectId, Fx>;
}

/** A saved scene carries only its own effect's settings. */
export interface SceneState {
  on: boolean;
  bri: number;
  effect: EffectId;
  fx: Partial<Record<EffectId, Fx>>;
}

export type FxPatch = Partial<Omit<Fx, "p">> & { p?: Record<string, number> };
export interface StatePatch {
  on?: boolean;
  bri?: number;
  effect?: EffectId;
  fx?: Partial<Record<EffectId, FxPatch>>;
}

/** The strobe in progress: seconds since it started and the effect it returns to. */
export interface Run {
  t: number;
  back: EffectId;
}

export interface Config {
  leds: number; // physical pixels
  fold: number; // pixels per layer of the fold at the top, 0 for a plain strip
  tip: boolean; // one pixel between the two layers
  pin: number;
  order: "GRB" | "RGB";
  maxBri: number;
  apSsid: string;
  staSsid: string;
}

export interface ConfigPatch extends Partial<Config> {
  apPass?: string;
  staPass?: string;
  otaPass?: string;
}

export interface Info {
  fw: string;
  heap: number;
  uptime: number;
  apIp: string;
  staIp: string;
  update: string; // a newer version on GitHub, empty when none
  clients: number;
  app: boolean;
}

export interface Snapshot {
  t: "state";
  state: LightState;
  scenes: (SceneState | null)[];
  run: Run | null;
  config: Config;
  info: Info;
}

export type ServerMessage =
  | Snapshot
  | { t: "notice"; message: string }
  | { t: "error"; message: string };

export type ClientMessage =
  | { t: "get" }
  | { t: "set"; state: StatePatch }
  | { t: "run" }
  | { t: "scene"; slot: number; op: "save" | "apply" | "clear" }
  | { t: "config"; config: ConfigPatch }
  | { t: "test" }
  | { t: "reboot" }
  | { t: "checkUpdate" }
  | { t: "installUpdate" };

// GPIOs on a D1 mini that can drive a strip, labelled as printed on the board.
export const PINS = [
  { gpio: 2, label: "D4 (GPIO2)" },
  { gpio: 4, label: "D2 (GPIO4)" },
  { gpio: 5, label: "D1 (GPIO5)" },
  { gpio: 14, label: "D5 (GPIO14)" },
  { gpio: 12, label: "D6 (GPIO12)" },
  { gpio: 13, label: "D7 (GPIO13)" },
  { gpio: 3, label: "RX (GPIO3)" },
  { gpio: 15, label: "D8 (GPIO15)" },
  { gpio: 0, label: "D3 (GPIO0)" },
  { gpio: 1, label: "TX (GPIO1)" },
] as const;

export const clamp = (v: number, lo: number, hi: number) =>
  Math.min(hi, Math.max(lo, Math.round(v)));

export function parseServerMessage(text: string): ServerMessage | null {
  let m: unknown;
  try {
    m = JSON.parse(text);
  } catch {
    return null;
  }
  if (!m || typeof m !== "object") return null;
  const o = m as Record<string, unknown>;
  if (o.t === "state" && o.state && o.config && o.info && Array.isArray(o.scenes)) {
    return o as unknown as Snapshot;
  }
  if ((o.t === "notice" || o.t === "error") && typeof o.message === "string") {
    return o as ServerMessage;
  }
  return null;
}

export function encode(msg: ClientMessage): string {
  return JSON.stringify(msg);
}

/** Same rules as the firmware's stateFromJson: unknown or out-of-range fields are dropped or clamped. */
export function sanitizeState(patch: StatePatch): StatePatch {
  const out: StatePatch = {};
  if (typeof patch.on === "boolean") out.on = patch.on;
  if (typeof patch.bri === "number") out.bri = clamp(patch.bri, 0, 255);
  if (patch.effect && EFFECTS.some((e) => e.id === patch.effect)) out.effect = patch.effect;
  if (patch.fx && typeof patch.fx === "object") {
    for (const def of EFFECTS) {
      const f = patch.fx[def.id];
      if (!f || typeof f !== "object") continue;
      const o: FxPatch = {};
      if (typeof f.variant === "number") o.variant = clamp(f.variant, 0, Math.max(0, def.variants.length - 1));
      if (Array.isArray(f.palette)) {
        const pal = f.palette.filter(isHex).slice(0, PALETTE_MAX).map((c) => c.toUpperCase());
        if (pal.length) o.palette = pal;
      }
      if (typeof f.period === "number") o.period = clampf(f.period, 3, 60);
      if (typeof f.spread === "number") o.spread = clampf(f.spread, 0, 100);
      if (f.p && typeof f.p === "object") {
        const p: Record<string, number> = {};
        for (const d of def.params) {
          const v = f.p[d.key];
          if (typeof v === "number") p[d.key] = clampf(v, d.min, d.max);
        }
        if (Object.keys(p).length) o.p = p;
      }
      if (typeof f.white === "boolean") o.white = f.white;
      if (Object.keys(o).length) (out.fx ??= {})[def.id] = o;
    }
  }
  return out;
}

const clampf = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));

/** Applies a patch the way the staff does: nested effect settings merge field by field. */
export function mergeState(state: LightState, patch: StatePatch): LightState {
  const next: LightState = { ...state, ...patch, fx: { ...state.fx } };
  for (const [id, f] of Object.entries(patch.fx ?? {}) as [EffectId, FxPatch][]) {
    const cur = state.fx[id];
    next.fx[id] = { ...cur, ...f, p: { ...cur.p, ...f.p } };
  }
  return next;
}

/** A value with the unit the slider shows. */
export function formatParam(v: number, unit: string) {
  const n = Number.isInteger(v) ? String(v) : v.toFixed(1).replace(".", ",");
  return `${n} ${unit}`;
}

export const isHex = (v: unknown): v is string =>
  typeof v === "string" && /^#[0-9a-fA-F]{6}$/.test(v);

export function hsvToHex(h: number, s: number, v: number): string {
  const f = (n: number) => {
    const k = (n + h / 60) % 6;
    return v - v * s * Math.max(0, Math.min(k, 4 - k, 1));
  };
  const to = (x: number) =>
    Math.round(x * 255)
      .toString(16)
      .padStart(2, "0");
  return `#${to(f(5))}${to(f(3))}${to(f(1))}`.toUpperCase();
}

export function hexToHsv(hex: string): { h: number; s: number; v: number } {
  const n = parseInt(hex.slice(1), 16);
  const r = ((n >> 16) & 255) / 255;
  const g = ((n >> 8) & 255) / 255;
  const b = (n & 255) / 255;
  const max = Math.max(r, g, b);
  const d = max - Math.min(r, g, b);
  let h = 0;
  if (d) {
    if (max === r) h = ((g - b) / d) % 6;
    else if (max === g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h *= 60;
    if (h < 0) h += 360;
  }
  return { h, s: max ? d / max : 0, v: max };
}

/** Brightness as the staff will show it, in percent of full. */
// How the strip splits up, as the staff renders it (firmware/src/effects.cpp). Null when the
// fold does not fit; the staff then treats the strip as plain.
export function layout(leds: number, fold: number, tip: boolean) {
  const straight = leds - 2 * fold - (tip ? 1 : 0);
  if (straight < 0) return null;
  return { straight, fold, tip, logical: leds - fold };
}

export function layoutText(leds: number, fold: number, tip: boolean) {
  const l = layout(leds, fold, tip);
  if (!l) return `Passt nicht: zwei Lagen à ${fold}${tip ? " plus Spitze" : ""} sind mehr als ${leds} LEDs.`;
  if (!fold) return `Ein gerader Strip, die Effekte laufen über alle ${leds} LEDs.`;
  return `${l.straight} gerade, ${fold} doppelt${tip ? ", 1 Spitze" : ""}: die Effekte laufen über ${l.logical} Pixel, die zweite Lage leuchtet gespiegelt mit.`;
}

export const effectivePercent = (bri: number, maxBri: number) =>
  Math.round((Math.min(bri, maxBri) / 255) * 100);

/** The upload target for a file the user picked: the filesystem image or the firmware. */
export function updateTarget(fileName: string, firstByte: number | undefined): "fs" | "firmware" | null {
  const name = fileName.toLowerCase();
  if (firstByte === 0xe9 || firstByte === 0x1f) return "firmware";
  if (name.includes("littlefs") || name.includes("fs")) return "fs";
  return null;
}
