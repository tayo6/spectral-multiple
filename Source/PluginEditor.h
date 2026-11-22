#pragma once

#include "PluginProcessor.h"

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

    void drawLane (juce::Graphics&, juce::Rectangle<float> lane, const std::vector<Trace>&,
                   const Snapshot&, float gain, bool showHeld, const juce::String& title);
    void drawTrace (juce::Graphics&, juce::Rectangle<float> lane, int ch, juce::Colour,
                    const Snapshot&, float gain, bool held);
    juce::String windowLabel() const;

    WaveLensProcessor& proc;
    std::atomic<float>* pView;
    std::atomic<float>* pCapture;
    std::atomic<float>* pBars;
    std::atomic<float>* pTimeMs;
    std::atomic<float>* pGain;
    std::atomic<float>* pShowLow;
    std::atomic<float>* pShowMid;
    std::atomic<float>* pShowHigh;
    std::atomic<float>* pScOn;
    std::atomic<float>* pScLayout;
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

    juce::ComboBox viewBox, captureBox, barsBox, inputBox, scLayoutBox;
    juce::Slider timeSlider, gainSlider, loSlider, hiSlider;
    juce::TextButton lowBtn, midBtn, highBtn, listenBtn, scOnBtn, freezeBtn, holdBtn, resetBtn;

    std::unique_ptr<ComboAtt>  viewAtt, captureAtt, barsAtt, inputAtt, scLayoutAtt;
    std::unique_ptr<SliderAtt> timeAtt, gainAtt, loAtt, hiAtt;
    std::unique_ptr<ButtonAtt> lowAtt, midAtt, highAtt, listenAtt, scOnAtt, freezeAtt, holdAtt;

    struct Caption { juce::String text; juce::Rectangle<int> area; };
    std::vector<Caption> captions;
    struct Group { juce::String title; juce::Rectangle<int> area; };
    std::vector<Group> groups;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveLensEditor)
};
