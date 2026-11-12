"""Generate original, expressive demo phrases locally with Windows speech synthesis."""
import pathlib
import subprocess
import tempfile

import numpy as np
import soundfile as sf

ROOT = pathlib.Path(__file__).resolve().parents[1]
PHRASES = [
    ('Microsoft Zira Desktop', '<prosody pitch="+8%">Oh,</prosody> <prosody pitch="+22%">yeah!</prosody>'),
    ('Microsoft David Desktop', '<prosody pitch="+12%">All</prosody> <prosody pitch="-6%">right!</prosody>'),
    ('Microsoft Hazel Desktop', '<prosody pitch="+18%">Here</prosody> we <prosody pitch="+9%">go!</prosody>'),
    ('Microsoft David Desktop', '<prosody pitch="+25%">Hey!</prosody>'),
    ('Microsoft Zira Desktop', '<prosody pitch="+15%">Let\'s</prosody> <prosody pitch="+28%">go!</prosody>'),
    ('Microsoft David Desktop', '<prosody pitch="+20%">Turn</prosody> it <prosody pitch="-8%">up!</prosody>'),
    ('Microsoft Zira Desktop', '<prosody pitch="+12%">Shine</prosody> <prosody pitch="+22%">on!</prosody>'),
    ('Microsoft Hazel Desktop', '<prosody pitch="+5%">So</prosody> <prosody pitch="+18%">good!</prosody>'),
    ('Microsoft David Desktop', '<prosody pitch="+12%">With</prosody> <prosody pitch="+4%">you!</prosody>'),
    ('Microsoft Zira Desktop', '<prosody pitch="+4%">All</prosody> <prosody pitch="+20%">night!</prosody>'),
    ('Microsoft David Desktop', '<prosody pitch="+12%">Come</prosody> <prosody pitch="+20%">on!</prosody>'),
    ('Microsoft Zira Desktop', '<prosody pitch="+14%">Feel</prosody> <prosody pitch="+25%">good!</prosody>'),
]


def main():
    assets = ROOT / "tools" / "demo-voices"
    assets.mkdir(exist_ok=True)
    cpp = ['// Original locally synthesized phrases; tools/make_demo_voices.py.',
           '#include "demo_sample.h"', 'namespace orbit {',
           f'static_assert(DemoCount == {len(PHRASES)}, "Regenerate the demo voice bank");']
    descriptions = []
    with tempfile.TemporaryDirectory() as temporary:
        for index, (voice, phrase) in enumerate(PHRASES):
            output = assets / f"{index + 1}.wav"
            ssml = (f'<speak version="1.0" xml:lang="en-US"><voice name="{voice}">'
                    f'<prosody rate="+25%">{phrase}</prosody></voice></speak>')
            xml = pathlib.Path(temporary) / "phrase.xml"
            xml.write_text(ssml)
            script = (
                "Add-Type -AssemblyName System.Speech; "
                "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
                "$format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo("
                "16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,"
                "[System.Speech.AudioFormat.AudioChannel]::Mono); "
                f"$s.SetOutputToWaveFile('{output}', $format); "
                f"$s.SpeakSsml([IO.File]::ReadAllText('{xml}')); $s.Dispose()"
            )
            subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", script], check=True)
            audio, rate = sf.read(output)
            if rate != 16000 or audio.ndim != 1:
                raise ValueError(f"Incorrect voice recording format: {output}")
            audible = np.flatnonzero(np.abs(audio) > 0.008)
            if not len(audible):
                raise ValueError(f"Silent voice recording: {output}")
            audio = audio[max(0, audible[0] - 80):audible[-1] + 161]
            if len(audio) > 20000:
                raise ValueError(f"Demo phrase exceeds 1.25 seconds: {output}")
            audio -= audio.mean()
            audio *= 0.65 / np.max(np.abs(audio))
            audio[:80] *= np.linspace(0, 1, 80)
            audio[-80:] *= np.linspace(1, 0, 80)
            samples = np.rint(audio * 32767).astype(np.int16)
            sf.write(output, samples, rate, subtype="PCM_16")
            cpp.append(f"const int16_t DemoVocal{index}[] = {{")
            cpp.extend("    " + ",".join(map(str, samples[i:i + 16])) + ","
                       for i in range(0, len(samples), 16))
            cpp.append("};")
            descriptions.append(f"    {{DemoVocal{index}, {len(samples)}}},")
            print(f"Phrase {index + 1}: {len(samples) / rate:.2f} seconds")
    cpp.extend(["const DemoVoice DemoVoices[DemoCount] = {", *descriptions, "};", "}", ""])
    (ROOT / "Orbit" / "demo_sample.cpp").write_text("\n".join(cpp), encoding="ascii")


if __name__ == "__main__":
    main()
