"""Build ORBIT with a locally installed, version-pinned Arduino toolchain."""

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent
CORE_VERSION = "3.3.2"
FQBN = "esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB"
APP_LIMIT = 0x2F0000


def output(command):
    return subprocess.check_output(command, encoding="utf-8").strip()


def find_cli(explicit):
    candidate = explicit or os.environ.get("ARDUINO_CLI") or shutil.which("arduino-cli")
    if not candidate and os.name == "nt":
        installed = pathlib.Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
        installed /= r"Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
        if installed.is_file():
            candidate = str(installed)
    if not candidate:
        raise RuntimeError("Install Arduino CLI or supply --cli PATH / ARDUINO_CLI.")
    return candidate


def build(args):
    cli = find_cli(args.cli)
    data = pathlib.Path(output([cli, "config", "get", "directories.data"]))
    platform = data / "packages" / "esp32" / "hardware" / "esp32" / CORE_VERSION
    if not platform.is_dir():
        raise RuntimeError(f"Install the pinned core: arduino-cli core install esp32:esp32@{CORE_VERSION}")
    libraries = json.loads(output([cli, "lib", "list", "--format", "json"]))
    required = {"GFX Library for Arduino": "1.6.3", "XPowersLib": "0.3.0"}
    locations = {}
    for row in libraries.get("installed_libraries", []):
        library = row["library"]
        if library["name"] in required and library["version"] == required[library["name"]]:
            locations[library["name"]] = pathlib.Path(library["install_dir"])
    if set(locations) != set(required):
        raise RuntimeError('Install libraries: arduino-cli lib install '
                           '"GFX Library for Arduino@1.6.3" "XPowersLib@0.3.0"')
    print(f"ESP32 {CORE_VERSION}; GFX 1.6.3; XPowersLib 0.3.0", flush=True)
    if args.check:
        return

    graphics = locations["GFX Library for Arduino"]
    local_graphics = ROOT / "vendor" / "OrbitGraphics"
    stems = ["Arduino_G", "Arduino_DataBus", "Arduino_GFX", "Arduino_TFT",
             "databus/Arduino_HWSPI", "display/Arduino_ST7789"]
    if args.screenshots:
        stems.append("canvas/Arduino_Canvas")
    for stem in stems:
        for extension in (".h", ".cpp"):
            relative = pathlib.Path("src") / (stem + extension)
            destination = local_graphics / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(graphics / relative, destination)
    for relative in ("src/gfxfont.h", "src/YCbCr2RGB.h", "src/font/glcdfont.h",
                     "library.properties"):
        destination = local_graphics / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(graphics / relative, destination)
    for notice in graphics.glob("LICENSE*"):
        shutil.copy2(notice, local_graphics / notice.name)
    shutil.copy2(graphics / "examples/HelloWorldGfxfont/FreeSansBold10pt7b.h",
                 ROOT / "Orbit" / "FreeSansBold10pt7b.h")

    destination = args.build_path or ROOT / "build"
    destination = destination.resolve()
    properties = {}
    if os.name == "nt":
        # Use the installed Python rather than bundled helper executables.
        index = json.loads((data / "package_esp32_index.json").read_text(encoding="utf-8"))
        dependencies = [tool for package in index["packages"]
                        if package["name"] == "esp32"
                        for version in package["platforms"] if version["version"] == CORE_VERSION
                        for tool in version["toolsDependencies"]]
        sdk_versions = {tool["version"] for tool in dependencies
                        if tool["name"] == "esp32-arduino-libs"}
        if len(sdk_versions) != 1:
            raise RuntimeError("Cannot resolve the pinned ESP32 SDK; update the ESP32 package index.")
        sdk_version = sdk_versions.pop()
        sdk = data / "packages" / "esp32" / "tools" / "esp32-arduino-libs" / sdk_version / "esp32c6"
        python = f'"{sys.executable}"'
        properties = {
            "recipe.hooks.prebuild.4.pattern.windows":
                f'{python} -m esptool --chip esp32c6 elf2image --flash-mode dio '
                f'--flash-freq 80m --flash-size 16MB -o "{destination / "Orbit.ino.bootloader.bin"}" '
                f'"{sdk / "bin" / "bootloader_qio_80m.elf"}"',
            "tools.gen_esp32part.cmd.windows":
                f'{python} "{platform / "tools" / "gen_esp32part.py"}"',
            "recipe.objcopy.bin.pattern": f"{python} -m esptool {{recipe.objcopy.bin.pattern_args}}",
            "recipe.hooks.objcopy.postobjcopy.3.pattern":
                f"{python} -m esptool {{recipe.hooks.objcopy.postobjcopy.3.pattern_args}}",
        }
    if args.screenshots:
        properties["compiler.cpp.extra_flags"] = "-DORBIT_SCREENSHOTS"
    command = [cli, "compile", "--fqbn", FQBN, "--build-path", str(destination),
               "--quiet", "--jobs", "8", "--library", str(local_graphics),
               "--library", str(locations["XPowersLib"])]
    for key, value in properties.items():
        command += ["--build-property", f"{key}={value}"]
    subprocess.run([*command, str(ROOT / "Orbit")], check=True)
    image = destination / "Orbit.ino.bin"
    if image.stat().st_size > APP_LIMIT:
        raise RuntimeError(f"Application exceeds the {APP_LIMIT:,}-byte partition.")
    print(f"Built {image.name}: {image.stat().st_size:,} / {APP_LIMIT:,} bytes", flush=True)

    if args.web_output:
        from tools.package_web_firmware import package_firmware
        package_firmware(destination, platform / "tools" / "partitions" / "boot_app0.bin",
                         args.web_output.resolve(), args.web_version, locations)

    port = args.install or args.flash
    if port:
        images = []
        if args.install:
            images += ["0x0", str(destination / "Orbit.ino.bootloader.bin"),
                       "0x8000", str(destination / "Orbit.ino.partitions.bin"),
                       "0xe000", str(platform / "tools" / "partitions" / "boot_app0.bin")]
        images += ["0x10000", str(image)]
        subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32c6",
                        "--port", port, "--baud", "921600", "write-flash", "--no-progress",
                        "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "16MB",
                        *images], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", help="Path to an existing Arduino CLI executable")
    parser.add_argument("--check", action="store_true", help="Check installed build dependencies only")
    parser.add_argument("--build-path", type=pathlib.Path, help="Override the output directory")
    parser.add_argument("--screenshots", action="store_true", help="Enable optional framebuffer capture")
    parser.add_argument("--web-output", type=pathlib.Path, help="Package firmware for the browser installer")
    parser.add_argument("--web-version", help="Published build identifier (defaults to the source revision)")
    flashing = parser.add_mutually_exclusive_group()
    flashing.add_argument("--install", metavar="PORT", help="First install: bootloader, partitions and app")
    flashing.add_argument("--flash", metavar="PORT", help="Update only the application")
    args = parser.parse_args()
    if args.check and (args.install or args.flash):
        parser.error("--check cannot be combined with flashing")
    if args.check and args.web_output:
        parser.error("--check cannot package web firmware")
    if args.web_version and not args.web_output:
        parser.error("--web-version requires --web-output")
    if args.screenshots and args.web_output:
        parser.error("Publish the normal firmware, not a screenshots build")
    try:
        build(args)
    except (RuntimeError, ValueError, subprocess.CalledProcessError, OSError) as error:
        print(f"BUILD_ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
