import { describe, expect, test } from "bun:test";
import {
  effectivePercent,
  encode,
  hexToHsv,
  hsvToHex,
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
    expect(sanitizeState({ bri: 300, speed: 0 })).toEqual({ bri: 255, speed: 1 });
    expect(sanitizeState({ bri: -4, speed: 140 })).toEqual({ bri: 0, speed: 100 });
  });
  test("drops unknown effects and bad colours", () => {
    expect(sanitizeState({ effect: "laser" as never, color: "red" })).toEqual({});
    expect(sanitizeState({ effect: "fire", color: "#ff6a1f" })).toEqual({
      effect: "fire",
      color: "#FF6A1F",
    });
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
      state: { on: true, bri: 10, effect: "solid", speed: 5, color: "#FF0000", color2: "#000000" },
      scenes: [null, null, null, null],
      config: { leds: 60, pin: 2, order: "GRB", maxBri: 102, apSsid: "Kupferstab", staSsid: "" },
      info: { fw: "0.1.0", heap: 20000, uptime: 3, apIp: "192.168.4.1", staIp: "", update: "", clients: 1, app: true },
    };
    expect(parseServerMessage(JSON.stringify(snap))?.t).toBe("state");
    expect(parseServerMessage('{"t":"notice","message":"hi"}')).toEqual({ t: "notice", message: "hi" });
    expect(parseServerMessage("not json")).toBeNull();
    expect(parseServerMessage('{"t":"state"}')).toBeNull();
  });
});

describe("helpers", () => {
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
