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
    g.setColour (ui::kMainBg);
    g.fillRect (b);
    g.setColour (ui::kBorder);
    g.drawRect (b.reduced (0.5f), 1.0f);

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
    // SVG: magnifier icon - r=3.8 circle + short handle, ui-icon grey.
    const auto b = getLocalBounds().toFloat();
    const bool active = getToggleState();
    const float cx = b.getCentreX() - 1.5f;
    const float cy = b.getCentreY() - 1.5f;

    g.setColour (active ? wavelens::ui::kPurpleAccent
                        : (highlighted || down ? juce::Colour (0xffc9c4d6) : snap::icon));
    g.drawEllipse (cx - 3.8f, cy - 3.8f, 7.6f, 7.6f, 1.35f);
    g.drawLine (cx + 2.7f, cy + 2.7f, cx + 5.5f, cy + 5.5f, 1.35f);
}

void WaveformControlBar::WaveformIconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    // SVG "Output Waveform Icon": four vertical bars of increasing then
    // decreasing height, ui-icon grey, accent when active.
    const auto b = getLocalBounds().toFloat();
    const bool active = getToggleState();
    const juce::Colour stroke = active ? wavelens::ui::kPurpleAccent
                                       : (highlighted || down ? juce::Colour (0xffc9c4d6)
                                                              : snap::icon);
    const float cy = b.getCentreY();
    g.setColour (stroke);
    const float bars[] = { 2.0f, 4.0f, 5.0f, 3.0f };
    const float spacing = b.getWidth() / 5.0f;
    for (int i = 0; i < 4; ++i)
    {
        const float x = b.getX() + spacing * (float) i + spacing * 0.5f;
        g.drawLine (x, cy - bars[i], x, cy + bars[i], 1.35f);
    }
}

//==============================================================================
WaveformControlBar::WaveformControlBar (WaveLensProcessor& processor)
    : proc (processor)
{
    // The bar is taller than its painted strip (the collapse tab rises 12
    // SVG units above it); clicks outside the tab must reach the scope.
    setInterceptsMouseClicks (false, true);

    limiterBtn.setButtonText ("");
    limiterBtn.getProperties().set ("isPowerIcon", true);
    limiterBtn.setClickingTogglesState (true);
    limiterBtn.setToggleState (true, juce::dontSendNotification);
    limiterBtn.setTooltip ("Limiter protection display toggle");
    limiterBtn.getProperties().set ("accentColour", (juce::int64) ui::kPurpleAccent.getARGB());
    addAndMakeVisible (limiterBtn);

    preLeadSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    preLeadSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    preLeadSlider.setColour (juce::Slider::trackColourId, ui::kPurpleAccent);
    preLeadSlider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " ms"; };
    preLeadSlider.setTooltip ("PRE lead offset before dry transient (0 ms to 100 ms)");
    addAndMakeVisible (preLeadSlider);
    preLeadAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids::preRollMs, preLeadSlider);
    preLeadSlider.updateText();

    // SVG "Collapse Center Tab": trapezoid with COLLAPSE text + chevron.
    collapseTabBtn.setClickingTogglesState (true);
    collapseTabBtn.setTooltip ("Collapse or restore the lower sample editors");
    collapseTabBtn.getProperties().set ("isTrapezoid", true);
    collapseTabBtn.getProperties().set ("accentColour", (juce::int64) juce::Colour (0xff7d72b3).getARGB());
    collapseTabBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffa8a3b8));
    collapseTabBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xffa8a3b8));
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
    addAndMakeVisible (triggerSettingsBtn);

    fireTriggerBtn.setTooltip ("Fire manual trigger audition");
    fireTriggerBtn.getProperties().set ("accentColour", (juce::int64) ui::kCyanAccent.getARGB());
    fireTriggerBtn.onClick = [this] { proc.requestTrigger(); };
    addAndMakeVisible (fireTriggerBtn);

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
    setupViewBtn (secondsBtn, "SECONDS", snap::tr, "Show milliseconds ruler");
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
    // SVG "Zoom Value" is a bright ui-val text with no box styling
    zoomLevelBtn.getProperties().set ("isTransparentText", true);
    zoomLevelBtn.setColour (juce::TextButton::textColourOffId, ui::kMainText);
    zoomLevelBtn.setButtonText ("1/1");
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

    // SVG "Output Waveform Icon": small vertical-bar waveform, ink strokes
    outputBtn.setTooltip ("Toggle live summed Master Output monitor");
    outputBtn.getProperties().set ("accentColour", (juce::int64) ui::kMasterWave.getARGB());
    outputBtn.onClick = [this]
    {
        if (onOutputToggled)
            onOutputToggled (outputBtn.getToggleState());
    };
    addAndMakeVisible (outputBtn);

    // The SVG toolbar shows only Limiter / Collapse / Detail / Zoom / Output;
    // the rest stay functional but invisible.
    triggerSettingsBtn.setVisible (false);
    fireTriggerBtn.setVisible (false);
    // Restore compact secondary controls in the empty toolbar spaces.
    // Trigger settings and manual fire remain available in the header menu.
    gridBtn.setVisible (true);
    secondsBtn.setVisible (true);
    zoomOutBtn.setVisible (true);
    zoomInBtn.setVisible (true);
    preLeadSlider.setVisible (true);

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
    // SVG "Zoom Value" reads as a fraction like "1/2"
    zoomText = zoomFactor <= 1.05f
        ? juce::String ("1/1")
        : (zoomFactor >= 9.95f ? juce::String (juce::roundToInt (zoomFactor)) + "x"
                               : juce::String (zoomFactor, 1) + "x");
    // The value is shown by the button itself (ui-val #dcd9e6)
    zoomLevelBtn.setButtonText (zoomText);
    zoomOutBtn.setEnabled (zoomFactor > 1.05f);
    zoomInBtn.setEnabled (zoomFactor < 31.5f);
    repaint();
}

void WaveformControlBar::timerCallback()
{
    repaint();
}

void WaveformControlBar::paint (juce::Graphics& g)
{
    // SVG [03]: #18161e strip (28 of the bar's 40 units) with a 2px
    // #0d0b10 bottom separator; the top 12 units stay transparent so the
    // COLLAPSE tab can rise above the toolbar into the visualizer.
    const int stripTop = getHeight() - stripHeight();
    auto strip = getLocalBounds().withTop (stripTop).toFloat();
    g.setColour (snap::panelHi);
    g.fillRect (strip);
    g.setColour (snap::tileTrack);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, strip.getRight());

    auto stripText = [this, &g, stripTop] (const juce::String& t, int x, int w, juce::Justification j, juce::Colour c)
    {
        g.setColour (c);
        g.drawText (t, x, stripTop, w, stripHeight(), j);
    };

    g.setFont (juce::Font (10.5f));
    stripText ("Limiter", limiterBtn.getRight() + 6, 50, juce::Justification::centredLeft,
               juce::Colour (0xff8b8699));
    stripText (formatPeakDb (proc.peakNow.load (relaxed)),
               limiterBtn.getRight() + 58, 64, juce::Justification::centredLeft,
               juce::Colour (0xff565261));

    // Static label positions are specified in the 800px SVG coordinate
    // system, just like the buttons below. Scale them with the editor width.
    const float sx = (float) getWidth() / 800.0f;
    auto px = [sx] (float value) { return juce::roundToInt (value * sx); };

    // PRE lead and ruler mode controls occupy the otherwise-unused middle-left
    // strip, preserving the clean SVG centre tab and right-side controls.
    stripText ("PRE", px (158), px (24), juce::Justification::centredLeft,
               juce::Colour (0xff928da1));
    stripText (preLeadSlider.getTextFromValue (preLeadSlider.getValue()),
               px (284), px (48), juce::Justification::centredLeft, juce::Colour (0xffdcd9e6));

    // SVG right side: compact GRID/SEC toggles, Detail, Zoom +/- and Output.
    stripText ("Detail", px (567), px (42), juce::Justification::centredRight,
               juce::Colour (0xff928da1));
    stripText ("Zoom", px (632), px (30), juce::Justification::centredRight,
               juce::Colour (0xff928da1));
    stripText ("Output", px (724), px (34), juce::Justification::centredRight,
               juce::Colour (0xff928da1));
}

void WaveformControlBar::resized()
{
    // SVG [03] toolbar geometry on an 800x28 strip (the bar component is 40
    // units tall: 12 overhang above the strip for the collapse tab):
    //   limiter icon @24, "Limiter" @37, "0.0dB" @78
    //   collapse trapezoid 124x40 centred @400 (spans the whole bar)
    //   "Detail" @580 + magnifier @613, "Zoom" @646 + value @680,
    //   "Output" @717 + waveform icon @762
    const int stripTop = getHeight() - stripHeight();
    const float sx = (float) getWidth() / 800.0f;
    const float sy = (float) stripHeight() / 28.0f;
    auto at = [sx, sy, stripTop] (float x, float y, float w, float h)
    {
        return juce::Rectangle<int> (juce::roundToInt (x * sx),
                                     juce::roundToInt (stripTop + (y - 342.0f) * sy),
                                     juce::roundToInt (w * sx), juce::roundToInt (h * sy));
    };

    limiterBtn.setBounds (at (18, 349, 16, 16));

    // Centre trapezoid COLLAPSE tab (SVG: 338..462 wide 124, y 330..370)
    // fills the whole bar height so its top half stays unclipped.
    collapseTabBtn.setBounds (juce::roundToInt (338.0f * sx), 0,
                              juce::roundToInt (124.0f * sx), getHeight());

    preLeadSlider.setBounds (at (184, 349, 96, 16));
    gridBtn.setButtonText ("GRID");
    gridBtn.setBounds (at (478, 349, 34, 16));
    secondsBtn.setButtonText ("SEC");
    secondsBtn.setBounds (at (516, 349, 44, 16));

    detailBtn.setBounds (at (613, 349, 16, 16));
    zoomOutBtn.setBounds (at (666, 349, 12, 16));
    zoomLevelBtn.setBounds (at (679, 348, 30, 18));
    zoomInBtn.setBounds (at (710, 349, 12, 16));
    outputBtn.setBounds (at (760, 349, 20, 16));
}

