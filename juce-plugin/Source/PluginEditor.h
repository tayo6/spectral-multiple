#pragma once

#include "PluginProcessor.h"
#include "SpectralMultipleLookAndFeel.h"
#include "PluginHeaderComponent.h"
#include "MasterWaveformComponent.h"
#include "WaveformControlBar.h"
#include "SampleGroupComponent.h"
#include <array>
#include <functional>

class LayerPadStripComponent final : public juce::Component
{
public:
    explicit LayerPadStripComponent (WaveLensProcessor& processor);
    std::function<void (int layerIndex)> onLayerSelected;
    void setSelectedLayers (int snapbackLayer, int transientLayer);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class PadCell final : public juce::Component
    {
    public:
        PadCell (WaveLensProcessor&, int layerIndex, std::function<void()> selectCallback);
        void setSelected (bool selected);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        const int layer;
        const juce::Colour accent;
        bool isSelected = false;
        juce::TextButton selectButton;
        juce::TextButton muteButton { "M" };
        juce::TextButton soloButton { "S" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment, soloAttachment;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadCell)
    };

    std::array<std::unique_ptr<PadCell>, 6> pads;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerPadStripComponent)
};

/**
    Spectral Multiple editor:
      1. Top toolbar (PluginHeaderComponent)
      2. Main oscilloscope with global M toggle + control bar + Trigger drawer
      3. Two equal side-by-side sample sections: SNAPBACK ●●● and TRANSIENT ●●●
*/
class WaveLensEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit WaveLensEditor (WaveLensProcessor&);
    ~WaveLensEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    WaveLensProcessor& proc;
    SpectralMultipleLookAndFeel lnf;

    PluginHeaderComponent header;
    MasterWaveformComponent masterWaveform;
    WaveformControlBar controlBar;
    TriggerPanelComponent triggerPanel;
    SampleGroupComponent snapbackGroup;
    SampleGroupComponent transientGroup;
    LayerPadStripComponent padStrip;

    bool triggerPanelOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveLensEditor)
};