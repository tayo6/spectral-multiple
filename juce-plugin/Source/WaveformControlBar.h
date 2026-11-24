#pragma once

#include "PluginProcessor.h"
#include "SpectralMultipleLookAndFeel.h"
#include "FilterControlComponent.h"
#include <functional>
#include <memory>

/**
    Horizontal Trigger Settings drawer revealed when clicking the TRIGGER tab in WaveformControlBar.
*/
class TriggerPanelComponent final : public juce::Component, private juce::Timer
{
public:
    explicit TriggerPanelComponent (WaveLensProcessor& processor);
    ~TriggerPanelComponent() override;

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
    Centre: TRIGGER settings drawer tab + quick-fire trigger button
    Right:  GRID/SEC toggle, DETAIL magnifying-glass toggle, ZOOM dropdown, OUTPUT toggle
*/
class WaveformControlBar final : public juce::Component, private juce::Timer
{
public:
    explicit WaveformControlBar (WaveLensProcessor& processor);
    ~WaveformControlBar() override;

    std::function<void (bool triggerPanelOpen)> onTriggerPanelToggled;
    std::function<void (bool detailActive)> onDetailToggled;
    std::function<void (double barsCount)> onZoomBarsSelected;
    std::function<void (bool gridMode)> onGridModeChanged;
    std::function<void (bool outputActive)> onOutputToggled;

    void setDetailActive (bool active) noexcept;
    void setOutputActive (bool active) noexcept;
    void setGridMode (bool isGrid) noexcept;
    bool isTriggerPanelOpen() const noexcept { return triggerTabBtn.getToggleState(); }

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

    void timerCallback() override;

    WaveLensProcessor& proc;

    juce::TextButton limiterBtn { "LIMITER" };
    juce::Slider preLeadSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preLeadAtt;

    juce::TextButton triggerTabBtn { "TRIGGER" };
    juce::TextButton fireTriggerBtn { ">" };

    juce::TextButton gridSecBtn { "GRID" };
    MagnifierButton detailBtn;
    juce::ComboBox zoomBox;
    juce::TextButton outputBtn { "OUTPUT" };

    bool isGridMode = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformControlBar)
};
