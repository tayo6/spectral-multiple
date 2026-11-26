#include "WaveformControlBar.h"

#include <cmath>

namespace ui = wavelens::ui;

namespace
{
    constexpr auto relaxed = std::memory_order_relaxed;

    juce::String formatPeakDb (float linear)
    {
        const float db = 20.0f * std::log10 (juce::jmax (linear, 1.0e-6f));
        return db <= -90.0f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB";
    }
}

//==============================================================================
TriggerPanelComponent::TriggerPanelComponent (WaveLensProcessor& processor)
    : proc (processor),
      inputFilter (processor, ids::rangeLo, ids::rangeHi, ui::kCyanAccent, 20.0f, 10000.0f, 200.0f, 20000.0f)
{
    thresholdSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    thresholdSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
    thresholdSlider.setColour (juce::Slider::trackColourId, ui::kCyanAccent);
    thresholdSlider.textFromValueFunction = [] (double v)
    {
        const float db = 20.0f * std::log10 (juce::jmax ((float) v, 1.0e-4f));
        return juce::String (db, 1) + " dB";
    };
    addAndMakeVisible (thresholdSlider);
    thresholdAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids::threshold, thresholdSlider);
    thresholdSlider.updateText();

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (ids::triggerMode)))
        modeBox.addItemList (choice->choices, 1);
    addAndMakeVisible (modeBox);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ids::triggerMode, modeBox);

    if (auto* viewChoice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (ids::view)))
        viewBox.addItemList (viewChoice->choices, 1);
    addAndMakeVisible (viewBox);
    viewAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ids::view, viewBox);

    // Timeline bar span (moved here from the control bar so the bar can host
    // the compact zoom out / level / zoom in controls instead)
    if (auto* spanChoice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (ids::bars)))
        spanBox.addItemList (spanChoice->choices, 1);
    spanBox.setTooltip ("Timeline bar span");
    spanBox.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (spanBox);
    spanAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ids::bars, spanBox);
    spanBox.onChange = [this]
    {
        if (onZoomBarsSelected)
            onZoomBarsSelected (barsFromIndex (spanBox.getSelectedItemIndex()));
    };

    for (auto* b : { &drumsBtn, &generalBtn, &complexBtn })
    {
        b->setClickingTogglesState (false);
        b->getProperties().set ("isFlatSegment", true);
        addAndMakeVisible (*b);
    }
    drumsBtn.onClick   = [this] { setAlgorithm (0); };
    generalBtn.onClick = [this] { setAlgorithm (1); };
    complexBtn.onClick = [this] { setAlgorithm (2); };
    setAlgorithm (0);

    addAndMakeVisible (inputFilter);

    listenBtn.setClickingTogglesState (true);
    listenBtn.getProperties().set ("accentColour", (juce::int64) ui::kCyanAccent.getARGB());
    addAndMakeVisible (listenBtn);
    listenAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ids::listen, listenBtn);

    lastTrigCount = proc.getTriggerCount();
    startTimerHz (24);
}

TriggerPanelComponent::~TriggerPanelComponent()
{
    stopTimer();
}

void TriggerPanelComponent::setAlgorithm (int algoIndex)
{
    activeAlgo = juce::jlimit (0, 2, algoIndex);
    drumsBtn.setToggleState (activeAlgo == 0, juce::dontSendNotification);
    generalBtn.setToggleState (activeAlgo == 1, juce::dontSendNotification);
    complexBtn.setToggleState (activeAlgo == 2, juce::dontSendNotification);

    const float presetThresh = (activeAlgo == 0) ? 0.03f : (activeAlgo == 1 ? 0.05f : 0.015f);
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (ids::threshold)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (presetThresh));
        p->endChangeGesture();
    }
    repaint();
}

void TriggerPanelComponent::timerCallback()
{
    const auto trig = proc.getTriggerCount();
    if (trig != lastTrigCount)
    {
        lastTrigCount = trig;
        ledFlashAlpha = 1.0f;
        repaint();
    }
    else if (ledFlashAlpha > 0.0f)
    {
        ledFlashAlpha = juce::jmax (0.0f, ledFlashAlpha - 0.10f);
        repaint();
    }
}

void TriggerPanelComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ui::kPanelBg);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (ui::kBorder);
    g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

    g.setFont (juce::Font (9.5f, juce::Font::bold));
    g.setColour (ui::kSecondaryText);
    g.drawText ("THRESH", thresholdSlider.getX() - 48, 0, 44, getHeight(), juce::Justification::centredRight);
    g.drawText ("MODE", modeBox.getX() - 36, 0, 32, getHeight(), juce::Justification::centredRight);
    g.drawText ("SPAN", spanBox.getX() - 36, 0, 32, getHeight(), juce::Justification::centredRight);
    g.drawText ("IN FILTER", inputFilter.getX() - 56, 0, 52, getHeight(), juce::Justification::centredRight);

    // Trigger LED indicator on far left
    const float ledX = 16.0f;
    const float ledY = b.getCentreY();
    if (ledFlashAlpha > 0.02f)
    {
        g.setColour (ui::kCyanAccent.withAlpha (0.35f * ledFlashAlpha));
        g.fillEllipse (ledX - 7.0f, ledY - 7.0f, 14.0f, 14.0f);
    }
    g.setColour (ui::kControlBg.interpolatedWith (ui::kCyanAccent, 0.20f + 0.80f * ledFlashAlpha));
    g.fillEllipse (ledX - 4.0f, ledY - 4.0f, 8.0f, 8.0f);
    g.setColour (ui::kBorder);
    g.drawEllipse (ledX - 4.0f, ledY - 4.0f, 8.0f, 8.0f, 1.0f);
}

void TriggerPanelComponent::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    r.removeFromLeft (18); // LED space

    r.removeFromLeft (48); // THRESH label
    thresholdSlider.setBounds (r.removeFromLeft (152));
    r.removeFromLeft (44); // MODE label
    modeBox.setBounds (r.removeFromLeft (84));
    r.removeFromLeft (12);

    const int segW = 64;
    drumsBtn.setBounds (r.removeFromLeft (segW));
    r.removeFromLeft (3);
    generalBtn.setBounds (r.removeFromLeft (segW));
    r.removeFromLeft (3);
    complexBtn.setBounds (r.removeFromLeft (segW));

    listenBtn.setBounds (r.removeFromRight (84));
    r.removeFromRight (6);
    spanBox.setBounds (r.removeFromRight (76));
    r.removeFromRight (40); // SPAN label
    viewBox.setBounds (r.removeFromRight (92));
    r.removeFromRight (12);

    const int filterW = juce::jlimit (120, 220, r.getWidth() - 60);
    if (filterW >= 120)
        inputFilter.setBounds (r.removeFromRight (filterW));
}

//==============================================================================
void WaveformControlBar::MagnifierButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool active = getToggleState();

    juce::Colour bg = active ? ui::kSelectedAccent.withAlpha (0.24f) : ui::kControlBg;
    if (down)
        bg = bg.brighter (0.14f);
    else if (highlighted)
        bg = active ? bg.brighter (0.08f) : ui::kHoverBg;

    g.setColour (bg);
    g.fillRoundedRectangle (b, 3.5f);
    g.setColour (active ? ui::kPurpleAccent : ui::kBorder);
    g.drawRoundedRectangle (b, 3.5f, 1.0f);

    // Magnifying glass vector + "DETAIL" text
    const float iconCx = b.getX() + 12.0f;
    const float iconCy = b.getCentreY() - 0.5f;
    g.setColour (active ? ui::kMainText : ui::kSecondaryText);
    g.drawEllipse (iconCx - 4.0f, iconCy - 4.0f, 7.5f, 7.5f, 1.3f);
    g.drawLine (iconCx + 2.2f, iconCy + 2.2f, iconCx + 5.6f, iconCy + 5.6f, 1.4f);

    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText ("DETAIL", b.withTrimmedLeft (18.0f), juce::Justification::centred);
}

//==============================================================================
WaveformControlBar::WaveformControlBar (WaveLensProcessor& processor)
    : proc (processor)
{
    limiterBtn.setClickingTogglesState (true);
    limiterBtn.setToggleState (true, juce::dontSendNotification);
    limiterBtn.setTooltip ("Limiter protection display toggle");
    limiterBtn.getProperties().set ("accentColour", (juce::int64) ui::kPurpleAccent.getARGB());
    addAndMakeVisible (limiterBtn);

    preLeadSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    preLeadSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
    preLeadSlider.setColour (juce::Slider::trackColourId, ui::kPurpleAccent);
    preLeadSlider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " ms"; };
    preLeadSlider.setTooltip ("PRE lead offset before dry transient (0 ms to 100 ms)");
    addChildComponent (preLeadSlider);
    preLeadAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids::preRollMs, preLeadSlider);
    preLeadSlider.updateText();

    collapseTabBtn.setClickingTogglesState (true);
    collapseTabBtn.setTooltip ("Collapse or restore the lower sample editors");
    collapseTabBtn.getProperties().set ("isTrapezoid", true);
    collapseTabBtn.getProperties().set ("accentColour", (juce::int64) ui::kPurpleAccent.getARGB());
    collapseTabBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    collapseTabBtn.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    collapseTabBtn.onClick = [this]
    {
        if (onCollapseToggled)
            onCollapseToggled (collapseTabBtn.getToggleState());
    };
    addAndMakeVisible (collapseTabBtn);

    triggerSettingsBtn.setClickingTogglesState (true);
    triggerSettingsBtn.setTooltip ("Open or close Trigger Settings");
    triggerSettingsBtn.getProperties().set ("accentColour", (juce::int64) ui::kCyanAccent.getARGB());
    triggerSettingsBtn.onClick = [this]
    {
        if (onTriggerPanelToggled)
            onTriggerPanelToggled (triggerSettingsBtn.getToggleState());
    };
    addChildComponent (triggerSettingsBtn);

    fireTriggerBtn.setTooltip ("Fire manual trigger audition");
    fireTriggerBtn.getProperties().set ("accentColour", (juce::int64) ui::kCyanAccent.getARGB());
    fireTriggerBtn.onClick = [this] { proc.requestTrigger(); };
    addChildComponent (fireTriggerBtn);

    auto setupViewBtn = [this] (juce::TextButton& b, const juce::String& text,
                                juce::Colour accent, const juce::String& tip)
    {
        b.setButtonText (text);
        b.setClickingTogglesState (true);
        b.setTooltip (tip);
        b.getProperties().set ("accentColour", (juce::int64) accent.getARGB());
        addAndMakeVisible (b);
    };
    setupViewBtn (gridBtn, "GRID", ui::kPurpleAccent, "Show beat grid ruler");
    gridBtn.setVisible (false);
    setupViewBtn (secondsBtn, "SECONDS", snap::tr, "Show milliseconds ruler");
    secondsBtn.setVisible (false);
    gridBtn.onClick = [this]
    {
        setGridMode (true);
        if (onGridModeChanged)
            onGridModeChanged (isGridMode);
    };
    secondsBtn.onClick = [this]
    {
        setGridMode (false);
        if (onGridModeChanged)
            onGridModeChanged (isGridMode);
    };
    setGridMode (true);

    detailBtn.onClick = [this]
    {
        if (onDetailToggled)
            onDetailToggled (detailBtn.getToggleState());
    };
    addAndMakeVisible (detailBtn);

    zoomOutBtn.setTooltip ("Zoom out timeline");
    zoomOutBtn.onClick = [this]
    {
        if (onZoomStep)
            onZoomStep (1.0f / 1.35f);
    };
    addAndMakeVisible (zoomOutBtn);

    zoomLevelBtn.setTooltip ("Current timeline zoom (click to reset)");
    zoomLevelBtn.onClick = [this]
    {
        if (onZoomReset)
            onZoomReset();
    };
    addAndMakeVisible (zoomLevelBtn);

    zoomInBtn.setTooltip ("Zoom in timeline");
    zoomInBtn.onClick = [this]
    {
        if (onZoomStep)
            onZoomStep (1.35f);
    };
    addAndMakeVisible (zoomInBtn);

    outputBtn.setClickingTogglesState (true);
    outputBtn.setTooltip ("Toggle live summed Master Output monitor");
    outputBtn.getProperties().set ("accentColour", (juce::int64) ui::kMasterWave.getARGB());
    outputBtn.onClick = [this]
    {
        if (onOutputToggled)
            onOutputToggled (outputBtn.getToggleState());
    };
    addAndMakeVisible (outputBtn);

    startTimerHz (15);
}

WaveformControlBar::~WaveformControlBar()
{
    stopTimer();
}

void WaveformControlBar::setDetailActive (bool active) noexcept
{
    detailBtn.setToggleState (active, juce::dontSendNotification);
}

void WaveformControlBar::setOutputActive (bool active) noexcept
{
    outputBtn.setToggleState (active, juce::dontSendNotification);
}

void WaveformControlBar::setGridMode (bool isGrid) noexcept
{
    isGridMode = isGrid;
    gridBtn.setToggleState (isGrid, juce::dontSendNotification);
    secondsBtn.setToggleState (! isGrid, juce::dontSendNotification);
    repaint();
}

void WaveformControlBar::setZoomLevel (float zoomFactor) noexcept
{
    const juce::String txt = zoomFactor <= 1.05f
        ? juce::String ("1.0x")
        : (zoomFactor >= 9.95f ? juce::String (juce::roundToInt (zoomFactor)) + "x"
                               : juce::String (zoomFactor, 1) + "x");
    zoomLevelBtn.setButtonText (txt);
    zoomOutBtn.setEnabled (zoomFactor > 1.05f);
    zoomInBtn.setEnabled (zoomFactor < 31.5f);
    repaint();
}

void WaveformControlBar::timerCallback()
{
    repaint (0, 0, 175, getHeight());
}

void WaveformControlBar::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ui::kPanelBg);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (ui::kBorder);
    g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

    g.setFont (juce::Font (10.5f, juce::Font::bold));
    g.setColour (limiterBtn.getToggleState() ? ui::kMainText : ui::kSecondaryText);
    g.drawText (formatPeakDb (proc.peakNow.load (relaxed)),
                limiterBtn.getRight() + 6, 0, 64, getHeight(),
                juce::Justification::centredLeft);

    g.setColour (ui::kSecondaryText);
    g.setFont (juce::Font (9.5f));
    g.drawText ("Limiter", limiterBtn.getRight() + 4, 0, 48, getHeight(),
                juce::Justification::centredLeft);
}

void WaveformControlBar::resized()
{
    const float sx = (float) getWidth() / 800.0f;
    const float sy = (float) getHeight() / 42.0f;
    auto px = [sx] (float v) { return juce::roundToInt (v * sx); };
    auto py = [sy] (float v) { return juce::roundToInt (v * sy); };

    limiterBtn.setButtonText ("LIMITER");
    limiterBtn.setBounds (px (18), py (14), px (64), py (22));
    preLeadSlider.setBounds ({});

    collapseTabBtn.setBounds (px (338), 0, px (124), getHeight());

    detailBtn.setBounds (px (568), py (9), px (62), py (25));
    zoomOutBtn.setBounds (px (646), py (9), px (20), py (25));
    zoomLevelBtn.setBounds (px (668), py (9), px (42), py (25));
    zoomInBtn.setBounds (px (712), py (9), px (18), py (25));
    outputBtn.setBounds (px (738), py (9), px (50), py (25));

    triggerSettingsBtn.setBounds ({});
    fireTriggerBtn.setBounds ({});
    gridBtn.setBounds ({});
    secondsBtn.setBounds ({});
}
