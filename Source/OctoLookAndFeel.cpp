#include "OctoLookAndFeel.h"

namespace octo
{
    juce::Font uiFont (float height, bool bold, float tracking)
    {
        auto opts = juce::FontOptions (height).withKerningFactor (tracking);
        if (bold) opts = opts.withStyle ("Bold");
        return juce::Font (opts);
    }

    juce::Colour sliceColour (int index, int numSlices)
    {
        const float t = numSlices <= 1 ? 0.0f : (float) index / (float) (numSlices - 1);
        if (t < 0.5f)
            return colours::cyan.interpolatedWith (colours::violet, t * 2.0f);
        return colours::violet.interpolatedWith (colours::pink, (t - 0.5f) * 2.0f);
    }

    void drawGlassPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner, float glow)
    {
        // soft outer glow
        if (glow > 0.0f)
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (colours::cyan.withAlpha (0.05f * glow / (float) i));
                g.drawRoundedRectangle (r.expanded ((float) i * 2.5f), corner + (float) i * 2.5f, 2.5f);
            }

        juce::ColourGradient fill (juce::Colour (0x3a1c2458), r.getX(), r.getY(),
                                   juce::Colour (0x220a0e24), r.getX(), r.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (r, corner);

        juce::ColourGradient edge (colours::cyan.withAlpha (0.35f + 0.3f * glow), r.getX(), r.getY(),
                                   colours::violet.withAlpha (0.12f), r.getRight(), r.getBottom(), false);
        g.setGradientFill (edge);
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

        // top highlight
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + corner, r.getRight() - corner);
    }

    OctoLookAndFeel::OctoLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::textColourOnId, colours::text);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xf00a0d1e));
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::violet.withAlpha (0.35f));
    }

    void OctoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float startAngle, float endAngle, juce::Slider& s)
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        bounds = bounds.withSizeKeepingCentre (size, size);
        const auto c = bounds.getCentre();
        const float radius = size * 0.5f;
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float fromAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        const bool hover = s.isMouseOverOrDragging();

        // track
        juce::Path track;
        track.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // tick marks
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
            const auto p1 = c.getPointOnCircumference (radius + 3.0f, a);
            const auto p2 = c.getPointOnCircumference (radius + 5.0f, a);
            g.setColour (juce::Colours::white.withAlpha (a <= angle ? 0.35f : 0.1f));
            g.drawLine ({ p1, p2 }, 1.0f);
        }

        // value arc with glow
        if (std::abs (angle - fromAngle) > 0.001f)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f,
                               juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);
            juce::ColourGradient grad (colours::cyan, c.x - radius, c.y, colours::violet, c.x + radius, c.y, false);
            for (int i = 3; i >= 1; --i)
            {
                g.setGradientFill (grad);
                g.setOpacity (0.07f * (float) (4 - i) + (hover ? 0.05f : 0.0f));
                g.strokePath (arc, juce::PathStrokeType (3.0f + (float) i * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setOpacity (1.0f);
            g.setGradientFill (grad);
            g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // body
        const float bodyR = radius - 9.0f;
        juce::ColourGradient body (juce::Colour (0xff222a52), c.x, c.y - bodyR,
                                   juce::Colour (0xff070915), c.x, c.y + bodyR, false);
        g.setGradientFill (body);
        g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (hover ? 0.22f : 0.12f));
        g.drawEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c), 1.0f);

        // indicator
        const auto tip = c.getPointOnCircumference (bodyR - 4.0f, angle);
        const auto inner = c.getPointOnCircumference (bodyR * 0.35f, angle);
        g.setColour (colours::cyan.withAlpha (0.25f));
        g.drawLine ({ inner, tip }, 5.0f);
        g.setColour (juce::Colours::white);
        g.drawLine ({ inner, tip }, 1.8f);
        g.setColour (colours::cyan.withAlpha (0.6f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c.getPointOnCircumference (radius - 2.0f, angle)));
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (3.5f, 3.5f).withCentre (c.getPointOnCircumference (radius - 2.0f, angle)));
    }

    void OctoLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        const float corner = r.getHeight() * 0.5f;

        g.setColour (on ? colours::violet.withAlpha (0.22f) : juce::Colours::white.withAlpha (highlighted ? 0.06f : 0.03f));
        g.fillRoundedRectangle (r, corner);
        g.setColour (on ? colours::cyan.withAlpha (0.8f) : juce::Colours::white.withAlpha (highlighted ? 0.3f : 0.16f));
        g.drawRoundedRectangle (r, corner, 1.0f);

        const auto led = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + corner + 2.0f, r.getCentreY() });
        if (on)
        {
            g.setColour (colours::cyan.withAlpha (0.3f));
            g.fillEllipse (led.expanded (4.0f));
        }
        g.setColour (on ? colours::cyan : juce::Colours::white.withAlpha (0.2f));
        g.fillEllipse (led);

        g.setColour (on ? colours::text : colours::textDim);
        g.setFont (uiFont (10.5f, true, 0.1f));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (corner + 12.0f), juce::Justification::centredLeft);
    }

    void OctoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const float corner = r.getHeight() * 0.5f;
        juce::ColourGradient grad (colours::cyan.withAlpha (down ? 0.35f : highlighted ? 0.22f : 0.12f), r.getX(), r.getY(),
                                   colours::violet.withAlpha (down ? 0.35f : highlighted ? 0.22f : 0.12f), r.getRight(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, corner);
        g.setColour (colours::cyan.withAlpha (highlighted ? 0.85f : 0.45f));
        g.drawRoundedRectangle (r, corner, 1.0f);
    }

    void OctoLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (colours::text);
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
    }
}
