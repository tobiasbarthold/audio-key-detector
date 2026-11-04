// Key Detector plugin: passes audio through unchanged and shows the key of what it hears.
//
// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#pragma once

#include "LiveAnalyzer.h"

#include <juce_audio_processors/juce_audio_processors.h>

class KeyDetectorProcessor : public juce::AudioProcessor
{
public:
    KeyDetectorProcessor();
    ~KeyDetectorProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    LiveAnalyzer& getAnalyzer() noexcept { return analyzer; }
    std::atomic<bool> resetOnPlay { true };   // start over when the host transport starts

private:
    LiveAnalyzer analyzer;
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyDetectorProcessor)
};
