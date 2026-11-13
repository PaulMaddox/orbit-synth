export class Transport {
  async disconnect() { window.testDevice.closed = true; }
}

export class ESPLoader {
  chip = { CHIP_NAME: "ESP32-C6" };
  async main() {
    if (window.testDevice.connectionError) throw new Error("Connection failed");
    if (window.testDevice.wrongChip) this.chip.CHIP_NAME = "ESP32-S3";
  }
  async detectFlashSize() { return "16MB"; }
  async readFlash() { return new Uint8Array([0xe9, 1, 2, 3]); }
  async after() { window.testDevice.reset = true; }
  async writeFlash(options) {
    window.testDevice.addresses = options.fileArray.map((part) => part.address);
    if (window.testDevice.writeError) throw new Error("USB disconnected during write");
    options.reportProgress(0, 1, 2);
    options.reportProgress(0, 2, 2);
  }
}
