// Shared dark look for the plugin and the app.
//
// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
const juce::Colour background { 0xff141619 };
const juce::Colour panel      { 0xff1d2025 };
const juce::Colour panelHi    { 0xff262a31 };
const juce::Colour outline    { 0xff30353d };
const juce::Colour text       { 0xffeceef1 };
const juce::Colour textDim    { 0xff8b929c };
const juce::Colour textFaint  { 0xff5c636d };
const juce::Colour accent     { 0xff58c4dd };

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

// Colour of a Camelot number on the usual Camelot wheel (1 = teal ... rotating through hues).
inline juce::Colour camelotColour (const juce::String& code)
{
    const int n = code.getIntValue();
    if (n < 1 || n > 12)
        return accent;
    const float hue = std::fmod (0.45f + (float) (n - 1) / 12.0f, 1.0f);
    return juce::Colour::fromHSV (hue, code.endsWithChar ('A') ? 0.50f : 0.62f, 0.92f, 1.0f);
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColourScheme ({ background, panel, panel, outline, text, accent, background, accent, text });
        setColour (juce::TextButton::buttonColourId, panelHi);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, background);
        setColour (juce::ToggleButton::textColourId, textDim);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::ToggleButton::tickDisabledColourId, textFaint);
        setColour (juce::ListBox::backgroundColourId, background);
        setColour (juce::TableHeaderComponent::backgroundColourId, panel);
        setColour (juce::TableHeaderComponent::textColourId, textDim);
        setColour (juce::TableHeaderComponent::outlineColourId, outline);
        setColour (juce::ScrollBar::thumbColourId, outline);
        setColour (juce::TooltipWindow::backgroundColourId, panelHi);
        setColour (juce::TooltipWindow::textColourId, text);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = bg;
        if (down)
            c = c.brighter (0.25f);
        else if (highlighted)
            c = c.brighter (0.12f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (outline.brighter (0.1f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int height) override
    {
        return font (juce::jmin (14.0f, (float) height * 0.5f), true);
    }
};
} // namespace theme
