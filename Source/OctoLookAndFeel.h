#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace octo
{
    namespace colours
    {
        const juce::Colour bg        { 0xff03040b };
        const juce::Colour panel     { 0x2e1a2150 };
        const juce::Colour panelEdge { 0x405ef2ff };
        const juce::Colour cyan      { 0xff5ef2ff };
        const juce::Colour violet    { 0xffa46bff };
        const juce::Colour pink      { 0xffff5fd2 };
        const juce::Colour text      { 0xffe2e8ff };
        const juce::Colour textDim   { 0x99aab4dc };
        const juce::Colour textFaint { 0x55aab4dc };
    }

    juce::Font uiFont (float height, bool bold = false, float tracking = 0.08f);

    /** Colour for a slice index, sweeping cyan -> violet -> pink. */
    juce::Colour sliceColour (int index, int numSlices);

    void drawGlassPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner = 14.0f, float glow = 0.0f);

    class OctoLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        OctoLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return uiFont (12.0f, true, 0.18f); }
    };
}
