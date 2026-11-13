import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { test } from "node:test";
import { checkDevice, fetchImages, fetchManifest, validateManifest, writeFirmware } from "../src/firmware.js";

const revision = "a".repeat(40);
const data = new Uint8Array([0xe9, 1, 2, 3]);
const hash = createHash("sha256").update(data).digest("hex");
function fixture() {
  const manifest = {
    schema: 1, name: "ORBIT", chip: "ESP32-C6", flash_size: 0x1000000,
    layout: "orbit-16mb-v1", version: "build-42", revision,
    parts: [["bootloader", 0], ["partitions", 0x8000], ["boot-support", 0xe000], ["application", 0x10000]]
      .map(([name, offset]) => ({ name, offset, path: `${revision}/${name}.bin`, size: data.length, sha256: hash })),
  };
  const images = manifest.parts.map((part) => ({ ...part, data }));
  return { manifest, images };
}

function device() {
  return {
    chip: { CHIP_NAME: "ESP32-C6" },
    main: async () => {},
    detectFlashSize: async () => "16MB",
    readFlash: async () => data,
  };
}

test("refuses unsafe offsets, paths, chip families, empty images and oversized applications", () => {
  for (const mutate of [
    (m) => { m.parts[3].offset = 0; },
    (m) => { m.parts[0].path = "https://example.com/image.bin"; },
    (m) => { m.parts[0].path = "../bootloader.bin"; },
    (m) => { m.parts[3].size = 0x2f0001; },
    (m) => { m.parts[3].size = 0; },
    (m) => { m.chip = "ESP32-S3"; },
    (m) => { m.parts[0] = null; },
    (m) => { m.parts.reverse(); },
  ]) {
    const { manifest } = fixture();
    mutate(manifest);
    assert.throws(() => validateManifest(manifest));
  }
});

test("downloads only app plus partition comparison for an update, all four for first install", async () => {
  for (const [mode, count] of [["update", 2], ["install", 4]]) {
    const paths = [];
    const images = await fetchImages(fixture().manifest, "https://example.com/orbit/firmware/manifest.json", mode, async (url) => {
      paths.push(url.href);
      return new Response(data);
    });
    assert.equal(images.length, count);
    assert.ok(paths.every((path) => path.startsWith(`https://example.com/orbit/firmware/${revision}/`)));
  }
});

test("refuses incomplete or corrupted downloads before connecting to a device", async () => {
  for (const bytes of [new Uint8Array([0xe9, 1, 2]), new Uint8Array([0xe9, 1, 2, 4])]) {
    await assert.rejects(fetchImages(fixture().manifest, "https://example.com/firmware/manifest.json", "update",
      async () => new Response(bytes)), /integrity check/);
  }
  await assert.rejects(fetchManifest("https://example.com/manifest.json", async () => new Response("", { status: 404 })), /HTTP 404/);
});

test("refuses wrong chips, wrong capacity, unreadable and mismatched partition layouts", async () => {
  for (const changes of [
    { chip: { CHIP_NAME: "ESP32-S3" } },
    { detectFlashSize: async () => "4MB" },
    { detectFlashSize: async () => undefined },
    { readFlash: async () => new Uint8Array([1, 2, 3, 4]) },
    { readFlash: async () => new Uint8Array([0xe9, 1]) },
    { readFlash: async () => { throw new Error("USB disconnected"); } },
  ]) {
    await assert.rejects(checkDevice({ ...device(), ...changes }, fixture().images, "update"));
  }
  await checkDevice(device(), fixture().images, "update");
  await checkDevice({ ...device(), readFlash: async () => { throw new Error("Must not read partitions"); } },
    fixture().images, "install");
});

test("updates only the application, never requests whole-chip erase, and supplies device verification", async () => {
  const { images } = fixture();
  const progress = [];
  let written;
  await writeFirmware({
    writeFlash: async (options) => {
      written = options;
      assert.equal(options.calculateMD5Hash(data), createHash("md5").update(data).digest("hex"));
      options.reportProgress(0, 1, 2);
      options.reportProgress(0, 2, 2);
    },
  }, images, "update", (percent) => progress.push(percent));
  assert.deepEqual(written.fileArray.map((file) => file.address), [0x10000]);
  assert.equal(written.eraseAll, false);
  assert.equal(written.flashSize, "16MB");
  assert.deepEqual(progress, [50, 100]);
});

test("first installation writes exactly the four separate regions, not a merged full-flash image", async () => {
  let addresses;
  await writeFirmware({
    writeFlash: async (options) => {
      addresses = options.fileArray.map((file) => file.address);
      assert.equal(options.eraseAll, false);
    },
  }, fixture().images, "install", () => {});
  assert.deepEqual(addresses, [0, 0x8000, 0xe000, 0x10000]);
});

test("propagates flash and verification failures instead of reporting success", async () => {
  await assert.rejects(writeFirmware({
    writeFlash: async () => { throw new Error("MD5 of file does not match data in flash!"); },
  }, fixture().images, "update", () => {}), /MD5/);
});
