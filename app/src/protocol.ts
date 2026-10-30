// The messages between the app and the staff; mirrors firmware/src/config.cpp
// and web.cpp. See docs/protocol.md.

export const EFFECTS = [
  { id: "solid", name: "Farbe" },
  { id: "rainbow", name: "Regenbogen" },
  { id: "breathe", name: "Atmen" },
  { id: "chase", name: "Lauflicht" },
  { id: "sparkle", name: "Funkeln" },
  { id: "fire", name: "Feuer" },
  { id: "colorloop", name: "Farbwechsel" },
  { id: "strobe", name: "Strobo" },
] as const;

export type EffectId = (typeof EFFECTS)[number]["id"];

export interface LightState {
  on: boolean;
  bri: number; // 0..255
  effect: EffectId;
  speed: number; // 1..100
  color: string; // #RRGGBB
  color2: string;
}

export interface Config {
  leds: number;
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
  scenes: (LightState | null)[];
  config: Config;
  info: Info;
}

export type ServerMessage =
  | Snapshot
  | { t: "notice"; message: string }
  | { t: "error"; message: string };

export type ClientMessage =
  | { t: "get" }
  | { t: "set"; state: Partial<LightState> }
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
export function sanitizeState(patch: Partial<LightState>): Partial<LightState> {
  const out: Partial<LightState> = {};
  if (typeof patch.on === "boolean") out.on = patch.on;
  if (typeof patch.bri === "number") out.bri = clamp(patch.bri, 0, 255);
  if (patch.effect && EFFECTS.some((e) => e.id === patch.effect)) out.effect = patch.effect;
  if (typeof patch.speed === "number") out.speed = clamp(patch.speed, 1, 100);
  if (isHex(patch.color)) out.color = patch.color.toUpperCase();
  if (isHex(patch.color2)) out.color2 = patch.color2.toUpperCase();
  return out;
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
export const effectivePercent = (bri: number, maxBri: number) =>
  Math.round((Math.min(bri, maxBri) / 255) * 100);

/** The upload target for a file the user picked: the filesystem image or the firmware. */
export function updateTarget(fileName: string, firstByte: number | undefined): "fs" | "firmware" | null {
  const name = fileName.toLowerCase();
  if (firstByte === 0xe9 || firstByte === 0x1f) return "firmware";
  if (name.includes("littlefs") || name.includes("fs")) return "fs";
  return null;
}
