# Building and installing ORBIT

This guide covers building ORBIT from source and installing or updating it manually
on the **Waveshare ESP32-C6 Touch-LCD-1.83 with 16 MB flash**.
For browser installation and using ORBIT, see the [user guide](README.md).

## Build and install manually

The build is verified on Windows with Python 3.11 and Arduino CLI 1.3.1.
The helper resolves paths from Arduino CLI, rather than requiring a particular
username or Arduino installation directory. Other desktop platforms have not yet
been verified.

1. Install [Python 3.11 or newer](https://www.python.org/downloads/) and
   [Arduino CLI](https://arduino.github.io/arduino-cli/latest/installation/).
   An existing Arduino IDE installation's CLI can also be used.
2. Clone this repository and install the small build dependency:

   ```sh
   git clone https://github.com/PaulMaddox/orbit-synth.git
   cd orbit-synth
   python -m pip install -r requirements.txt
   ```

   On Windows, use `py` in place of `python` if necessary. A Python virtual
   environment is recommended; it is not committed to the repository.

3. Install the tested core and library versions:

   ```sh
   arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
   arduino-cli core update-index
   arduino-cli core install esp32:esp32@3.3.2
   arduino-cli lib install "GFX Library for Arduino@1.6.3" "XPowersLib@0.3.0"
   ```

4. Connect the board and find its serial port:

   ```sh
   python -m serial.tools.list_ports
   python build.py --check
   ```

   The examples below use `COM3`. Replace it with your board's port.
   Close any serial monitor before flashing.

## First installation

> **Installing replaces the board's firmware and partition layout. Existing
> firmware data may be lost.** Use this command for a new board or when replacing
> unrelated firmware, not for routine ORBIT updates.

```sh
python build.py --install COM3
```

This builds the checked-in sounds and firmware, checks the application fits, then
flashes the bootloader, partition table, boot support and application. It does not
need to download or regenerate instrument recordings.

The board restarts into ORBIT. Press **PLAY** to hear the starter demo.
Start with a low volume; the default is 35%.

If Arduino CLI is not on your path, supply its existing executable:

```powershell
py build.py --cli "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" --install COM3
```

`ARDUINO_CLI` is also supported. Use an approved installation if your computer
restricts executables; do not disable security controls.

## Updating an existing ORBIT installation

```sh
python build.py --flash COM3
```

This writes **only the application** at `0x10000`, leaving the existing ORBIT
partitions and filesystem in place. Do not use a merged full-flash image for a
routine update. To build without touching the device, run `python build.py`.

## Build troubleshooting and development

If build dependencies do not match, run `python build.py --check` and install
the exact versions listed above.

For serial diagnostics, sound-bank regeneration and feature development, see
[AGENTS.md](AGENTS.md).
