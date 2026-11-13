# Third-party notices

ORBIT-specific code is published here without an additional open-source license.
Do not assume this grants permission to redistribute that code. Third-party
components retain their own licenses; this file does not replace them.

## Hardware photographs

`docs/images/device.png` combines Waveshare's original labeled device photograph
with its [optional 400 mAh battery photograph](https://www.waveshare.com/img/devkit/general/battery-3.7V-400mAh.jpg).
The original device image and labels are unchanged; the battery photo is cropped
and resized, with an added specification label. No generated battery image is used.
The photographs remain Waveshare's material; no endorsement or additional license
is implied.

The [exact device listing](https://www.waveshare.com/esp32-c6-touch-lcd-1.83.htm)
links this optional 602525 lithium battery, marked 3.7 V and 400 mAh.
[Waveshare's documentation](https://docs.waveshare.com/ESP32-C6-Touch-LCD-1.83)
recommends 6 x 25 x 25 mm for installation inside the case.

## Instrument recordings

The firmware contains modified recordings from:

- **VSCO 2 Community Edition**, Sam Gossner / Versilian Studios, Simon Dalzell,
  Elan Hickler / Soundemote and contributors: CC0 1.0.
- **Shinyguitar**, D. Smolken / Karoryfer Samples: CC0 1.0.
- **MTG Solo Saxophones**, MTG / Freesound, with editing, looping and SFZ mapping
  by kinwie, distributed through sfzinstruments: **CC BY 4.0**.

See [assets/ATTRIBUTION.txt](assets/ATTRIBUTION.txt) for exact sources, pinned
revisions, credits, modifications and license links. Full licenses are in
[assets/licenses](assets/licenses). Preserve the saxophone attribution when
redistributing a recording bank or firmware. No source author's endorsement is implied.

## Codec and graphics

- `Orbit/es8311.cpp`, `es8311.h` and `es8311_reg.h` are the Espressif ES8311 driver
  distributed with Waveshare examples, under Apache-2.0. Original source notices
  and [the full license](Orbit/LICENSE-APACHE-2.0.txt) are retained.
- ES7210 register initialization and standby follow Espressif's Apache-2.0
  [esp_codec_dev driver](https://github.com/espressif/esp-adf/tree/master/components/esp_codec_dev/device/es7210).
- ES8311 standby follows Espressif's Apache-2.0
  [esp_codec_dev driver](https://github.com/espressif/esp-adf/tree/master/components/esp_codec_dev/device/es8311).
- [GFX Library for Arduino](https://github.com/moononournation/Arduino_GFX) and
  [XPowersLib](https://github.com/lewisxhe/XPowersLib) are installed dependencies,
  not relicensed ORBIT code. Their upstream licenses apply. The graphics build
  subset is generated locally and excluded from Git.
- The fixed-width graphics font originates in
  [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library).
- The bundled `FreeSansBold10pt7b.h` heading font comes from Arduino_GFX's
  HelloWorldGfxfont example and is derived from **GNU FreeFont**.
  GNU FreeFont is GPL-3.0-or-later with its font embedding exception.
  See [the full GPL](docs/licenses/GNU-GPL-3.0.txt),
  [the exception](docs/licenses/FreeFont-exception.txt),
  [upstream licensing](https://www.gnu.org/software/freefont/license.html) and
  [font sources](https://ftp.gnu.org/gnu/freefont/).

## Browser installer

The browser installer bundles these upstream packages with their original licenses:

- [esptool-js](https://github.com/espressif/esptool-js), Espressif Systems: Apache-2.0,
  including its bundled flash-loader stubs.
- [pako](https://github.com/nodeca/pako): MIT and Zlib.
- [atob-lite](https://github.com/hughsk/atob-lite): MIT.
- [spark-md5](https://github.com/satazor/js-spark-md5): WTFPL-2.0.

`web/build.mjs` includes the full installed package notices in the published
`third-party-licenses.txt`. Firmware packages also retain the recording attribution,
font and codec licenses, and the installed graphics/power-library notices.

## Original material

Electronic presets and demo compositions are original ORBIT material.
Twelve demo vocal phrases were synthesized locally with Windows David, Hazel
and Zira voices using expressive SSML. Six Seven uses its supplied, approved
non-personal two-second demo recording, preserved without resynthesis.
No personal recordings or proprietary keyboard factory samples are included.
