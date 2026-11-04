// Renders the plugin editor and the app window to PNG files (no DAW needed), e.g. to check the
// layout:  KeyDetectorSnapshots <out dir> <audio file for the plugin> <audio files for the app...>
//
// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#include "../Source/App/MainComponent.h"
#include "../Source/Plugin/PluginEditor.h"
#include "../Source/Plugin/PluginProcessor.h"

#include <thread>

static void save (juce::Component& c, const juce::File& file)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (img, out);
    std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
}

static void pump (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

int main (int argc, char* argv[])
{
    if (argc < 3)
        return 1;
    juce::ScopedJuceInitialiser_GUI init;
    juce::File outDir (juce::String::fromUTF8 (argv[1]));
    outDir.createDirectory();

    KeyDetectorProcessor proc;
    proc.setPlayConfigDetails (2, 2, 44100.0, 512);
    proc.prepareToPlay (44100.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    pump (300);
    save (*editor, outDir.getChildFile ("plugin_listening.png"));

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto feed = [&] (const juce::File& f, double seconds)
    {
        auto audio = kd::loadAudioFile (f, formats);
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        size_t pos = 0;
        for (int i = 0; i < (int) (seconds * 44100.0 / 512); ++i)
        {
            for (int s = 0; s < 512; ++s, ++pos)
            {
                const float v = audio.samples[pos % audio.samples.size()];
                block.setSample (0, s, v);
                block.setSample (1, s, v);
            }
            proc.processBlock (block, midi);
            if (i % 8 == 0)
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        pump (1600);
    };
    feed (juce::File (juce::String::fromUTF8 (argv[2])), 12.0);
    save (*editor, outDir.getChildFile ("plugin_key.png"));
    if (argc > 3)
    {
        proc.getAnalyzer().requestReset();
        pump (200);
        juce::File noise (juce::String::fromUTF8 (argv[3]));
        feed (noise, 8.0);
        save (*editor, outDir.getChildFile ("plugin_noise.png"));
    }
    editor = nullptr;

    MainComponent app;
    save (app, outDir.getChildFile ("app_empty.png"));
    juce::StringArray paths;
    for (int i = 2; i < argc; ++i)
        paths.add (juce::String::fromUTF8 (argv[i]));
    app.addPaths (paths);
    pump (4000);
    save (app, outDir.getChildFile ("app_results.png"));
    return 0;
}
