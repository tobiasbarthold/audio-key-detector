// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#pragma once

#include "../Ui/Theme.h"
#include "PluginProcessor.h"

class KeyDetectorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KeyDetectorEditor (KeyDetectorProcessor&);
    ~KeyDetectorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    KeyDetectorProcessor& keyProcessor;
    theme::LookAndFeel lookAndFeel;
    juce::TextButton resetButton { "Reset" };
    juce::ToggleButton resetOnPlayToggle { "Reset when playback starts" };
    LiveAnalyzer::Snapshot snap;
    juce::uint32 shownSerial = 0xffffffff;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyDetectorEditor)
};
