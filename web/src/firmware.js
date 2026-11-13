import SparkMD5 from "spark-md5";

const REGIONS = [
  ["bootloader", 0, 0x8000],
  ["partitions", 0x8000, 0x1000],
  ["boot-support", 0xe000, 0x2000],
  ["application", 0x10000, 0x2f0000],
];

export function validateManifest(manifest) {
  if (
    manifest?.schema !== 1 || manifest.name !== "ORBIT" ||
    manifest.chip !== "ESP32-C6" || manifest.flash_size !== 0x1000000 ||
    manifest.layout !== "orbit-16mb-v1" ||
    !/^[a-f0-9]{40}$/.test(manifest.revision) ||
    !/^[a-zA-Z0-9._-]{1,80}$/.test(manifest.version) ||
    !Array.isArray(manifest.parts) || manifest.parts.length !== REGIONS.length
  ) {
    throw new Error("This firmware package is not a supported ORBIT build.");
  }
  REGIONS.forEach(([name, offset, maximum], index) => {
    const part = manifest.parts[index];
    if (
      part?.name !== name || part.offset !== offset ||
      part.path !== `${manifest.revision}/${name}.bin` ||
      !Number.isInteger(part.size) || part.size <= 0 || part.size > maximum ||
      !/^[a-f0-9]{64}$/.test(part.sha256)
    ) {
      throw new Error(`The ${name} image has invalid firmware metadata.`);
    }
  });
  return manifest;
}

export async function fetchManifest(url, fetcher = fetch) {
  const response = await fetcher(url, { cache: "no-store", signal: AbortSignal.timeout(30000) });
  if (!response.ok) throw new Error(`Firmware information is unavailable (HTTP ${response.status}).`);
  return validateManifest(await response.json());
}

export async function fetchImages(manifest, manifestUrl, mode, fetcher = fetch) {
  validateManifest(manifest);
  if (mode !== "update" && mode !== "install") throw new Error("Choose update or first installation.");
  const selected = manifest.parts.filter((part) =>
    mode === "install" || ["application", "partitions"].includes(part.name));
  const images = await Promise.all(selected.map(async (part) => {
    const response = await fetcher(new URL(part.path, manifestUrl), {
      cache: "no-store", signal: AbortSignal.timeout(60000),
    });
    if (!response.ok) throw new Error(`Cannot download ${part.name} (HTTP ${response.status}). Reconnect to retry.`);
    const data = new Uint8Array(await response.arrayBuffer());
    const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", data));
    const hash = Array.from(digest, (byte) => byte.toString(16).padStart(2, "0")).join("");
    if (data.length !== part.size || hash !== part.sha256) {
      throw new Error(`The ${part.name} download failed its integrity check. Nothing has been written.`);
    }
    return { ...part, data };
  }));
  return images;
}

export async function checkDevice(loader, images, mode) {
  if (mode !== "update" && mode !== "install") throw new Error("Choose update or first installation.");
  await loader.main();
  if (loader.chip.CHIP_NAME !== "ESP32-C6") {
    throw new Error("This is not an ESP32-C6. Only the Waveshare ESP32-C6 Touch-LCD-1.83 is supported.");
  }
  if (await loader.detectFlashSize() !== "16MB") {
    throw new Error("This device does not report 16 MB of flash. Nothing has been written.");
  }
  if (mode === "update") {
    const expected = images.find((part) => part.name === "partitions");
    const actual = await loader.readFlash(0x8000, expected.data.length);
    if (actual.length !== expected.data.length || actual.some((byte, index) => byte !== expected.data[index])) {
      throw new Error("The existing partition layout does not match ORBIT. Nothing has been written. Use first installation only if replacing the firmware and its data is OK.");
    }
  }
}

export async function writeFirmware(loader, images, mode, onProgress) {
  if (mode !== "update" && mode !== "install") throw new Error("Choose update or first installation.");
  const files = mode === "update" ? images.filter((part) => part.name === "application") : images;
  const expectedNames = mode === "update" ? ["application"] : REGIONS.map(([name]) => name);
  if (files.length !== expectedNames.length || files.some((part, index) =>
    part.name !== expectedNames[index] ||
    part.offset !== REGIONS.find(([name]) => name === part.name)[1] ||
    !(part.data instanceof Uint8Array) || part.data.length !== part.size)) {
    throw new Error("The prepared firmware images are incomplete or invalid.");
  }
  await loader.writeFlash({
    fileArray: files.map((part) => ({ data: part.data, address: part.offset })),
    flashSize: "16MB",
    flashMode: "dio",
    flashFreq: "80m",
    eraseAll: false,
    compress: true,
    calculateMD5Hash: (image) => SparkMD5.ArrayBuffer.hash(image),
    reportProgress: (index, written, total) => {
      // Compressed totals differ per file; weight progress by original image sizes.
      const completed = files.slice(0, index).reduce((sum, part) => sum + part.size, 0);
      const fullSize = files.reduce((sum, part) => sum + part.size, 0);
      onProgress(Math.floor(100 * (completed + files[index].size * written / total) / fullSize));
    },
  });
}
