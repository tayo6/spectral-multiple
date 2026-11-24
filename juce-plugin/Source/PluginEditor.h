#pragma once

#include "PluginProcessor.h"
#include "LayerEnvelopeComponent.h"

/** Trace colour of signal n (0-based). */
inline juce::Colour signalColour (int n)
{
    static const juce::uint32 table[] = { 0xff8b7cff,   // SNAPBACK 1: violet
                                          0xffa78bfa,   // SNAPBACK 2: light violet
                                          0xffc084fc,   // SNAPBACK 3: lavender
                                          0xffc06cff,   // TRANSIENT 1: magenta-violet
                                          0xffd58cff,   // TRANSIENT 2: light magenta
                                          0xff9b8cff }; // TRANSIENT 3: periwinkle
    return juce::Colour (table[juce::jlimit (0, 5, n)]);
}

//==============================================================================
class LayerOverlayView final : public juce::Component, private juce::Timer
{
public:
    explicit LayerOverlayView (WaveLensProcessor&);
    ~LayerOverlayView() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    void setSelectedPair (int snapbackIdx, int transientIdx) noexcept
    {
        selectedSnapback = juce::jlimit (0, 2, snapbackIdx);
        selectedTransient = juce::jlimit (3, 5, transientIdx);
        repaint();
    }

    void setBpm (double newBpm) noexcept
    {
        bpm = juce::jlimit (40.0, 240.0, newBpm);
        repaint();
    }

    void setGridMode (bool useGrid) noexcept
    {
        gridMode = useGrid;
        repaint();
    }

    bool isGridMode() const noexcept { return gridMode; }
    float getZoomFactor() const noexcept { return zoomFactor; }

    void zoomBy (float factor, float anchorNorm = 0.5f) noexcept;
    void resetZoom() noexcept;
    void focusTrigger (float zoomIfUnzoomed = 4.0f) noexcept;

private:
    void timerCallback() override { repaint(); }
    void clampViewport (float nextStart, float nextZoom) noexcept;
    float normToX (float norm, float plotX, float plotW) const noexcept;
    float xToNorm (float x, float plotX, float plotW) const noexcept;

    WaveLensProcessor& processor;
    int selectedSnapback = 0;
    int selectedTransient = 3;
    double bpm = 120.0;
    bool gridMode = true;
    float zoomFactor = 1.0f;
    float viewStartNorm = 0.0f;
    float dragStartX = 0.0f;
    float dragStartView = 0.0f;

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
    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;
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
    void promptLoadLayer (int layer);
    void loadFileIntoLayer (int layer, const juce::File& file);
    void setupCombo (juce::ComboBox&, const char* paramId, std::unique_ptr<ComboAtt>&);
    void setupToggle (juce::TextButton&, const juce::String& text, const juce::String& paramId,
                      std::unique_ptr<ButtonAtt>&);

    WaveLensProcessor& proc;
    WaveLensLookAndFeel lnf;

    ScopeView scope;
    LayerOverlayView overlay;
    MeterView meter;

    juce::ComboBox viewBox, layoutBox;
    juce::TextButton freezeBtn, holdBtn, resetBtn;
    juce::TextButton bpmDownBtn, bpmUpBtn, triggerBtn;
    juce::TextButton gridModeBtn, secondsModeBtn, detailBtn;
    juce::TextButton masterZoomOutBtn, masterZoomResetBtn, masterZoomInBtn, outputBtn;
    juce::TextButton sbPrevBtn, sbNextBtn, sbLoadBtn, sbClearBtn;
    juce::TextButton trPrevBtn, trNextBtn, trLoadBtn, trClearBtn;

    juce::TextButton circleSelectBtn[WaveLensProcessor::kMaxSignals];
    juce::TextButton mutePadBtn[WaveLensProcessor::kMaxSignals];
    juce::TextButton auditionBtn[WaveLensProcessor::kMaxSignals];
    juce::TextButton soloPadBtn[WaveLensProcessor::kMaxSignals];
    juce::TextButton sigBtn[WaveLensProcessor::kMaxSignals];
    juce::Slider preLeadSlider;

    std::array<std::unique_ptr<LayerEnvelopeComponent>, WaveLensProcessor::kMaxSignals> envelopeComponents;
    std::array<juce::String, WaveLensProcessor::kMaxSignals> displayedSampleNames;
    double bpm = 120.0;
    int selectedSnapback = 0;
    int selectedTransient = 3;
    bool monitorMode = false;

    juce::Rectangle<int> topHeaderArea, topCanvasArea, dividerArea, monitorToolbarArea;
    juce::Rectangle<int> snapbackDeckArea, transientDeckArea;
    juce::Rectangle<int> snapbackHeaderArea, transientHeaderArea;

    std::unique_ptr<ComboAtt> viewAtt, layoutAtt;
    std::unique_ptr<SliderAtt> preLeadAtt;
    std::unique_ptr<ButtonAtt> freezeAtt, holdAtt;
    std::unique_ptr<ButtonAtt> sigAtt[WaveLensProcessor::kMaxSignals];
    std::unique_ptr<ButtonAtt> muteAtt[WaveLensProcessor::kMaxSignals];
    std::unique_ptr<ButtonAtt> soloAtt[WaveLensProcessor::kMaxSignals];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveLensEditor)
};