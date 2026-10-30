// Packs a release the staff can install itself (firmware/src/selfupdate.cpp): the signed
// firmware, the app files one by one, and manifest.json with its signature manifest.sig.
// Run after `bun run build` and `pio run -e d1_mini` (and `-t buildfs` for littlefs.bin):
//   SIGNING_KEY_FILE=~/.config/kupferstab/signing.key bun run scripts/release.ts 0.2.0
// The release workflow (.github/workflows/release.yml) does exactly this.

import { mkdir, readdir, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";

const root = join(import.meta.dir, "..");
const build = join(root, "firmware/.pio/build/d1_mini");
const data = join(root, "firmware/data");
const out = join(root, "release");

const version = process.argv[2] ?? "";
if (!/^\d+\.\d+\.\d+$/.test(version)) fail("Version fehlt oder ist kein x.y.z, z. B. 0.2.0");
const key = (process.env.SIGNING_KEY_FILE ?? "").replace(/^~/, process.env.HOME ?? "~");
if (!key || !(await Bun.file(key).exists())) fail("SIGNING_KEY_FILE zeigt auf keinen privaten Schlüssel");

function fail(message: string): never {
  console.error(message);
  process.exit(1);
}

// RSA-2048 over SHA-256, PKCS#1 v1.5: what BearSSL's SigningVerifier on the staff checks.
async function sign(bytes: Uint8Array): Promise<Uint8Array> {
  const proc = Bun.spawn(["openssl", "dgst", "-sha256", "-sign", key], { stdin: bytes, stdout: "pipe", stderr: "pipe" });
  const sig = new Uint8Array(await new Response(proc.stdout).arrayBuffer());
  if ((await proc.exited) !== 0) fail(`openssl: ${await new Response(proc.stderr).text()}`);
  return sig;
}

// Checks a signature against the public key the firmware carries, so a wrong secret fails here
// and not on the staff.
async function verify(bytes: Uint8Array, sig: Uint8Array) {
  const header = await Bun.file(join(root, "firmware/src/signing_key.h")).text();
  const pem = header.match(/-----BEGIN PUBLIC KEY-----[\s\S]+?-----END PUBLIC KEY-----/)?.[0];
  if (!pem) fail("Kein öffentlicher Schlüssel in firmware/src/signing_key.h");
  const dir = join(tmpdir(), `kupferstab-${process.pid}`);
  await mkdir(dir, { recursive: true });
  await Bun.write(join(dir, "pub.pem"), pem);
  await Bun.write(join(dir, "sig"), sig);
  const proc = Bun.spawn(
    ["openssl", "dgst", "-sha256", "-verify", join(dir, "pub.pem"), "-signature", join(dir, "sig")],
    { stdin: bytes, stdout: "ignore", stderr: "pipe" },
  );
  const ok = (await proc.exited) === 0;
  await rm(dir, { recursive: true, force: true });
  if (!ok) fail("Signatur passt nicht zum öffentlichen Schlüssel der Firmware. Falscher SIGNING_KEY?");
}

const sha256 = (bytes: Uint8Array) => new Bun.CryptoHasher("sha256").update(bytes).digest("hex");

await rm(out, { recursive: true, force: true });
await mkdir(out, { recursive: true });

// The updater expects: image, signature, signature length as u32 little-endian.
const image = new Uint8Array(await Bun.file(join(build, "firmware.bin")).arrayBuffer());
if (image[0] !== 0xe9) fail("firmware/.pio/build/d1_mini/firmware.bin ist kein ESP8266-Image");
const imageSig = await sign(image);
await verify(image, imageSig);
const length = new Uint8Array(4);
new DataView(length.buffer).setUint32(0, imageSig.length, true);
await Bun.write(join(out, "firmware.bin"), new Blob([image, imageSig, length]));

const files = [];
for (const name of (await readdir(data)).sort()) {
  const bytes = new Uint8Array(await Bun.file(join(data, name)).arrayBuffer());
  await Bun.write(join(out, name), bytes);
  files.push({ name, size: bytes.length, sha256: sha256(bytes) });
}

const manifest = new TextEncoder().encode(
  JSON.stringify({ version, tag: `v${version}`, firmware: "firmware.bin", files }),
);
const manifestSig = await sign(manifest);
await verify(manifest, manifestSig);
await Bun.write(join(out, "manifest.json"), manifest);
await Bun.write(join(out, "manifest.sig"), manifestSig);

// For uploading by hand without internet; the staff itself never loads it.
const fsImage = Bun.file(join(build, "littlefs.bin"));
if (await fsImage.exists()) await Bun.write(join(out, "littlefs.bin"), fsImage);

console.log(`Release ${version} in release/: firmware.bin (signiert), ${files.length} App-Dateien, manifest.json + .sig`);
