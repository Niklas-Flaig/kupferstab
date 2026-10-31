import { describe, expect, test } from "bun:test";
import {
  effectivePercent,
  encode,
  hexToHsv,
  hsvToHex,
  layout,
  mergeState,
  parseServerMessage,
  sanitizeState,
  updateTarget,
} from "../src/protocol";

describe("colour conversion", () => {
  test("primary colours round-trip", () => {
    for (const hex of ["#FF0000", "#00FF00", "#0000FF", "#FFFFFF", "#FF6A1F"]) {
      const { h, s, v } = hexToHsv(hex);
      expect(hsvToHex(h, s, v)).toBe(hex);
    }
  });
  test("hue positions match the wheel", () => {
    expect(hsvToHex(0, 1, 1)).toBe("#FF0000");
    expect(hsvToHex(120, 1, 1)).toBe("#00FF00");
    expect(hsvToHex(240, 1, 1)).toBe("#0000FF");
    expect(hsvToHex(0, 0, 1)).toBe("#FFFFFF");
  });
});

describe("state patches", () => {
  test("clamps like the firmware", () => {
    expect(sanitizeState({ bri: 300 })).toEqual({ bri: 255 });
    expect(
      sanitizeState({ fx: { strobo: { variant: 9, period: 1, spread: 140, p: { charge: 50, nope: 3 } } } }),
    ).toEqual({ fx: { strobo: { variant: 1, period: 3, spread: 100, p: { charge: 20 } } } });
  });
  test("drops unknown effects and bad colours", () => {
    expect(sanitizeState({ effect: "laser" as never, fx: { atem: { palette: ["red"] } } })).toEqual({});
    expect(sanitizeState({ effect: "glut", fx: { glut: { palette: ["#ff6a1f", "x", "#14c9a8"] } } })).toEqual({
      effect: "glut",
      fx: { glut: { palette: ["#FF6A1F", "#14C9A8"] } },
    });
  });
  test("merges effect settings field by field", () => {
    const fx = { variant: 0, palette: ["#FF6A1F"], period: 12, spread: 30, p: { tempo: 50 } };
    const base = { on: true, bri: 100, effect: "glut" as const, fx: { atem: fx, strobo: fx, glut: fx, polarlicht: fx, funkenflug: fx, mosaik: fx } };
    const next = mergeState(base, { fx: { glut: { p: { tempo: 80 } } } });
    expect(next.fx.glut).toEqual({ ...fx, p: { tempo: 80 } });
    expect(next.fx.atem).toBe(fx);
  });
});

describe("messages", () => {
  test("encodes a set message", () => {
    expect(JSON.parse(encode({ t: "set", state: { on: false } }))).toEqual({
      t: "set",
      state: { on: false },
    });
  });
  test("parses a snapshot and rejects junk", () => {
    const snap = {
      t: "state",
      state: { on: true, bri: 10, effect: "atem", fx: {} },
      scenes: [null, null, null, null],
      run: null,
      config: { leds: 164, fold: 50, tip: true, pin: 2, order: "GRB", maxBri: 102, apSsid: "Kupferstab", staSsid: "" },
      info: { fw: "0.1.0", heap: 20000, uptime: 3, apIp: "192.168.4.1", staIp: "", update: "", clients: 1, app: true },
    };
    expect(parseServerMessage(JSON.stringify(snap))?.t).toBe("state");
    expect(parseServerMessage('{"t":"notice","message":"hi"}')).toEqual({ t: "notice", message: "hi" });
    expect(parseServerMessage("not json")).toBeNull();
    expect(parseServerMessage('{"t":"state"}')).toBeNull();
  });
});

describe("helpers", () => {
  test("layout splits the strip like the staff", () => {
    expect(layout(164, 50, true)).toEqual({ straight: 63, fold: 50, tip: true, logical: 114 });
    expect(layout(163, 50, false)).toEqual({ straight: 63, fold: 50, tip: false, logical: 113 });
    expect(layout(60, 0, false)?.logical).toBe(60);
    expect(layout(100, 50, true)).toBeNull();
  });
  test("brightness shows the cap", () => {
    expect(effectivePercent(255, 102)).toBe(40);
    expect(effectivePercent(51, 255)).toBe(20);
  });
  test("update target follows the file", () => {
    expect(updateTarget("firmware.bin", 0xe9)).toBe("firmware");
    expect(updateTarget("firmware.bin.gz", 0x1f)).toBe("firmware");
    expect(updateTarget("littlefs.bin", 0x00)).toBe("fs");
    expect(updateTarget("foto.jpg", 0xff)).toBeNull();
  });
});
