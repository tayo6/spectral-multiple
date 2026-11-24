#pragma once

#include "SpectralMultipleLookAndFeel.h"
#include <functional>

class SampleSelectorDot final : public juce::Component
{
public:
    SampleSelectorDot (int indexInGroup, juce::Colour dotColour, juce::String tip)
        : index (indexInGroup), colour (dotColour)
    {
        setTooltip (std::move (tip));
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setRepaintsOnMouseActivity (true);
    }

    void setSelected (bool shouldBeSelected) noexcept
    {
        if (selected != shouldBeSelected)
        {
            selected = shouldBeSelected;
            repaint();
        }
    }

    bool isSelected() const noexcept { return selected; }

    void setHasSample (bool sampleLoaded) noexcept
    {
        if (hasSample != sampleLoaded)
        {
            hasSample = sampleLoaded;
            repaint();
        }
    }

    std::function<void (int)> onSelect;

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const float dotDiameter = 13.0f;
        const auto circle = bounds.withSizeKeepingCentre (dotDiameter, dotDiameter);
        const bool hovered = isMouseOver();

        if (selected)
        {
            // Soft ambient outer glow + crisp bright selection ring
            g.setColour (colour.withAlpha (0.28f));
            g.fillEllipse (circle.expanded (4.0f));

            g.setColour (wavelens::ui::kMainText.withAlpha (0.92f));
            g.drawEllipse (circle.expanded (2.1f), 1.4f);

            g.setColour (colour);
            g.fillEllipse (circle);
        }
        else
        {
            if (hovered)
            {
                g.setColour (colour.withAlpha (0.18f));
                g.fillEllipse (circle.expanded (3.0f));
            }

            const float fillAlpha = hovered ? 0.85f : (hasSample ? 0.62f : 0.42f);
            g.setColour (colour.withAlpha (fillAlpha));
            g.fillEllipse (circle);

            g.setColour (hovered ? colour.brighter (0.25f) : wavelens::ui::kBorder.brighter (0.18f));
            g.drawEllipse (circle, 1.0f);
        }

        if (hasSample && ! selected)
        {
            g.setColour (wavelens::ui::kMainBg.withAlpha (0.85f));
            g.fillEllipse (circle.withSizeKeepingCentre (3.6f, 3.6f));
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && getLocalBounds().toFloat().contains (e.position))
        {
            if (onSelect)
                onSelect (index);
        }
    }

private:
    int index = 0;
    juce::Colour colour;
    bool selected = false;
    bool hasSample = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleSelectorDot)
};
