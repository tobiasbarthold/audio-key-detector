// Feeds audio files through the plugin's LiveAnalyzer the way a host would (blocks of 512
// samples at the file's own sample rate, looped to ~20 s like a looping clip) and compares the
// live result with the offline analysis of the same looped audio (looping shifts the analysis
// frames, so it can differ from the single file in close major/minor cases; both are printed).
// Also checks that Reset clears the result.
//
//   KeyDetectorLiveTest <files...>
//
// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#include "../Source/Engine/AudioPrep.h"
#include "../Source/Engine/KeyEngine.h"
#include "../Source/Plugin/LiveAnalyzer.h"

#include <thread>

int main (int argc, char* argv[])
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    kd::KeyEngine engine;
    int failures = 0;

    for (int i = 1; i < argc; ++i)
    {
        juce::File file (juce::String::fromUTF8 (argv[i]));
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            std::printf ("cannot read %s\n", argv[i]);
            ++failures;
            continue;
        }
        const auto sr = reader->sampleRate;
        const int len = (int) reader->lengthInSamples;
        juce::AudioBuffer<float> audio ((int) reader->numChannels, len);
        reader->read (&audio, 0, len, 0, true, true);

        auto offline = kd::loadAudioFile (file, formats);
        auto single = engine.analyze (offline.samples.data(), offline.samples.size());

        LiveAnalyzer live;
        live.prepare (sr, 512);
        std::this_thread::sleep_for (std::chrono::milliseconds (120));   // let the prepare-reset settle
        const int repeats = juce::jmax (1, (int) std::ceil (20.0 * sr / len));   // ~20 s of audio
        std::vector<float> looped;
        for (int r = 0; r < repeats; ++r)
            looped.insert (looped.end(), offline.samples.begin(), offline.samples.end());
        auto ref = engine.analyze (looped.data(), looped.size());
        juce::AudioBuffer<float> block (audio.getNumChannels(), 512);
        int pushed = 0;
        for (int r = 0; r < repeats; ++r)
            for (int pos = 0; pos < len; pos += 512)
            {
                const int n = juce::jmin (512, len - pos);
                for (int c = 0; c < audio.getNumChannels(); ++c)
                    block.copyFrom (c, 0, audio, c, pos, n);
                live.pushAudio (block, audio.getNumChannels(), n);
                if ((pushed += n) > (int) (sr / 20))   // ~20x real time, well within the FIFO
                {
                    pushed = 0;
                    std::this_thread::sleep_for (std::chrono::milliseconds (2));
                }
            }
        std::this_thread::sleep_for (std::chrono::milliseconds (1500));
        auto snap = live.getSnapshot();
        const bool same = snap.hasResult && snap.result.clear == ref.clear && snap.result.bestGuess == ref.bestGuess
                          && snap.result.resonances == ref.resonances && snap.result.fits == ref.fits;
        auto name = [] (const kd::KeyResult& k) { return k.clear ? k.key : "(" + k.reason + ")"; };
        std::printf ("%-44s %5.0f Hz  file: %-14s looped: %-14s live (%4.1f s): %-14s %s\n",
                     file.getFileName().toRawUTF8(), sr, name (single).c_str(), name (ref).c_str(), snap.secondsHeard,
                     snap.hasResult ? (snap.result.clear ? snap.result.key.c_str() : ("(" + snap.result.reason + ")").c_str()) : "listening",
                     same ? "OK" : "DIFF");
        if (snap.hasResult && ! snap.result.clear)
            std::printf ("    resonances file: %s | looped: %s | live: %s | fits live: %s\n",
                         kd::joinWithDots (single.resonances).c_str(), kd::joinWithDots (ref.resonances).c_str(),
                         kd::joinWithDots (snap.result.resonances).c_str(), kd::joinWithDots (snap.result.fits).c_str());
        failures += same ? 0 : 1;

        live.requestReset();
        std::this_thread::sleep_for (std::chrono::milliseconds (200));
        auto afterReset = live.getSnapshot();
        if (afterReset.hasResult || afterReset.secondsHeard > 0.0)
        {
            std::printf ("  reset did not clear the result\n");
            ++failures;
        }
    }
    std::printf (failures == 0 ? "all OK\n" : "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
