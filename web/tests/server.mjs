import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { build } from "esbuild";
import { createHash } from "node:crypto";

const template = await readFile("index.html", "utf8");
const mock = await build({
  entryPoints: ["src/app.js"], bundle: true, write: false, format: "iife",
  plugins: [{
    name: "test-device-only",
    setup(builder) {
      builder.onResolve({ filter: /^esptool-js$/ }, () => ({
        path: new URL("./mock-loader.mjs", import.meta.url).pathname.replace(/^\/([A-Z]:)/, "$1"),
      }));
    },
  }],
});
const mockPage = template.replace("<!-- INSTALLER_SCRIPT -->", () => `<script>${mock.outputFiles[0].text}</script>`);
const revision = "a".repeat(40);
const data = new Uint8Array([0xe9, 1, 2, 3]);
const manifest = {
  schema: 1, name: "ORBIT", chip: "ESP32-C6", flash_size: 0x1000000,
  layout: "orbit-16mb-v1", version: "build-42", revision,
  parts: [["bootloader", 0], ["partitions", 0x8000], ["boot-support", 0xe000], ["application", 0x10000]]
    .map(([name, offset]) => ({
      name, offset, path: `${revision}/${name}.bin`, size: data.length,
      sha256: createHash("sha256").update(data).digest("hex"),
    })),
};
const server = createServer(async (request, response) => {
  const path = new URL(request.url, "http://localhost").pathname;
  try {
    if (path.endsWith("/firmware/manifest.json")) {
      response.setHeader("Content-Type", "application/json");
      response.end(JSON.stringify(manifest));
    } else if (path.endsWith(".bin")) {
      response.end(data);
    } else if (path === "/orbit-synth/mock/") {
      response.setHeader("Content-Type", "text/html");
      response.end(mockPage);
    } else if (path === "/orbit-synth/") {
      response.setHeader("Content-Type", "text/html");
      response.end(await readFile("dist/index.html"));
    } else if (path.endsWith("/images/play.png")) {
      response.setHeader("Content-Type", "image/png");
      response.end(await readFile("dist/images/play.png"));
    } else {
      response.writeHead(404).end();
    }
  } catch (error) {
    console.error(error);
    response.writeHead(500).end("Test server error");
  }
});
server.listen(4173, "127.0.0.1");
