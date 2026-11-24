#pragma once

#include "PluginProcessor.h"
#include "SpectralMultipleLookAndFeel.h"
#include "PluginHeaderComponent.h"
#include "MasterWaveformComponent.h"
#include "WaveformControlBar.h"
#include "SampleGroupComponent.h"

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

    bool triggerPanelOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveLensEditor)
};