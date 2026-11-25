#pragma once

#include "PluginProcessor.h"
#include "SpectralMultipleLookAndFeel.h"

/**
    Single horizontal Snapback-inspired dual-handle HP/LP filter bar.
    Left handle adjusts high-pass cutoff; right handle adjusts low-pass cutoff.
*/
class FilterControlComponent final : public juce::Component,
                                     public juce::SettableTooltipClient,
                                     private juce::Timer
{
public:
    FilterControlComponent (WaveLensProcessor& processor,
                            const juce::String& highPassParamId,
                            const juce::String& lowPassParamId,
                            juce::Colour accentColour,
                            float minHpHz = 20.0f,
                            float maxHpHz = 1000.0f,
                            float minLpHz = 1000.0f,
                            float maxLpHz = 20000.0f);
    ~FilterControlComponent() override;

    void setAccentColour (juce::Colour nextColour) noexcept
    {
        accent = nextColour;
        repaint();
    }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class DragHandle { None, HighPass, LowPass };

    void timerCallback() override;
    juce::Rectangle<float> getTrackBounds() const noexcept;
    float hzToNorm (float hz) const noexcept;
    float normToHz (float norm) const noexcept;
    void applyValue (DragHandle handle, float hzValue);

    WaveLensProcessor& proc;
    juce::String hpId, lpId;
    std::atomic<float>* pHp = nullptr;
    std::atomic<float>* pLp = nullptr;
    juce::Colour accent;
    float hpMin = 20.0f, hpMax = 1000.0f;
    float lpMin = 1000.0f, lpMax = 20000.0f;
    DragHandle activeHandle = DragHandle::None;
    DragHandle hoveredHandle = DragHandle::None;
    float lastPaintedHp = -1.0f;
    float lastPaintedLp = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterControlComponent)
};
