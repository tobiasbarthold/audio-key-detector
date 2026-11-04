// Loads the installed VST3 and AU like a DAW does, plays an audio file through them and checks
// that the audio passes through unchanged and that state save/restore works.
//
//   KeyDetectorHostTest <audio file>
//
// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#include "../Source/Engine/AudioPrep.h"

#include <juce_audio_processors/juce_audio_processors.h>

static int runOne (juce::AudioPluginFormat& format, const juce::String& path, const std::vector<float>& audio)
{
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, path);
    if (found.isEmpty())
    {
        std::printf ("%s: no plugin found in %s\n", format.getName().toRawUTF8(), path.toRawUTF8());
        return 1;
    }
    juce::String error;
    auto plugin = format.createInstanceFromDescription (*found[0], 48000.0, 512, error);
    if (plugin == nullptr)
    {
        std::printf ("%s: cannot instantiate: %s\n", format.getName().toRawUTF8(), error.toRawUTF8());
        return 1;
    }
    plugin->setPlayConfigDetails (2, 2, 48000.0, 512);
    plugin->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    double maxDiff = 0.0;
    size_t pos = 0;
    for (int b = 0; b < 48000 * 10 / 512; ++b)
    {
        for (int s = 0; s < 512; ++s, ++pos)
        {
            block.setSample (0, s, audio[pos % audio.size()]);
            block.setSample (1, s, -0.5f * audio[pos % audio.size()]);
        }
        juce::AudioBuffer<float> copy (block);
        plugin->processBlock (block, midi);
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 512; ++s)
                maxDiff = std::max (maxDiff, (double) std::abs (block.getSample (c, s) - copy.getSample (c, s)));
    }
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    plugin->setStateInformation (state.getData(), (int) state.getSize());
    const bool hasEditor = plugin->hasEditor();
    plugin->releaseResources();
    std::printf ("%-5s %-14s latency %d, editor %s, state %d bytes, max pass-through difference %.1e  %s\n",
                 format.getName().toRawUTF8(), found[0]->name.toRawUTF8(), plugin->getLatencySamples(),
                 hasEditor ? "yes" : "no", (int) state.getSize(), maxDiff, maxDiff == 0.0 ? "OK" : "FAIL");
    return maxDiff == 0.0 && hasEditor ? 0 : 1;
}

int main (int argc, char* argv[])
{
    if (argc < 2)
        return 1;
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto audio = kd::loadAudioFile (juce::File (juce::String::fromUTF8 (argv[1])), formats).samples;
    if (audio.empty())
        return 1;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    int failures = 0;
    juce::VST3PluginFormat vst3;
    failures += runOne (vst3, home.getChildFile ("Library/Audio/Plug-Ins/VST3/Key Detector.vst3").getFullPathName(), audio);
    juce::AudioUnitPluginFormat au;
    failures += runOne (au, "AudioUnit:Effects/aufx,Kdet,Tbkd", audio);
    std::printf (failures == 0 ? "all OK\n" : "%d failure(s)\n", failures);
    return failures;
}
