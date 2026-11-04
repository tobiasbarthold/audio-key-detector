// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#include "PluginProcessor.h"
#include "PluginEditor.h"

KeyDetectorProcessor::KeyDetectorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool KeyDetectorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void KeyDetectorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    analyzer.prepare (sampleRate, samplesPerBlock);
}

void KeyDetectorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    for (int c = numIn; c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
        {
            const bool playing = pos->getIsPlaying();
            if (playing && ! wasPlaying && resetOnPlay.load())
                analyzer.requestReset();
            wasPlaying = playing;
        }
    }
    // audio passes through unchanged; the analyzer only copies it
    analyzer.pushAudio (buffer, numIn, buffer.getNumSamples());
}

juce::AudioProcessorEditor* KeyDetectorProcessor::createEditor()
{
    return new KeyDetectorEditor (*this);
}

void KeyDetectorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("KeyDetector");
    state.setProperty ("resetOnPlay", resetOnPlay.load(), nullptr);
    juce::MemoryOutputStream out (destData, false);
    state.writeToStream (out);
}

void KeyDetectorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (state.isValid() && state.hasProperty ("resetOnPlay"))
        resetOnPlay.store ((bool) state.getProperty ("resetOnPlay"));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KeyDetectorProcessor();
}
