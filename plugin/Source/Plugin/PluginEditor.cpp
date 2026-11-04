// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#include "PluginEditor.h"

KeyDetectorEditor::KeyDetectorEditor (KeyDetectorProcessor& p)
    : AudioProcessorEditor (&p), keyProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    resetButton.setTooltip ("Forget everything heard so far and start over");
    resetButton.onClick = [this] { keyProcessor.getAnalyzer().requestReset(); };
    addAndMakeVisible (resetButton);

    resetOnPlayToggle.setToggleState (keyProcessor.resetOnPlay.load(), juce::dontSendNotification);
    resetOnPlayToggle.onClick = [this] { keyProcessor.resetOnPlay.store (resetOnPlayToggle.getToggleState()); };
    addAndMakeVisible (resetOnPlayToggle);

    setSize (420, 260);
    timerCallback();
    startTimerHz (10);
}

KeyDetectorEditor::~KeyDetectorEditor()
{
    setLookAndFeel (nullptr);
}

void KeyDetectorEditor::timerCallback()
{
    auto s = keyProcessor.getAnalyzer().getSnapshot();
    if (s.serial != shownSerial)
    {
        snap = s;
        shownSerial = s.serial;
        repaint();
    }
}

void KeyDetectorEditor::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (background);
    auto area = getLocalBounds().reduced (16, 12);

    // header
    auto header = area.removeFromTop (20);
    g.setColour (textFaint);
    g.setFont (font (11.0f, true));
    g.drawText ("KEY DETECTOR", header, juce::Justification::centredLeft);
    juce::String status;
    if (snap.secondsHeard > 0.0)
        status = juce::String (snap.secondsHeard, 1) + " s heard";
    g.drawText (status, header, juce::Justification::centredRight);

    // card
    area.removeFromTop (6);
    auto card = area.removeFromTop (152);
    g.setColour (panel);
    g.fillRoundedRectangle (card.toFloat(), 10.0f);
    auto inner = card.reduced (16, 10);
    const auto& r = snap.result;

    if (! snap.hasResult)
    {
        g.setColour (textDim);
        g.setFont (font (34.0f, true));
        g.drawText (juce::CharPointer_UTF8 ("listening\xe2\x80\xa6"), inner.removeFromTop (78), juce::Justification::centred);
        g.setFont (font (13.0f));
        g.setColour (textFaint);
        g.drawText ("Play the clip - the key appears after 2 s of audio.", inner.removeFromTop (22),
                    juce::Justification::centred);
    }
    else if (! r.clear)
    {
        g.setColour (textDim);
        g.setFont (font (34.0f, true));
        const bool extras = ! r.resonances.empty();
        g.drawText ("no clear key", inner.removeFromTop (extras ? 56 : 78), juce::Justification::centred);
        g.setFont (font (13.0f));
        g.setColour (textFaint);
        g.drawText (r.reason, inner.removeFromTop (extras ? 20 : 22), juce::Justification::centred);
        if (extras)
        {
            // what can still be said about the sound: its strongest resonances and the keys they fit
            const juce::String res = juce::String ("resonances: ")
                                     + juce::String (juce::CharPointer_UTF8 (kd::joinWithDots (r.resonances).c_str()));
            const juce::String fits = juce::String (r.fitsPartial ? "fits (partly): " : "fits: ")
                                      + juce::String (juce::CharPointer_UTF8 (kd::joinWithDots (r.fits).c_str()));
            g.setFont (font (13.0f, true));
            g.setColour (textDim);
            g.drawText (res, inner.removeFromTop (20), juce::Justification::centred, true);
            g.setFont (font (12.5f));
            g.drawText (fits, inner.removeFromTop (20), juce::Justification::centred, true);
        }
    }
    else
    {
        // big key name
        g.setColour (text);
        g.setFont (font (44.0f, true));
        g.drawText (r.key, inner.removeFromTop (62), juce::Justification::centred);

        // Camelot pill + second key
        auto row = inner.removeFromTop (30);
        const juce::String cam (r.camelot);
        const juce::String second = juce::String ("2nd: ") + r.secondKey + "  " + r.secondCamelot;
        g.setFont (font (13.0f));
        const int secondW = juce::GlyphArrangement::getStringWidthInt (font (13.0f), second) + 4;
        const int pillW = 54, gap = 14;
        const int x0 = row.getCentreX() - (pillW + gap + secondW) / 2;
        auto pill = juce::Rectangle<int> (x0, row.getY() + 2, pillW, 26).toFloat();
        g.setColour (camelotColour (cam));
        g.fillRoundedRectangle (pill, 13.0f);
        g.setColour (background);
        g.setFont (font (15.0f, true));
        g.drawText (cam, pill, juce::Justification::centred);
        g.setColour (textDim);
        g.setFont (font (13.0f));
        g.drawText (second, juce::Rectangle<int> (x0 + pillW + gap, row.getY(), secondW, row.getHeight()),
                    juce::Justification::centredLeft);
    }

    // confidence bar
    if (snap.hasResult && r.clear)
    {
        inner.removeFromTop (10);
        auto bar = inner.removeFromTop (22);
        g.setFont (font (12.0f));
        g.setColour (textDim);
        g.drawText ("Confidence", bar.removeFromLeft (78), juce::Justification::centredLeft);
        g.drawText (juce::String (juce::roundToInt (r.confidence * 100.0f)) + " %", bar.removeFromRight (44),
                    juce::Justification::centredRight);
        auto track = bar.reduced (6, 7).toFloat();
        g.setColour (panelHi);
        g.fillRoundedRectangle (track, track.getHeight() / 2);
        g.setColour (accent);
        g.fillRoundedRectangle (track.withWidth (juce::jmax (track.getHeight(), track.getWidth() * r.confidence)),
                                track.getHeight() / 2);
    }
}

void KeyDetectorEditor::resized()
{
    auto area = getLocalBounds().reduced (16, 12);
    auto footer = area.removeFromBottom (32);
    resetButton.setBounds (footer.removeFromLeft (96));
    footer.removeFromLeft (12);
    resetOnPlayToggle.setBounds (footer);
}
