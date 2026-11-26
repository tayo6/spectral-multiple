#pragma once

#include "SpectralMultipleLookAndFeel.h"

class MasterButton final : public juce::Button
{
public:
    MasterButton() : juce::Button ("Master")
    {
        setClickingTogglesState (true);
        setTooltip ("Master (M): Toggle all six Snapback & Transient waveforms overlaid on the timeline");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        const bool active = getToggleState();
        namespace ui = wavelens::ui;

        // Dark card with accent stroke; latches blue when the all-layer
        // overlay is on. Small multi-colour accent bar appears under "M".
        juce::Colour bg = active ? snap::tile : snap::inset;
        if (shouldDrawButtonAsDown)
            bg = bg.brighter (0.08f);
        else if (shouldDrawButtonAsHighlighted)
            bg = active ? bg.brighter (0.06f) : ui::kHoverBg;

        g.setColour (bg);
        g.fillRoundedRectangle (b, 2.0f);

        g.setColour (active ? snap::snap : juce::Colour (0xff2b2836));
        g.drawRoundedRectangle (b, 2.0f, 1.0f);

        // Multi-colour mini accent bar underneath M when active
        if (active)
        {
            const float barW = b.getWidth() - 10.0f;
            const float segW = barW / 6.0f;
            const float barY = b.getBottom() - 3.5f;
            for (int i = 0; i < 6; ++i)
            {
                g.setColour (wavelens::ui::layerColour (i).withAlpha (0.92f));
                g.fillRect (b.getX() + 5.0f + (float) i * segW, barY, segW - 0.5f, 1.8f);
            }
        }

        g.setColour (active ? wavelens::ui::kMainText : wavelens::ui::kSecondaryText);
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText ("M", b.withTrimmedBottom (active ? 3.0f : 0.0f), juce::Justification::centred);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterButton)
};
