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
        const float uiScale = (float) getHeight() / 26.0f;
        const bool active = getToggleState();

        if (active)
        {
            g.setColour (wavelens::ui::kPurpleAccent.withAlpha (0.16f));
            g.fillRoundedRectangle (b.expanded (1.5f * uiScale), 5.0f * uiScale);
        }

        juce::Colour bg = active ? wavelens::ui::kSelectedAccent.withAlpha (0.26f)
                                 : wavelens::ui::kControlBg.withAlpha (0.92f);
        if (shouldDrawButtonAsDown)
            bg = bg.brighter (0.16f);
        else if (shouldDrawButtonAsHighlighted)
            bg = active ? bg.brighter (0.10f) : wavelens::ui::kHoverBg;

        g.setColour (bg);
        g.fillRoundedRectangle (b, 4.0f * uiScale);

        g.setColour (active ? wavelens::ui::kCyanAccent.withAlpha (0.90f) : wavelens::ui::kBorder);
        g.drawRoundedRectangle (b, 4.0f * uiScale, 1.15f * uiScale);

        // Multi-colour mini accent bar underneath M when active.
        if (active)
        {
            const float barW = b.getWidth() - 10.0f * uiScale;
            const float segW = barW / 6.0f;
            const float barY = b.getBottom() - 3.5f * uiScale;
            for (int i = 0; i < 6; ++i)
            {
                g.setColour (wavelens::ui::layerColour (i).withAlpha (0.92f));
                g.fillRect (b.getX() + 5.0f * uiScale + (float) i * segW,
                            barY, segW - 0.5f * uiScale, 1.8f * uiScale);
            }
        }

        g.setColour (active ? wavelens::ui::kMainText : wavelens::ui::kSecondaryText);
        g.setFont (juce::Font (13.0f * uiScale, juce::Font::bold));
        g.drawText ("M", b.withTrimmedBottom (active ? 3.0f * uiScale : 0.0f),
                    juce::Justification::centred);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterButton)
};
