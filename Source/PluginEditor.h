#pragma once

#include "PluginProcessor.h"
#include "LayerEnvelopeComponent.h"

/** Trace colour of signal n (0-based). Chosen to stay apart on a dark background. */
inline juce::Colour signalColour (int n)
{
    static const juce::uint32 table[] = { 0xffffb04a,   // Pre 1: amber
                                          0xffffc36b,   // Pre 2: light amber
                                          0xffff8c38,   // Pre 3: deep orange
                                          0xff4fd1ff,   // Main 1: cyan
                                          0xff78deff,   // Main 2: light cyan
                                          0xff22b7e6 }; // Main 3: deep cyan
    return juce::Colour (table[juce::jlimit (0, 5, n)]);
}

//==============================================================================
/** The oscilloscope display. */
class ScopeView final : public juce::Component, private juce::Timer
{
public:
    explicit ScopeView (WaveLensProcessor&);
    ~ScopeView() override { stopTimer(); }

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }

    struct Snapshot
    {
        int  head = 0, passStart = 0, filled = 0, heldFrom = 0, heldTo = 0;
        bool has = false, heldValid = false;
    };

    struct Trace { int ch; juce::Colour colour; };

    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>, const std::vector<Trace>&, bool);
    void drawLane (juce::Graphics&, juce::Rectangle<float> lane, const std::vector<Trace>&,
                   const Snapshot&, float gain, bool showHeld, const juce::String& title);
    void drawTrace (juce::Graphics&, juce::Rectangle<float> lane, int ch, juce::Colour,
                    const Snapshot&, float gain, bool held, float fillAlpha);
    juce::String windowLabel() const;

    WaveLensProcessor& proc;
    std::atomic<float>* pView;
    std::atomic<float>* pCapture;
    std::atomic<float>* pBars;
    std::atomic<float>* pTimeMs;
    std::atomic<float>* pGain;
    std::atomic<float>* pLayout;
    std::atomic<float>* pShowSig[WaveLensProcessor::kMaxSignals];
    std::atomic<float>* pFreeze;
    std::atomic<float>* pHold;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeView)
};

//==============================================================================
/** Peak / RMS / Crest meter. Click to cycle the mode. */
class MeterView final : public juce::Component, private juce::Timer
{
public:
    explicit MeterView (WaveLensProcessor&);
    ~MeterView() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }

    WaveLensProcessor& proc;
    std::atomic<float>* pMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterView)
};

//==============================================================================
class WaveLensLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    WaveLensLookAndFeel();
};

//==============================================================================
class WaveLensEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit WaveLensEditor (WaveLensProcessor&);
    ~WaveLensEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateEnabledStates();

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void setupCombo (juce::ComboBox&, const char* paramId, std::unique_ptr<ComboAtt>&);
    void setupToggle (juce::TextButton&, const juce::String& text, const char* paramId,
                      std::unique_ptr<ButtonAtt>&);

    WaveLensProcessor& proc;
    WaveLensLookAndFeel lnf;

    ScopeView scope;
    MeterView meter;

    juce::ComboBox viewBox, captureBox, barsBox, inputBox, layoutBox, triggerModeBox;
    juce::Slider timeSlider, gainSlider, loSlider, hiSlider, thresholdSlider;
    juce::TextButton listenBtn, freezeBtn, holdBtn, resetBtn;
    juce::TextButton sigBtn[WaveLensProcessor::kMaxSignals];
    juce::TextButton loadSampleBtn[WaveLensProcessor::kMaxSignals];
    juce::Slider layerGainSlider[WaveLensProcessor::kMaxSignals];
    juce::Label layerHeader[WaveLensProcessor::kMaxSignals];
    std::array<std::unique_ptr<LayerEnvelopeComponent>, WaveLensProcessor::kMaxSignals> envelopeComponents;
    std::array<juce::String, WaveLensProcessor::kMaxSignals> displayedSampleNames;

    std::unique_ptr<ComboAtt>  viewAtt, captureAtt, barsAtt, inputAtt, layoutAtt, triggerModeAtt;
    std::unique_ptr<SliderAtt> timeAtt, gainAtt, loAtt, hiAtt, thresholdAtt;
    std::unique_ptr<ButtonAtt> listenAtt, freezeAtt, holdAtt;
    std::unique_ptr<ButtonAtt> sigAtt[WaveLensProcessor::kMaxSignals];
    std::unique_ptr<SliderAtt> layerGainAtt[WaveLensProcessor::kMaxSignals];

    struct Caption { juce::String text; juce::Rectangle<int> area; };
    std::vector<Caption> captions;
    struct Group { juce::String title; juce::Rectangle<int> area; };
    std::vector<Group> groups;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveLensEditor)
};
