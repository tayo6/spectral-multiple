#pragma once

#include "PluginProcessor.h"
#include "SpectralMultipleLookAndFeel.h"
#include "FilterControlComponent.h"
#include <functional>
#include <memory>

/**
    Horizontal Trigger Settings drawer revealed by the separate TRIG control in WaveformControlBar.
*/
class TriggerPanelComponent final : public juce::Component, private juce::Timer
{
public:
    explicit TriggerPanelComponent (WaveLensProcessor& processor);
    ~TriggerPanelComponent() override;

    std::function<void (double barsCount)> onZoomBarsSelected;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setAlgorithm (int algoIndex);

    WaveLensProcessor& proc;

    juce::Slider thresholdSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAtt;

    juce::ComboBox modeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;

    juce::TextButton drumsBtn { "DRUMS" };
    juce::TextButton generalBtn { "GENERAL" };
    juce::TextButton complexBtn { "COMPLEX" };
    int activeAlgo = 0;

    FilterControlComponent inputFilter;

    juce::ComboBox viewBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> viewAtt;

    juce::ComboBox spanBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> spanAtt;

    juce::TextButton listenBtn { "MONITOR IN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAtt;

    uint32_t lastTrigCount = 0;
    float ledFlashAlpha = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggerPanelComponent)
};

//==============================================================================
/**
    36 px control bar directly beneath the main oscilloscope:
    Left:   Limiter toggle + live peak ceiling dB + PRE lead slider
    Centre: Trapezoid TRIGGER settings drawer tab + quick-fire trigger button
    Right:  GRID / SECONDS ruler toggle, DETAIL magnifier toggle,
            zoom out / zoom level / zoom in, OUTPUT monitor toggle
*/
class WaveformControlBar final : public juce::Component, private juce::Timer
{
public:
    explicit WaveformControlBar (WaveLensProcessor& processor);
    ~WaveformControlBar() override;

    std::function<void (bool triggerPanelOpen)> onTriggerPanelToggled;
    std::function<void (bool collapsed)> onCollapseToggled;
    std::function<void (bool detailActive)> onDetailToggled;
    std::function<void (float zoomFactor)> onZoomStep;
    std::function<void()> onZoomReset;
    std::function<void (bool gridMode)> onGridModeChanged;
    std::function<void (bool outputActive)> onOutputToggled;

    void setDetailActive (bool active) noexcept;
    void setOutputActive (bool active) noexcept;
    void setGridMode (bool isGrid) noexcept;
    void setZoomLevel (float zoomFactor) noexcept;
    void setCollapsedHint (bool collapsed) noexcept { editorsCollapsedHint = collapsed; repaint(); }
    bool isTriggerPanelOpen() const noexcept { return triggerSettingsBtn.getToggleState(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class MagnifierButton final : public juce::Button
    {
    public:
        MagnifierButton() : juce::Button ("Detail")
        {
            setClickingTogglesState (true);
            setTooltip ("Detail: Zoom in around trigger anchor");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    class WaveformIconButton final : public juce::Button
    {
    public:
        WaveformIconButton() : juce::Button ("Output")
        {
            setClickingTogglesState (true);
            setTooltip ("Toggle live summed Master Output monitor");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    void timerCallback() override;

    /** Painted strip height (28 of the bar's 40 SVG units). */
    int stripHeight() const noexcept { return juce::roundToInt ((float) getHeight() * 28.0f / 40.0f); }

    WaveLensProcessor& proc;

    juce::TextButton limiterBtn { "LIMITER" };
    juce::Slider preLeadSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preLeadAtt;

    juce::TextButton collapseTabBtn { "COLLAPSE" };
    juce::TextButton triggerSettingsBtn { "TRIG" };
    juce::TextButton fireTriggerBtn { ">" };

    juce::TextButton gridBtn { "GRID" };
    juce::TextButton secondsBtn { "SECONDS" };
    MagnifierButton detailBtn;
    juce::TextButton zoomOutBtn { "-" };
    juce::TextButton zoomLevelBtn { "1.0x" };
    juce::TextButton zoomInBtn { "+" };
    WaveformIconButton outputBtn;

    bool isGridMode = true;
    bool editorsCollapsedHint = false; // chevron direction hint (set by editor)
    juce::String zoomText { "1/1" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformControlBar)
};
