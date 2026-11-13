import { build } from "esbuild";
import { cp, mkdir, readFile, writeFile } from "node:fs/promises";

await mkdir("dist/images", { recursive: true });
const result = await build({
  entryPoints: ["src/app.js"],
  bundle: true,
  minify: true,
  format: "iife",
  target: "chrome120",
  write: false,
  legalComments: "inline",
});
const template = await readFile("index.html", "utf8");
if (!template.includes("<!-- INSTALLER_SCRIPT -->")) {
  throw new Error("Installer script placeholder is missing.");
}
const script = result.outputFiles[0].text.replaceAll("</script", "<\\/script");
await writeFile("dist/index.html", template.replace("<!-- INSTALLER_SCRIPT -->", () => `<script>${script}</script>`));
await cp("../docs/images/play.png", "dist/images/play.png");
await writeFile("dist/.nojekyll", "");
const notices = [];
for (const [name, license] of [
  ["esptool-js", "LICENSE"],
  ["pako", "LICENSE"],
  ["atob-lite", "LICENSE.md"],
  ["spark-md5", "LICENSE"],
]) {
  notices.push(`${name}\n${"=".repeat(name.length)}\n${await readFile(`node_modules/${name}/${license}`, "utf8")}`);
}
const zlib = await readFile("node_modules/pako/lib/zlib/deflate.js", "utf8");
const start = zlib.indexOf("// (C)");
const end = zlib.indexOf("\n\n", start);
if (start < 0 || end < 0) throw new Error("The pako Zlib license header is missing.");
notices.push(`pako / Zlib\n===========\n${zlib.slice(start, end).replace(/^\/\/ ?/gm, "")}`);
await writeFile("dist/third-party-licenses.txt", notices.join("\n\n"));
console.log("Built ORBIT installer in web/dist (firmware is packaged separately by build.py).");
