import { ESPLoader, Transport } from "esptool-js";
import { checkDevice, fetchImages, fetchManifest, writeFirmware } from "./firmware.js";

const $ = (id) => document.getElementById(id);
const manifestUrl = new URL("firmware/manifest.json", window.location.href);
const form = $("installer");
let manifest;
let connection;
let busy = false;

const mode = () => form.elements.mode.value;
const supported = window.isSecureContext && "serial" in navigator;

function message(text, kind = "") {
  $("status").textContent = text;
  $("status").dataset.kind = kind;
}

function log(text) {
  $("log").textContent = `${$("log").textContent}${text}\n`.slice(-16000);
  $("log").scrollTop = $("log").scrollHeight;
}

function refresh() {
  const firstInstall = mode() === "install";
  $("install-warning").hidden = !firstInstall;
  $("mode-note").textContent = firstInstall
    ? "For a new board or replacing other firmware. Existing data may be lost."
    : "Only the app is updated. Your ORBIT songs and recordings stay in place.";
  const acknowledged = $("board-check").checked && (!firstInstall || $("data-check").checked);
  $("connect").disabled = !supported || !manifest || !acknowledged || busy;
  $("connect").hidden = Boolean(connection);
  $("write").hidden = !connection;
  $("write").disabled = busy || !connection;
  $("write").textContent = firstInstall ? "Install ORBIT now" : "Update ORBIT now";
  $("disconnect").hidden = !connection;
  $("disconnect").disabled = busy;
  form.querySelectorAll("input").forEach((input) => { input.disabled = busy || Boolean(connection); });
  $("reload").hidden = Boolean(manifest) || busy;
}

async function loadManifest() {
  busy = true;
  refresh();
  try {
    manifest = await fetchManifest(manifestUrl);
    $("version").textContent = manifest.version.startsWith("build-")
      ? `Build ${manifest.version.slice(6)}` : manifest.version;
    $("revision").textContent = manifest.revision.slice(0, 7);
    $("source").href = `https://github.com/PaulMaddox/orbit-synth/commit/${manifest.revision}`;
    $("release").href = /^build-[0-9]+$/.test(manifest.version)
      ? `https://github.com/PaulMaddox/orbit-synth/releases/tag/${manifest.version}`
      : $("source").href;
    $("release").hidden = false;
    message(supported ? "Ready when you are. Check your board below, then connect." : "Use desktop Chrome or Edge to connect your board.");
  } catch (error) {
    $("version").textContent = "Unavailable";
    message(`Could not load the latest firmware. ${error.message} Check your connection and try again.`, "error");
    log(error.message);
  } finally {
    busy = false;
    refresh();
  }
}

async function closeConnection({ reset = true } = {}) {
  const current = connection;
  if (!current) return [];
  connection = undefined;
  const errors = [];
  if (reset) {
    try { await current.loader.after("hard_reset"); }
    catch (error) { errors.push(`Automatic restart failed: ${error.message}. Unplug USB and reconnect to restart ORBIT.`); }
  }
  try { await current.transport.disconnect(); }
  catch (error) { errors.push(`Could not close the USB connection: ${error.message}. Unplug USB before retrying.`); }
  errors.forEach(log);
  return errors;
}

$("connect").addEventListener("click", async () => {
  if (busy || !manifest || !supported || $("connect").disabled) return;
  busy = true;
  $("log").textContent = "";
  $("progress").hidden = true;
  $("progress-text").textContent = "";
  $("first-play").hidden = true;
  refresh();
  let transport;
  let loader;
  try {
    // Keep the chooser in the click's user-activation window, before any fetch.
    const port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a, usbProductId: 0x1001 }] });
    message("Downloading and checking the firmware. Keep this tab open.");
    const images = await fetchImages(manifest, manifestUrl, mode());
    message("Connecting to your board. This restarts the device; no flash has been written yet.");
    transport = new Transport(port, false);
    loader = new ESPLoader({
      transport, baudrate: 115200, romBaudrate: 115200,
      terminal: { clean: () => {}, writeLine: log, write: log },
    });
    connection = { transport, loader, images, mode: mode() };
    await checkDevice(loader, images, mode());
    message(`ESP32-C6 and 16 MB flash checked. ${mode() === "update" ? "ORBIT layout matches. Ready to update." : "Ready for first installation. Existing data may be lost."}`, "success");
  } catch (error) {
    const errors = await closeConnection();
    if (error.name === "NotFoundError") {
      message("No device selected. Connect your USB data cable and try again.");
    } else {
      log(error.message);
      message(`${error.message} ${errors.join(" ")}`, "error");
      $("connection-help").open = true;
    }
  } finally {
    busy = false;
    refresh();
  }
});

$("write").addEventListener("click", async () => {
  if (busy || !connection) return;
  busy = true;
  refresh();
  $("progress").value = 0;
  $("progress").hidden = false;
  message("Writing ORBIT. Keep USB connected and this tab open until verification finishes.");
  let written = false;
  try {
    const current = connection;
    await writeFirmware(current.loader, current.images, current.mode, (percent) => {
      $("progress").value = percent;
      $("progress-text").textContent = `Writing ${percent}% · verifying each image before finishing`;
    });
    written = true;
    const errors = await closeConnection();
    $("progress").value = 100;
    $("progress-text").textContent = "All written images verified.";
    message(errors.length
      ? `ORBIT was written and verified. ${errors.join(" ")}`
      : "You're ready to make music. ORBIT is restarting. If the screen stays dark, unplug USB and reconnect, then press PLAY.",
    errors.length ? "warning" : "success");
  } catch (error) {
    log(error.message);
    const errors = await closeConnection({ reset: false });
    $("progress-text").textContent = "Not completed. Reconnect and retry the same installation type.";
    message(`Installation did not finish: ${error.message}. Keep the same installation type and reconnect to retry. ${errors.join(" ")}`, "error");
    $("connection-help").open = true;
  } finally {
    busy = false;
    refresh();
    if (written) {
      $("connect").textContent = "Connect another ORBIT";
      $("first-play").hidden = false;
    }
  }
});

$("disconnect").addEventListener("click", async () => {
  busy = true;
  refresh();
  const errors = await closeConnection();
  message(errors.length ? errors.join(" ") : "Disconnected. No flash was written.", errors.length ? "warning" : "");
  busy = false;
  refresh();
});

form.addEventListener("submit", (event) => event.preventDefault());
form.addEventListener("change", refresh);
$("reload").addEventListener("click", loadManifest);
window.addEventListener("beforeunload", (event) => {
  if (busy || connection) {
    event.preventDefault();
    event.returnValue = "";
  }
});

if (!supported) $("browser-help").hidden = false;
loadManifest();
