#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Look & feel oscuro azul electrico (maqueta de Manana Vemos)
class LabLook : public juce::LookAndFeel_V4
{
public:
    LabLook()
    {
        setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffb9c8da));
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xff111b28));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2b78c9));
        setColour (juce::TextButton::textColourOffId, juce::Colour (0xffb9c8da));
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff111b28));
        setColour (juce::ComboBox::textColourId, juce::Colour (0xffd8e4f2));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff223550));
        setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff4aa8ff));
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff0c1118));
        setColour (juce::PopupMenu::textColourId, juce::Colour (0xffd8e4f2));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2b78c9));
        setColour (juce::ToggleButton::textColourId, juce::Colour (0xffb9c8da));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (9.0f);
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float ang = startAngle + pos * (endAngle - startAngle);
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);

        juce::Path track;
        track.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff1a2638));
        g.strokePath (track, juce::PathStrokeType (3.0f));

        juce::Path val;
        val.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, ang, true);
        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (val, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (juce::Colour (0xff0b1018));
        g.fillEllipse (c.x - r + 7.0f, c.y - r + 7.0f, (r - 7.0f) * 2.0f, (r - 7.0f) * 2.0f);
        g.setColour (juce::Colour (0xff223550));
        g.drawEllipse (c.x - r + 7.0f, c.y - r + 7.0f, (r - 7.0f) * 2.0f, (r - 7.0f) * 2.0f, 1.0f);

        juce::Path needle;
        needle.addRectangle (-1.25f, -(r - 8.0f), 2.5f, (r - 8.0f) * 0.5f);
        g.setColour (juce::Colours::white);
        g.fillPath (needle, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        const bool on = b.getToggleState();
        const juce::Rectangle<float> pill (r.getX() + 2.0f, r.getCentreY() - 8.0f, 32.0f, 16.0f);
        g.setColour (on ? juce::Colour (0xff2b78c9) : juce::Colour (0xff17212f));
        g.fillRoundedRectangle (pill, 8.0f);
        g.setColour (juce::Colour (0xff223550));
        g.drawRoundedRectangle (pill, 8.0f, 1.0f);
        const float kx = on ? pill.getRight() - 14.0f : pill.getX() + 2.0f;
        g.setColour (on ? juce::Colours::white : juce::Colour (0xff6d8199));
        g.fillEllipse (kx, pill.getY() + 2.0f, 12.0f, 12.0f);
        if (b.getButtonText().isNotEmpty())
        {
            g.setColour (findColour (juce::ToggleButton::textColourId));
            g.setFont (12.0f);
            g.drawText (b.getButtonText(), r.withTrimmedLeft (42.0f).toNearestInt(), juce::Justification::centredLeft);
        }
    }
};
