export class Transport {
  async setDTR(value) { this.signal("DTR", value); }
  async setRTS(value) {
    this.signal("RTS", value);
    if (window.testDevice.resetError) throw new Error("Reset signal failed");
  }
  signal(name, value) {
    (window.testDevice.signals ||= []).push({ name, value, time: performance.now() });
  }
  async disconnect() {
    window.testDevice.closed = true;
    window.testDevice.closedAt = performance.now();
  }
}

export class ESPLoader {
  constructor(options) { this.options = options; }
  chip = { CHIP_NAME: "ESP32-C6" };
  async main() {
    if (window.testDevice.connectionError) throw new Error("Connection failed");
    if (window.testDevice.wrongChip) this.chip.CHIP_NAME = "ESP32-S3";
  }
  async detectFlashSize() { return "16MB"; }
  async readFlash() { return new Uint8Array([0xe9, 1, 2, 3]); }
  async after(mode) {
    if (mode !== "hard_reset") throw new Error(`Unexpected reset mode: ${mode}`);
    await this.options.resetConstructors.hardReset(this.options.transport).reset();
    window.testDevice.reset = true;
  }
  async writeFlash(options) {
    window.testDevice.addresses = options.fileArray.map((part) => part.address);
    if (window.testDevice.writeError) throw new Error("USB disconnected during write");
    options.reportProgress(0, 1, 2);
    options.reportProgress(0, 2, 2);
  }
}
