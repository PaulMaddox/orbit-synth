# ORBIT development guide

Read this before changing firmware. README.md is the user guide; keep technical
details here and update the user guide and screenshots when behavior changes.

## Product rules

- Keep README strictly user-facing: features, how to use the app, installation
  and practical troubleshooting. Put implementation details and engineering
  explanations in AGENTS.md, not the user guide.
- Design for **beginners across the entire app**, not just the sound picker.
  Prefer ordinary words, large targets, short flows, safe previews and undo.
- Portrait, 240 x 284. Preserve the rounded-screen safe areas: header sides 24 px,
  footer sides 20 px, footer bottom 16 px. `ui_layout.h` is the shared source of
  truth for drawing/hit targets; native tests check conservative corner safety.
  Keep the visible footers inside those safe margins, but use `FooterTouch`,
  `FooterPairTouch` and `FooterWideTouch` for input. These cover the full bottom
  strip (y=232..283) including side/bottom padding, partitioned without overlaps.
- MELODY/BASS use **automatic tap or hold**. Do not reintroduce a per-sound
  Play Whole/Hold selector. Drums, microphone recordings and pieces are one-shots.
- Keep sounds recognizable and genuinely distinct. Twenty-one instruments use
  real licensed recordings; nine electronic sounds are intentionally synthetic.
  Do not substitute similar oscillators for every instrument or copy proprietary
  keyboard samples. Tuba is an intentional first-class instrument.
- All demos are original compositions; style inspiration is not permission to
  reproduce someone else's melody, lyrics or arrangement.
- SIX SEVEN is picker/serial demo 1 and the fresh-install fallback, using its
  curated SONG 1 arrangement and approved non-personal two-second voice asset.
  Existing valid saved sessions still restore; loading demos preserves master volume.
  IRON remains demo 2, with Saw Stack (17) on MELODY in every scene.
  `DemoInfo::arrangement` maps presentation order to stable composition/vocal-bank
  IDs; do not reorder generated voices or change other songs when moving a card.
- Development/test songs, tracks and microphone content are disposable. Do not
  spend time making backups or reverse-engineering restoration of test music.
  **Keep the product's ordinary save, undo and confirmation behavior.**
- Never commit device dumps, credentials, raw sample caches,
  downloaded tools or generated build artifacts. Check `.gitignore` and the
  staged diff before pushing. Generated original demo phrases are intentional assets.
- Do not bypass Windows security restrictions. Use a trusted installed toolchain.
- Do not change battery charging settings or undocumented power rails.

## Repository map

| Path | Purpose |
|---|---|
| `Orbit/app.cpp` | Drawing, touch/buttons, page flows, audio task, saves, serial diagnostics |
| `Orbit/engine.h`, `engine.cpp` | Portable music engine, voices, sequencing, scenes, FX, sampling, demos |
| `Orbit/song_storage.h` | Shared checksummed session/slot reader and atomic writer |
| `Orbit/board.h`, `board.cpp` | I2C peripherals, power, codecs, I2S and touch |
| `Orbit/audio_input.h` | Verified microphone word layout |
| `Orbit/ui_layout.h` | Shared touch/drawing rectangles |
| `Orbit/touch_gesture.h` | Testable press/movement/hold recognition; ordinary button holds remain taps |
| `Orbit/screen_idle.h` | Wrap-safe inactivity timing and power-off countdown |
| `Orbit/sound_presets.h` | Sound definitions and family API |
| `Orbit/sample_bank.h`, `sample_bank.cpp`, `sample_bank_lookup.cpp` | Generated recordings/FIR tables, register folding and sample lookup |
| `Orbit/sound_levels.h` | Generated per-role/note/tone Q12 gains |
| `Orbit/demo_sample.h`, `demo_sample.cpp` | Generated original demo speech |
| `Orbit/es8311*`, `LICENSE-APACHE-2.0.txt` | Upstream codec driver and license |
| `Orbit/FreeSansBold10pt7b.h` | Upstream bitmap heading font; see third-party notices |
| `Orbit/partitions.csv` | Actual 16 MB flash layout |
| `Orbit/Orbit.ino` | Arduino sketch entry; setup/loop live in app.cpp |
| `assets/instruments.json` | Pinned recording paths, roots, layers, Git blob hashes and generated metadata |
| `assets/ATTRIBUTION.txt`, `assets/licenses/` | Recording provenance, modifications and full licenses |
| `tools/build_instruments.py` | Reproducible sample preparation and bank generation |
| `tools/calibrate_sounds.cpp`, `.py` | Offline synthesis/speaker model calibration |
| `tools/make_demo_voices.py`, `tools/demo-voices/` | Local SSML generation and resulting original WAV assets |
| `assets/six_seven.json`, `tools/build_six_seven.py`, `Orbit/six_seven.h` | Curated Six Seven score and reproducible generated arrangement |
| `tools/capture_screen.py` | Checksum-verified real framebuffer capture to PNG |
| `tests/engine_test.cpp` | Native behavior/audio/layout regressions |
| `tests/sound_loudness.h`, `speaker_adjustments.h` | Measured speaker response and empirical residual corrections |
| `tests/speaker_levels_test.py`, `sound_performance_test.py`, `capture_test.py` | Hardware checks |
| `tests/power_off_test.py` | Inaudible countdown/cancellation check; optional real shutdown |
| `build.py`, `requirements.txt` | Portable discovery of the pinned Arduino toolchain; build/install/update |
| `tools/requirements.txt` | Optional asset-generation, calibration and screenshot dependencies |
| `docs/images/` | Current firmware screenshots used by README |

`build/`, `build-*/`, `vendor/`, `assets/cache/` and root diagnostics are disposable
local outputs, not source. Do not run the obsolete single-phrase demo generator
from an old workspace; the supported generator is `make_demo_voices.py`.

## Hardware: tested board and connections

Only the Waveshare **ESP32-C6 Touch-LCD-1.83**, 16 MB flash, is verified.
The C6 is **single-core, 160 MHz, without hardware floating-point**.
Do not assume ESP32-S3 PSRAM or dual-core behavior.

| Peripheral | Connection / constraint |
|---|---|
| ST7789 LCD | 240 x 284 portrait; SPI SCK 1, MOSI 2, CS 5, DC 3, reset 4; backlight 6 |
| Shared I2C | SDA 7, SCL 8; 400 kHz; short bounded timeout |
| CST816D touch | Address `0x15`, IRQ 11; poll registers `0x02..0x06` |
| Touch wake setup | Register `0xfe=0xff`; interrupt mode `0xfa=0x60` |
| BOOT | GPIO 9, active low; held during power-on enters downloader |
| PWR / AXP2101 | PMU address `0x34`; short-key IRQ polling, not an ordinary button GPIO |
| Peripheral power | Only documented ALDO1/ALDO2 at 3.3 V are configured |
| ES8311 speaker DAC | I2C `0x18` |
| ES7210 input ADC | I2C `0x40` |
| Speaker amplifier | GPIO 0, high enabled; physically disabled during capture |
| Shared I2S | MCLK 19, BCLK 20, LRCK 22, DAC output 23, ADC input 21 |

Battery status is actual PMU state, not inferred USB voltage. Preserve charging,
USB-only, absent-battery and invalid-reading distinctions.

## Critical audio lessons

### Microphone format: do not regress this

- I2S runs **16 kHz, stereo 32-bit, Philips format, full duplex**, MCLK 256 x rate.
- The ES7210 packs four 16-bit words into each stereo-32 frame:
  **speaker reference / microphone 1 / unused / microphone 2**.
- Capture is **stride 4, channel 1**. Never select whichever slot is loudest:
  that can select the speaker reference and produce apparently plausible garbage.
- **ES7210 register `0x08` must retain `0x10`.** Clearing the LRCK-rate field
  produces 14 zero-filled frames followed by two real frames and garbled audio.
- RX reads must allow the block to arrive; zero-timeout reads caused incomplete data.
  Short/error reads and queue overflows must be reported.
- Roughly 150 startup RX drops have been observed. Separate startup settling from
  drops during actual capture; the latter produce CAPTURE GLITCH.
- A previously recorded corrupt sample is not fixed by changing routing.
  Test with a new recording.

### Real-time budget

- 64 frames/block = **4,000 microseconds**, four DMA descriptors.
- Audio task priority 4, stack 6,144 bytes; UI is lower priority.
- Engine mutations are protected by a FreeRTOS mutex. Do not hold it during
  expensive serial transfers, filesystem work or unrelated analysis.
- Twelve voices; integer phase accumulators, envelopes, filters and interpolation.
  Keep floating point and heap allocation **out of the per-sample path**.
  Trigger-time tuning/offline generation may use floating point.
- Output duplicates mono int16 into stereo int32 using multiplication by 65,536.
- Output hard limit is magnitude 30,000. Software volume defaults to 35, maximum
  80; the ES8311 hardware volume is 80. Do not silently remove these limits.
- The output DC blocker deliberately uses signed division. A negative arithmetic
  right shift left a rounding residue that could stick at DC.
- Ordinary echo: 4,096 samples, feedback 3/8. Recorded XY FX has its own delay/filter.
- A slow UI redraw must not miss audio deadlines. Mixer playhead updates are
  incremental; do not repaint whole track rows every audio/display tick.

USB disconnections during development have also been caused by unplugging or
using the device. Do not diagnose a firmware panic solely from a disappearing
serial port. Check reconnection/user activity and actual boot/error logs first.

## Music engine and automatic articulation

- Four roles: 0 MELODY, 1 BASS, 2 BEATS, 3 VOICE.
- 96 ticks/beat, 24 ticks/sixteenth, 32 steps = **768 ticks / two bars**.
- Integer sample-clock sequencer: add `BPM * 96`, tick threshold `SampleRate * 60`.
- BPM 60..180; root 0..11; scale 0 minor pentatonic / DREAMY,
  1 major pentatonic / BRIGHT, 2 natural minor / MOODY.
- Melody base MIDI 60, bass 48; per-track/scene pitch -12..12.
- A short melodic hit is an eighth note, **30/BPM seconds**.
  Voice progress accumulates BPM each sample; the budget is `16000 * 30`.
  This also lets already sounding notes follow tempo changes without retuning them.
- Live fingers still down at the short hit's final-fade boundary automatically
  become held notes. There is no per-track or per-sound playing-mode state.
- The short fade is inside the note budget, not an extra long release.
  Held-note release has a short de-click fade (128 samples / 8 ms).
- Recorded taps are 48 ticks; held lengths are rounded to the sixteenth-note grid.
  Preserve the count-in-held-note case: a note already held through the count-in
  is not mistaken for a fresh tap when recording begins.
- Sequenced gates use musical units, not fixed wall-clock samples.
  Do not change sample playback speed to implement a different BPM.
- Synthetic attacks are capped for short notes; recorded timbre/phase is preserved.
- Drums, whole microphone samples and pieces ignore finger release and stored
  short gates, playing their natural complete hit. Explicit stop/mute/clear can stop them.
- Live playing/preview bypasses loop mute; sequence playback respects it.
- New recording unmutes its selected track when recording actually starts.
  Cancelling must not unmute it; undo restores its pre-take state.
- Track and FX loop recording share `RecordPreparation`: Waiting (only when
  already playing), GetReady, Practice, then None when recording starts.
  GetReady and Practice each last two full eight-beat loops (16 beats per stage).
  Count 8 down to 1 twice per stage, at the current BPM, using the audio tick
  clock rather than UI timers. Starting stopped enters GetReady immediately;
  starting during playback waits for the next loop boundary first.
  Existing sequenced music is silent in GetReady and returns for Practice.
  Fresh takes suppress the selected old part during Practice without clearing
  notes until recording actually starts. Metronome clicks run in both stages.
  Audition notes/FX during preparation must never be saved. Preserve held-note
  classification across the transition, resetting its recorded start/duration.
  Cancel restores the original playing/stopped state and leaves notes/FX intact.
  Stop, undo and clear must not leave a preparation stage armed.
  Status exposes preparation, countdown and preparationLoop; serial reports
  preparation=0/1/2/3 (none/waiting/get-ready/practice), countdown and
  preparation_loop. Keep countdowns visible on Pads, Pieces and Live FX.
  Fresh-or-add confirmation defaults to fresh after three seconds.
- Max 96 notes/track. Undo is one track snapshot, with redo.

## Sound bank, registers and fidelity

Current IDs are stable numeric slots:

```
 0 Acoustic Guitar   1 Warm Pad          2 Glock          3 Acid
 4 Metal Guitar      5 Harp              6 Marimba        7 Piano
 8 Clean Guitar      9 Organ            10 Saxophone     11 Trumpet
12 French Horn      13 Trombone        14 Cello         15 Strings
16 Dream Pad        17 Saw Stack       18 Pulse Lead    19 Flute
20 Bowed Bass       21 Deep Bass       22 Pluck Bass     23 Growl Bass
24 Clarinet         25 Oboe            26 Game Bass     27 Vox Pad
28 Tuba             29 Solo Violin
```

Twenty-one instruments use 96 multisamples, about 2.10 MB encoded flash.
Soft/hard layers exist for acoustic guitar, piano and sax; velocity threshold 90.
The nearest practical root/layer is selected. Other nine slots are electronic.

Recorded notes fold **by whole octaves**, preserving pitch class, into these ranges:

| Instrument | MIDI range |
|---|---|
| Acoustic / metal / clean guitar | 40..88 |
| Glock | 72..100 |
| Harp | 48..96 |
| Marimba | 60..96 |
| Piano / organ | 36..96 |
| Saxophone | 49..84 |
| Trumpet | 54..86 |
| French horn | 41..77 |
| Trombone | 40..72 |
| Cello | 36..84 |
| Strings / solo violin | 55..96 |
| Flute | 60..96 |
| Bowed / plucked bass | 28..67 |
| Clarinet | 50..94 |
| Oboe | 58..91 |
| Tuba | 28..65 |

Do not force a flute into sub-bass and then compensate with extreme gain.
Electronic sounds retain their freely transposable registers.

Preparation: downmix, polyphase resample to 16 kHz, 40 Hz DC high-pass, trim,
4 ms attack fade, natural tail fade, normalization and logarithmically companded
8-bit PCM. Sustained recordings have crossfaded body loops and release continuation;
plucked recordings retain up to 1.8 s for held/natural decays.

Barely audible sustained-note pre-rolls are trimmed using the **same causal
measured-speaker model** as calibration: raw bass/breath energy was not a useful
audibility test. This made fast flute/tuba taps speak without speeding up or
pitch-shifting the recording. Per-source trim metadata is in the manifest.

Runtime: direct-flash PCM, 256-entry decode LUT, 16-tap / 32-phase / eight-band
fixed-point anti-aliasing resampler. Keep the contiguous-tap fast path and the
wrap/clamp edge path correct; indiscriminate per-tap boundary checks consumed
valuable deadline headroom. Minimum measured companding SNR is 36.9 dB.
This is a mono 16 kHz portable speaker, not CD/stereo workstation fidelity.

Recorded instruments bypass synthetic supporting harmonics and the old tone
low-pass. Plucked bass uses source-relative low-cut EQ, not fake harmonics.
Metal guitar uses recorded pickup audio, oversampled saturation and cabinet roll-off.
Saw/pulse/acid use bandlimited discontinuity correction.
Warm Pad, Dream Pad and Vox Pad must remain perceptually different; Vox uses
pitch-aware vowel formants.

## Source recordings and regeneration

Full details and required attribution: `assets/ATTRIBUTION.txt`, manifest and licenses.

| Source | Pinned revision | License |
|---|---|---|
| `sgossner/VSCO-2-CE` | `440300901dfe9275fd84e0b7763af1f8443ae62e` | CC0 |
| `sfzinstruments/karoryfer.shinyguitar` | `57243cca85277dbcc120ce17c6178032f93c80f3` | CC0 |
| `sfzinstruments/MTG.SoloSax` | `b494d256549b3d088fdec176ce82867f8a1f58b2` | CC BY 4.0 |

Do not infer roots blindly from filenames: source octave conventions differ.
Sax SFZ tuning corrections are applied offline. Preserve full relative paths in
the cache; different directories contain identically named files.

```sh
python -m pip install -r tools/requirements.txt
python tools/build_instruments.py
python tools/calibrate_sounds.py --reset-acoustic
python build.py
```

Normal builds use checked-in generated banks; no raw recordings or downloads are
required. Regeneration downloads only manifest-pinned files, verifies Git blob SHA1
even for cached files, and writes `assets/cache/` (ignored).
`--select` is an optional re-curation path requiring the pinned recursive source
tree snapshots; those research snapshots are intentionally not committed.

The original demo phrases are generated **locally**, not uploaded to a service:

```sh
python tools/make_demo_voices.py
```

This requires Windows System.Speech with David, Hazel and Zira voices.
SSML gives word-level pitch/rate variation. It writes the WAV assets and
`demo_sample.cpp`, whose static assertion must match `DemoCount`.
Phrases are trimmed, DC-removed, peak-normalized, faded and limited to 1.25 s.
Do not use an obsolete single-demo generator.

Six Seven is stable arrangement/voice ID 12, appended without renumbering the
original twelve composition/voice IDs. Its approved non-personal audio is
`tools/demo-voices/six-seven.wav`: exactly 32,000 mono PCM16 frames at 16 kHz.
Do not trim, normalize, pitch-shift or resynthesize that source. Its curated
musical settings, active scene, notes and FX are in `assets/six_seven.json`;
no device filesystem image or saved-session dump is a source asset.
Regenerate the arrangement and encode the existing WAVs without changing them:

```sh
python tools/build_six_seven.py
python tools/make_demo_voices.py --encode-only
```

## Loudness: measure the right thing

- Tiny-speaker fundamentals can be almost inaudible. Bass, kick and tom need
  audible upper content; simply increasing inaudible bass clips headroom.
- A generic 200 Hz high-pass was **not** an adequate speaker model. The real
  enclosure has sharp resonances/notches, captured in `tests/sound_loudness.h`.
- The model is a 1,024-tap minimum-phase reconstruction from a 2,048-point cepstrum.
  A short weighted FFT smeared narrow resonances and attack timing.
- Metric: loudest 80 ms RMS window, 40 ms hops, within the first 400 ms.
  Do not flatten the first 25/40 ms of every acoustic instrument: natural attacks differ.
- Current target is 72 speaker-weighted RMS. Gains are offline Q12 per role,
  preset, MIDI 36..100 and six tone anchors, interpolated at runtime.
  No online AGC, gain pumping or floating-point loudness meter belongs in the audio path.
- `speaker_adjustments.h` stores measured acoustic residuals that a sine model
  misses. Preserve the distinction between modeled and physically measured results.

After recipe/envelope/bank changes, regenerate gains and test both taps and holds:

```sh
python tools/calibrate_sounds.py
```

Use `--reset-acoustic` for a changed bank/recipe rather than blindly treating
earlier residuals as fresh physical proof. Then measure and refine on the hardware:

```sh
python tests/speaker_levels_test.py --port COM3 --output speaker-levels.json
python tools/calibrate_sounds.py --speaker-check speaker-levels.json
python build.py --flash COM3
```

Use key C, quiet surroundings, fixed device placement, and leave the device
untouched. The checker uses microphone **aggregates**, not the saved voice recording.
It checks low Melody, normal Melody and Bass. Refiner requires all 90 measurements,
`peak80ms-v1`, current bank `sampled-v2`, clean audio health and adequate SNR.
It rejects old-bank/incomplete/state-changing data.

Historical 3.0 physical matching reached <=1.87 dB, but that is **not proof** for
the later sampled-v2 attack changes. Native 3.1 tests cover three tempos and
require <=3 dB nominal tap spread. Rerun physical matching and the complete
twelve-voice FX stress check after meaningful DSP changes; ordinary healthy
playback is not a substitute for that stress test.

## Scenes, microphone and FX invariants

- Four scenes each own all four tracks, FX loop, and piece-playback setting.
  Tempo/root/scale/swing/volume and the original sample are shared.
- Scene switching stores the current section, applies the destination at a loop
  boundary while playing (immediately stopped), and resets local undo.
  Tapping the pending/current scene cancels the queue; STOP cancels it too.
- Scene copy needs confirmation in the UI and replaces only its destination.
  Finish track/FX recording before switching/copying.
- Pieces are eight equal-time windows into the original sample. Note pad values
  128..135 encode piece identity, retaining correct playback after leaving piece mode.
  They are not destructive edits or transient references to the current mode.
- Capture buffer is 32,000 int16 frames / two seconds. Discard the first 3,200
  incoming frames / 200 ms; remove mean, normalize toward 26,000 with gain cap
  32x, and fade 80 samples at both edges.
- Capture stops music and disables the amplifier. Never create acoustic feedback
  by enabling the speaker while recording. Replacing the sample affects all scenes.
- FX has 64 points/loop, smoothed filter/echo changes, recording/undo and live override.
  Release on page changes, touch-read failure and hardware STOP to prevent stuck FX.
  Do not let a recorded movement overwrite notes or instrument settings.
- After 45 stopped, idle seconds, request the AXP2101's documented shutdown
  (COMMON_CONFIG bit 0, as used by XPowersLib::shutdown). Check the I2C read/write;
  preserve other bits, charging settings and rail configuration. This replaces
  screen/audio standby and CPU light sleep on both USB and battery power.
- Show a full-screen warning after 30 seconds. Paint it once, then replace only
  its fixed-width opaque digits once per second (15..1); no full-screen refresh
  on countdown ticks or battery updates. Touch/BOOT/PWR cancels the warning and
  consumes the cancellation gesture, including BOOT hold/release.
  Tell users to press the power button to turn back on at any time.
- Performance, preparation, capture, voices/echo tails, live touches, diagnostics
  and pending saves prevent shutdown. Pause audio cooperatively between complete
  blocks before disabling the amplifier/display and requesting PMU shutdown.
  Never suspend a task while it might own the engine mutex or use I2S.
  A failed shutdown must restore the UI/audio and report POWER_OFF_ERROR.
  Power-on is a fresh boot restoring the saved session, not RAM/undo resume.
- Status exposes `idle_ms` and `power_countdown` (0 outside the warning).
  `status`, `fingerprint` and `screenshot` do not reset inactivity; `wake` remains
  a compatibility command to cancel/reset the countdown, not to power on.
  `python tests\power_off_test.py --port COM3` checks countdown/cancellation
  and redraw counts without playing sounds. `--shutdown` additionally requests
  real power-off and expects the USB port to disappear. A disappearing port
  alone is not proof of physical power-off: also check the screen and PWR
  startup/session restoration, on both USB and battery-only power.

### Touch reliability

- The original visible footer rectangles were fully clickable, but only 31 px
  tall. Input now covers their surrounding bottom padding too, without changing
  drawing or overlapping controls above y=232. Use the shared touch rectangles
  for three-button, two-button and full-width footer variants on every page.
- `TouchGesture::takeHold` consumes a gesture only when an actual hold action
  is eligible: a Play track tab or a Mixer row. An older global hold flag was
  set after 650 ms on every control, silently discarding ordinary longer presses.
  Keep ordinary button holds actionable on release; keep real hold actions
  single-shot and prevent an additional release tap afterward.
- Preserve musical pad note-on/off, FX/slider dragging, wake suppression and the
  existing movement tolerances (14 px for holds; 24 px for taps). Do not invent
  touch-coordinate calibration offsets without physical measurements.
- Native regressions check every drawn footer pixel, every expanded target pixel,
  non-overlap/upper-control boundaries and short/long/consumed/moved/wraparound
  gestures. Serial `tap` verifies dispatch only, not the physical touch controller.

## Persistence and flash layout

Session format is **version 5**, magic `0x4f524254`.
LittleFS writes `/orbit.tmp`, FNV-1a checksum, then atomic rename to `/orbit.bin`.
Save only stopped/idle, after voices/echo settle; no writes during performance.

The SESSION footer opens SONGS: eight numbered device slots in `/song1.bin` through
`/song8.bin`, using the same versioned format including the microphone sample.
Slots use `/song.tmp` and atomic replacement without removing the previous file.
Autosave continues independently in `/orbit.bin`; editing or loading never writes
back to a slot automatically. Confirm slot replacement and every load.
`song_storage.h` hashes stored bytes before migration and validates into staging
memory before `Engine::restoreSong` commits under the mutex. Failed reads,
checksums, allocation or validation must leave both live session and sample intact.
Only stopped, settled engines can restore; clear transient voices, delays and undo.
Never hold the engine mutex across filesystem transfers.

`Track` remains 584 bytes: pitch offset 582, reserved byte offset 583.
Session tracks start at offset 16; the old v2 prefix is 2,352 bytes.
Do not casually change packed layouts or remove static assertions.

- v1: no saved pitch; initialize neutral.
- v1/v2: initialize starter scenes.
- v3: scenes, pieces and FX; old padding is not a playing mode.
- **Only v4** used offset 583 for auto/whole/hold (0/1/2).
  Its whole-note events become eighth notes; held durations are retained.
- v5: reserved byte must be zero; note duration encodes the musical length.
- Hash the **original stored bytes before migration**. Reconstructing an old
  hash from zeroed padding failed because old padding was not reliably zero.
- Reject invalid settings, lengths and checksums explicitly; do not silently
  manufacture a valid-looking song from corrupt storage.

| Partition | Offset | Size |
|---|---:|---:|
| NVS | `0x9000` | `0x5000` |
| OTA data | `0xe000` | `0x2000` |
| Application | `0x10000` | `0x2f0000` = 3,080,192 bytes |
| Core dump | `0x300000` | `0x10000` |
| LittleFS | `0x310000` | `0xcf0000` |

Application-only updates use `build.py --flash PORT`. First install uses
`--install PORT` for bootloader at `0x0`, partitions `0x8000`, boot support
`0xe000`, and application `0x10000`. Never use an unrelated merged 16 MB image
or repartition to save a little code space. The coredump reservation was carved
from app space without moving LittleFS.

## Build and verification workflow

Dependencies and beginner setup are in README. `build.py` discovers Arduino CLI
via `--cli`, `ARDUINO_CLI`, PATH, or the existing Windows Arduino IDE installation.
It gets data/library directories from CLI, requires core 3.3.2 / GFX 1.6.3 /
XPowersLib 0.3.0, copies the small required graphics subset into ignored `vendor/`,
and uses the pinned package index for Windows SDK paths. Python helpers avoid
depending on separately blocked bundled executables. Never hardcode a contributor's
home, redirected Documents/OneDrive path or SDK folder into the build.

Native tests need g++ with C++17:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/engine_test.cpp Orbit/engine.cpp Orbit/sample_bank.cpp Orbit/sample_bank_lookup.cpp Orbit/demo_sample.cpp -o engine_test
./engine_test
```

On Windows, use `-o tests\engine_test.exe` and run `.\tests\engine_test.exe`.
Link every required translation unit if adding one.
The focused storage regression uses the same engine translation units:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/song_storage_test.cpp Orbit/engine.cpp Orbit/sample_bank.cpp Orbit/sample_bank_lookup.cpp Orbit/demo_sample.cpp -o tests/song_storage_test
./tests/song_storage_test
```

On Windows, use `-o tests\song_storage_test.exe` and run
`.\tests\song_storage_test.exe`. It covers full-sample round trips, independent slots,
replacement/write failures, corrupt/short reads, legacy file migration and restore state.

The suite covers layout safety, valid saves/migration, natural registers, gestures,
exact 60/120/180 BPM hit/preview lifetimes, tempo changes mid-note, fades, held
release, all 32 preparation beats and stage boundaries, cancellation, practice
audition without saving, tempo changes during preparation, scene copy/undo, all preset levels/headroom, pad spectral
differences, full microphone/drum hits, pieces/FX, all demo tracks and scene contrast.
PCM correlation alone is not proof that two pads sound different.

```sh
python build.py --check
python build.py
python build.py --flash COM3
python tests/sound_performance_test.py
```

The hardware stress check is audible and requires stopped playback/recording and
no user touching the board. It exercises twelve overlapping notes and live FX.
Assert no new audio errors and max render <4,000 us; do not claim an interrupted
or partial two-role run passed. Do not unexpectedly interrupt a person auditioning.
`capture_test.py --replace-sample` is a separate acoustic input check that
deliberately replaces microphone content and needs an audible PC speaker.

### Reproducing README screenshots

The ordinary build allocates **no framebuffer**. Optional capture is compiled
only with `ORBIT_SCREENSHOTS`; each request temporarily allocates a 136,320-byte
canvas, runs the actual drawing code, restores the physical display pointer and
streams little-endian RGB565 with an FNV checksum. Main-loop serial framing avoids
interleaved health output. Allocation/short writes must fail explicitly.
The native USB TX buffer is 2,048 bytes. Stream 512-byte chunks, yield between
writes and retry temporary backpressure with a bounded timeout; an early 4,096-byte
chunk implementation intermittently truncated frames. Validate dimensions, exact
byte count, checksum and end marker on the host. Also inspect the image and verify
the current page before/after capture: a valid checksum alone does not establish
that the intended screen was captured.

```sh
python -m pip install -r tools/requirements.txt
python build.py --screenshots --build-path build-screenshots --flash COM3
python tools/capture_screen.py --port COM3 docs/images/play.png
python build.py --flash COM3
```

Navigate the device before each capture, or use the serial page controls.
Use only original demo material for public screenshots of waveforms.
Restore the normal build afterward. Do not present concept art as a current screenshot.
ORBIT 3.2's two ready loops, two practice loops, discarded practice notes and actual
recording were exercised on hardware with zero audio errors. Native tests cover
each countdown beat, boundary and cancellation path. This short workflow check is
not the separate twelve-voice hardware stress test.

## Serial control reference

115200 baud, native USB CDC. Open ports with **DTR and RTS false before open**;
unnecessary control-line toggles have caused resets. Only one process owns the port.
Commands change the real instrument, so do not run them over active user work.

```
status / play / stop / record / track 0..3 / note 0..7 / release
pitch -12..12 / volume 0..80 / echo 0..100
mic / sample-stats / sample-dump / input-probe / reboot
sounds / preview 1..30 / use-sound / cancel-sound
demos / demo 1..13
menu / scenes / slices / whole / fx
scene 1..4 / copy-scene 1..4
fx X Y / fx-release / fx-record / fx-toggle / fx-undo / fx-clear
fingerprint / speaker-level / speaker-tone 50..6000
page N / tap X Y / wake / screenshot
```

`preview` is one-based; status patches are zero-based. Preview does not record,
change the selected patch, steal ordinary loop/live voices or replace the sample.
`use-sound`/`cancel-sound` require the picker. `demo` without a number loads demo 1.
The removed `mode` command must stay removed.

Pages: Play 0, Mixer 1, Sound 2, Mic 3, Session 4, Confirm 5, Demos 6,
SoundPicker 7, Explore 8, Scenes 9, Pieces 10, LiveFX 11, CopyScene 12, FXOptions 13.
Songs 14 and SongOptions 15 are the saved-song library and selected slot.
`page` intentionally accepts 0..4 and 8..15; use `sounds` or `demos` for those pickers.
`tap` calls the touch-release handler for a button; it is not a fake held pad.

`status` includes transport, counts, pitch/tone/echo, battery validity, screen
power-off countdown, scene/FX, bank identity, heap, peak, render maximum and RX/capture drops.
`fingerprint` gives hashes, not recordings, only while stopped.
`sample-dump` exports actual recorded PCM; never commit its output.
`speaker-level` measures 400 ms of the real mic without replacing recorded audio.
`speaker-tone` needs stopped music/sounds. `screenshot` needs the optional build.
Health logs every ten seconds report heap, max render, audio and touch errors.
