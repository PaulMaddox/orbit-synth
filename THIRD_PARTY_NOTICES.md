# Third-party notices

ORBIT-specific code is published here without an additional open-source license.
Do not assume this grants permission to redistribute that code. Third-party
components retain their own licenses; this file does not replace them.

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
- ES7210 register initialization follows Espressif's Apache-2.0
  [esp_codec_dev driver](https://github.com/espressif/esp-adf/tree/master/components/esp_codec_dev/device/es7210).
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

## Original material

Electronic presets, demo compositions and vocal phrases are original ORBIT
material. Demo speech was synthesized locally with Windows David, Hazel and Zira
voices using expressive SSML. No user's microphone recording or proprietary
keyboard factory sample is included.
