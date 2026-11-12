#include "../Orbit/engine.h"
#include "../tests/sound_loudness.h"

#include <algorithm>
#include <cstdio>
#include <memory>

using namespace orbit;
int main() {
    for (int track = 0; track < 2; ++track) {
        for (int patch = 0; patch < SoundCount; ++patch) {
            for (int midi = 36; midi <= 100; ++midi) {
                double shortRms = 0, heldRms = 0;
                for (int tone : {0, 20, 40, 60, 80, 100}) {
                    if (Sounds[patch].wave == SoundWave::Sample && tone) {
                        std::printf("%d %d %d %d %.6f %.6f\n", track, patch, midi, tone,
                            shortRms, heldRms);
                        continue;
                    }
                    auto engine = std::make_unique<Engine>();
                    engine->select(track);
                    engine->setPatch(patch);
                    engine->setSpace(0);
                    engine->setTone(tone);
                    int target = std::max(track == 0 ? 48 : 36,
                        std::min(track == 0 ? 100 : 88, midi));
                    int pad = 0, pitch = 0;
                    bool found = false;
                    for (int root = 0; root < 12 && !found; ++root)
                        for (pad = 0; pad < 8; ++pad) {
                            pitch = target - Engine::midiNote(track, pad, root, 0);
                            if (pitch < -12 || pitch > 12) continue;
                            engine->setRoot(root);
                            engine->setPitch(pitch);
                            found = true;
                            break;
                        }
                    if (!found) return 1;
                    if (Engine::midiNote(track, pad, engine->session.root, engine->session.scale) +
                        engine->session.tracks[track].pitch != target) return 2;
                    engine->noteOn(pad);
                    int16_t pcm[6400];
                    engine->render(pcm, 6400);
                    auto weighted = test::speakerSignal(pcm, 6400);
                    double adjustment = test::speakerAdjustment(track, patch, target);
                    shortRms = test::noteLoudness(weighted.data(), 6400) * adjustment;
                    heldRms = test::rms(weighted.data(), 6400) * adjustment;
                    std::printf("%d %d %d %d %.6f %.6f", track, patch, midi, tone,
                        shortRms, heldRms);
                    if (Sounds[patch].wave == SoundWave::Arcade) {
                        double energy[3]{};
                        int counts[3]{};
                        for (int i = 0; i < 6400; ++i) {
                            if ((i + 1) % 800 < 64) continue;
                            int step = ((i + 1) / 800) % 3;
                            energy[step] += weighted[i] * weighted[i];
                            ++counts[step];
                        }
                        for (int step = 0; step < 3; ++step)
                            std::printf(" %.6f", std::sqrt(energy[step] / counts[step]) * adjustment);
                    }
                    std::printf("\n");
                }
            }
        }
    }
}
