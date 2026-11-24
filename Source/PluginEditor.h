#pragma once

#include "PluginProcessor.h"
#include "LayerEnvelopeComponent.h"

/** Trace colour of signal n (0-based). */
inline juce::Colour signalColour (int n)
{
    static const juce::uint32 table[] = { 0xffffb04a,   // Pre 1: amber
                                          0xffffc36b,   // Pre 2: light amber
                                          0xffff8c38,   // Pre 3: deep orange
                                          0xff2cf0ff,   // Main 1: Ableton cyan
                                          0xff78deff,   // Main 2: light cyan
                                          0xff22b7e6 }; // Main 3: deep cyan
    return juce::Colour (table[juce::jlimit (0, 5, n)]);
}

//==============================================================================
class LayerOverlayView final : public juce::Component, private juce::Timer
{
public:
    explicit LayerOverlayView (WaveLensProcessor&);
    ~LayerOverlayView() override { stopTimer(); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }
    WaveLensProcessor& processor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerOverlayView)
};

//==============================================================================
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
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
};

//==============================================================================
class WaveLensEditor final : public juce::AudioProcessorEditor,
                             public juce::FileDragAndDropTarget,
                             private juce::Timer
{
public:
    explicit WaveLensEditor (WaveLensProcessor&);
    ~WaveLensEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void updateEnabledStates();
    void updateViewVisibility();
    void loadFileIntoLayer (int layer, const juce::File& file);

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void setupCombo (juce::ComboBox&, const char* paramId, std::unique_ptr<ComboAtt>&);
    void setupToggle (juce::TextButton&, const juce::String& text, const juce::String& paramId,
                      std::unique_ptr<ButtonAtt>&);

    WaveLensProcessor& proc;
    WaveLensLookAndFeel lnf;

    ScopeView scope;
    LayerOverlayView overlay;
    MeterView meter;

    juce::ComboBox viewBox, layoutBox;
    juce::Slider preRollSlider;
    juce::TextButton freezeBtn, holdBtn, resetBtn;
    juce::TextButton masterBtn, oneShotBtn, monitorBtn;
    juce::TextButton padMasterBtn;
    juce::TextButton layerSelectBtn[WaveLensProcessor::kMaxSignals];
    juce::TextButton mutePadBtn[WaveLensProcessor::kMaxSignals];
    juce::TextButton auditionBtn[WaveLensProcessor::kMaxSignals];
    juce::TextButton soloPadBtn[WaveLensProcessor::kMaxSignals];
    juce::TextButton sigBtn[WaveLensProcessor::kMaxSignals];
    juce::TextButton loadSampleBtn, clearSampleBtn;

    std::array<std::unique_ptr<LayerEnvelopeComponent>, WaveLensProcessor::kMaxSignals> envelopeComponents;
    std::array<juce::String, WaveLensProcessor::kMaxSignals> displayedSampleNames;
    int selectedLayer = 0; // -1 = Master, 0..5 = layer
    bool monitorMode = false;

    juce::Rectangle<int> drumRackDeviceArea, simplerDeviceArea;
    juce::Rectangle<int> drumRackHeaderArea, simplerHeaderArea;
    juce::Rectangle<int> padGridArea, modeRailArea, monitorToolbarArea, canvasArea;
    juce::Rectangle<int> padCellBounds[WaveLensProcessor::kMaxSignals];

    std::unique_ptr<ComboAtt>  viewAtt, layoutAtt;
    std::unique_ptr<SliderAtt> preRollAtt;
    std::unique_ptr<ButtonAtt> freezeAtt, holdAtt;
    std::unique_ptr<ButtonAtt> sigAtt[WaveLensProcessor::kMaxSignals];
    std::unique_ptr<ButtonAtt> muteAtt[WaveLensProcessor::kMaxSignals];
    std::unique_ptr<ButtonAtt> soloAtt[WaveLensProcessor::kMaxSignals];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveLensEditor)
};