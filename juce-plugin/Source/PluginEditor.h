#pragma once

#include "PluginProcessor.h"
#include "LayerEnvelopeComponent.h"

/** Trace colour of signal n (0-based). */
inline juce::Colour signalColour (int n)
{
    static const juce::uint32 table[] = { 0xff8b7cff,   // PRE 1: violet
                                          0xffa78bfa,   // PRE 2: light violet
                                          0xffc084fc,   // PRE 3: lavender
                                          0xffc06cff,   // MAIN 1: magenta-violet
                                          0xffd58cff,   // MAIN 2: light magenta
                                          0xff9b8cff }; // MAIN 3: periwinkle
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
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void updateEnabledStates();
    void updateViewVisibility();
    void selectLayer (int layer);
    int selectedLayerForGroup (int group) const noexcept;
    void chooseLoadSample (int group);
    void loadFileIntoLayer (int layer, const juce::File& file);
    void setupCombo (juce::ComboBox&, const char* paramId, std::unique_ptr<ComboAtt>&);
    void setupToggle (juce::TextButton&, const juce::String& text, const char* paramId,
                      std::unique_ptr<ButtonAtt>&);

    WaveLensProcessor& proc;
    WaveLensLookAndFeel lnf;

    ScopeView scope;
    LayerOverlayView overlay;
    MeterView meter;

    juce::ComboBox viewBox, layoutBox;
    juce::TextButton freezeBtn, holdBtn, resetBtn;
    juce::TextButton masterBtn, monitorBtn, triggerBtn;
    juce::TextButton layerSelectBtn[WaveLensProcessor::kMaxSignals];
    juce::TextButton mutePadBtn[WaveLensProcessor::kMaxSignals];
    juce::TextButton auditionBtn[WaveLensProcessor::kMaxSignals];
    juce::TextButton soloPadBtn[WaveLensProcessor::kMaxSignals];
    juce::TextButton sigBtn[WaveLensProcessor::kMaxSignals];
    std::array<juce::TextButton, 2> loadSampleBtn, clearSampleBtn;
    juce::Slider preLeadSlider;

    std::array<std::unique_ptr<LayerEnvelopeComponent>, WaveLensProcessor::kMaxSignals> envelopeComponents;
    std::array<juce::String, WaveLensProcessor::kMaxSignals> displayedSampleNames;
    int selectedPreLayer = 0;
    int selectedMainLayer = 3;
    int lastSelectedLayer = 0;
    bool monitorMode = false;

    juce::Rectangle<int> topBarArea, waveformArea, triggerBarArea;
    juce::Rectangle<int> prePanelArea, mainPanelArea;
    juce::Rectangle<int> preHeaderArea, mainHeaderArea;
    juce::Rectangle<int> preBodyArea, mainBodyArea;
    juce::Rectangle<int> padCellBounds[WaveLensProcessor::kMaxSignals];

    std::unique_ptr<ComboAtt> viewAtt, layoutAtt;
    std::unique_ptr<SliderAtt> preLeadAtt;
    std::unique_ptr<ButtonAtt> freezeAtt, holdAtt;
    std::unique_ptr<ButtonAtt> sigAtt[WaveLensProcessor::kMaxSignals];
    std::unique_ptr<ButtonAtt> muteAtt[WaveLensProcessor::kMaxSignals];
    std::unique_ptr<ButtonAtt> soloAtt[WaveLensProcessor::kMaxSignals];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveLensEditor)
};