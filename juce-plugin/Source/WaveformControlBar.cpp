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

    preLeadSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    preLeadSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 18);
    preLeadSlider.setColour (juce::Slider::trackColourId, ui::kPurpleAccent);
    preLeadSlider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " ms"; };
    preLeadSlider.setTooltip ("PRE lead offset before the dry transient (0 to 100 ms)");
    addAndMakeVisible (preLeadSlider);
    preLeadAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, ids::preRollMs, preLeadSlider);
    preLeadSlider.updateText();

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

    const float uiScale = (float) getWidth() / 720.0f;
    // Controls identify themselves through compact values and tooltips; avoid
    // separate labels that collide when the drawer is hosted at compact widths.
    juce::ignoreUnused (uiScale);

    // Trigger LED indicator on far left
    const float ledX = 16.0f * uiScale;
    const float ledY = b.getCentreY();
    if (ledFlashAlpha > 0.02f)
    {
        g.setColour (ui::kCyanAccent.withAlpha (0.35f * ledFlashAlpha));
        g.fillEllipse (ledX - 7.0f * uiScale, ledY - 7.0f * uiScale,
                       14.0f * uiScale, 14.0f * uiScale);
    }
    g.setColour (ui::kControlBg.interpolatedWith (ui::kCyanAccent, 0.20f + 0.80f * ledFlashAlpha));
    g.fillEllipse (ledX - 4.0f * uiScale, ledY - 4.0f * uiScale,
                   8.0f * uiScale, 8.0f * uiScale);
    g.setColour (ui::kBorder);
    g.drawEllipse (ledX - 4.0f * uiScale, ledY - 4.0f * uiScale,
                   8.0f * uiScale, 8.0f * uiScale, 1.0f * uiScale);
}

void TriggerPanelComponent::resized()
{
    // Dense settings drawer uses a compact measured row. All widths are
    // proportional to the 720 x 38 design row, so labels never overlap controls.
    const float sx = (float) getWidth() / 720.0f;
    const float sy = (float) getHeight() / 38.0f;
    auto px = [sx] (float v) { return juce::roundToInt (v * sx); };
    auto py = [sy] (float v) { return juce::roundToInt (v * sy); };

    auto r = getLocalBounds().reduced (px (12.0f), py (4.0f));
    const int h = juce::jmax (1, r.getHeight());
    const int gap = px (3.0f);

    // LED, threshold, trigger mode, algorithm, view/span, PRE lead, filter, listen.
    r.removeFromLeft (px (12.0f));
    r.removeFromLeft (gap);
    thresholdSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false,
                                     px (38.0f), py (20.0f));
    thresholdSlider.setBounds (r.removeFromLeft (px (100.0f)));
    r.removeFromLeft (gap);
    modeBox.setBounds (r.removeFromLeft (px (62.0f)));
    r.removeFromLeft (gap);

    const int segmentW = px (42.0f);
    drumsBtn.setBounds (r.removeFromLeft (segmentW));
    r.removeFromLeft (px (2.0f));
    generalBtn.setBounds (r.removeFromLeft (segmentW));
    r.removeFromLeft (px (2.0f));
    complexBtn.setBounds (r.removeFromLeft (segmentW));
    r.removeFromLeft (gap);

    viewBox.setBounds (r.removeFromLeft (px (64.0f)));
    r.removeFromLeft (gap);
    spanBox.setBounds (r.removeFromLeft (px (56.0f)));
    r.removeFromLeft (gap);

    preLeadSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false,
                                   px (34.0f), py (20.0f));
    preLeadSlider.setBounds (r.removeFromLeft (px (88.0f)));
    r.removeFromLeft (gap);

    inputFilter.setBounds (r.removeFromLeft (juce::jmin (r.getWidth(), px (94.0f))));
    r.removeFromLeft (gap);
    listenBtn.setBounds (r.removeFromRight (juce::jmin (r.getWidth(), px (46.0f))));

    listenBtn.setTooltip ("Listen to the selected input filter range");
    thresholdSlider.setTooltip ("Trigger threshold in dB");
    modeBox.setTooltip ("Audio, MIDI or combined trigger mode");
    viewBox.setTooltip ("Trigger detector view");
    spanBox.setTooltip ("Timeline bar span");
}

//==============================================================================
void WaveformControlBar::CollapseButton::paintButton (juce::Graphics& g,
                                                       bool highlighted,
                                                       bool down)
{
    juce::TextButton::paintButton (g, highlighted, down);

    const float uiScale = (float) getHeight() / 42.0f;
    const float cx = (float) getWidth() * 0.5f;
    const float cy = (float) getHeight() - 8.0f * uiScale;
    g.setColour (juce::Colours::white.withAlpha (0.95f));
    g.drawLine (cx - 4.0f * uiScale, cy - 2.5f * uiScale,
                cx, cy + 1.5f * uiScale, 1.3f * uiScale);
    g.drawLine (cx, cy + 1.5f * uiScale,
                cx + 4.0f * uiScale, cy - 2.5f * uiScale, 1.3f * uiScale);
}

//==============================================================================
void WaveformControlBar::LimiterButton::paintButton (juce::Graphics& g,
                                                     bool highlighted,
                                                     bool down)
{
    const auto b = getLocalBounds().toFloat();
    const float uiScale = b.getHeight() / 22.0f;
    const bool active = getToggleState();
    auto stroke = active ? ui::kMainText : ui::kSecondaryText;
    if (down) stroke = stroke.brighter (0.25f);
    else if (highlighted) stroke = stroke.brighter (0.12f);

    const float cx = b.getCentreX();
    const float cy = b.getCentreY();
    const float radius = juce::jmin (6.0f * uiScale, b.getWidth() * 0.38f);
    juce::Path arc;
    arc.addCentredArc (cx, cy + 1.0f * uiScale, radius, radius, 0.0f,
                       0.48f, juce::MathConstants<float>::twoPi - 0.48f, true);
    g.setColour (stroke);
    g.strokePath (arc, juce::PathStrokeType (1.4f * uiScale));
    g.drawLine (cx, cy - 7.0f * uiScale, cx, cy - 0.5f * uiScale, 1.4f * uiScale);
}

//==============================================================================
void WaveformControlBar::MagnifierButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const float uiScale = (float) getHeight() / 25.0f;
    const bool active = getToggleState();

    juce::Colour bg = active ? ui::kSelectedAccent.withAlpha (0.24f) : ui::kControlBg;
    if (down)
        bg = bg.brighter (0.14f);
    else if (highlighted)
        bg = active ? bg.brighter (0.08f) : ui::kHoverBg;

    g.setColour (bg);
    g.fillRoundedRectangle (b, 3.5f * uiScale);
    g.setColour (active ? ui::kPurpleAccent : ui::kBorder);
    g.drawRoundedRectangle (b, 3.5f * uiScale, 1.0f * uiScale);

    // SVG order is label first, magnifier second.
    const float iconCx = b.getRight() - 10.0f * uiScale;
    const float iconCy = b.getCentreY() - 0.5f * uiScale;
    g.setColour (active ? ui::kMainText : ui::kSecondaryText);
    g.drawEllipse (iconCx - 4.0f * uiScale, iconCy - 4.0f * uiScale,
                   7.5f * uiScale, 7.5f * uiScale, 1.3f * uiScale);
    g.drawLine (iconCx + 2.2f * uiScale, iconCy + 2.2f * uiScale,
                iconCx + 5.6f * uiScale, iconCy + 5.6f * uiScale, 1.4f * uiScale);

    g.setFont (juce::Font (10.0f * uiScale, juce::Font::bold));
    g.drawFittedText ("Detail", b.withTrimmedRight (20.0f * uiScale).toNearestInt(),
                      juce::Justification::centred, 1);
}

//==============================================================================
WaveformControlBar::WaveformControlBar (WaveLensProcessor& processor)
    : proc (processor)
{
    limiterBtn.setToggleState (true, juce::dontSendNotification);
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
    // Keep the 30 px toolbar bar below the scope. Only the raised COLLAPSE
    // tab occupies the 12 px overlap into the visualizer.
    b.removeFromTop ((float) juce::roundToInt ((float) getHeight() * 12.0f / 42.0f));
    g.setColour (ui::kPanelBg);
    g.fillRect (b);
    g.setColour (ui::kBorder);
    g.drawRect (b.reduced (0.5f), 1.0f);

    const float sy = (float) getHeight() / 42.0f;
    g.setFont (juce::Font (10.0f * sy, juce::Font::bold));
    g.setColour (limiterBtn.getToggleState() ? ui::kMainText : ui::kSecondaryText);
    g.drawText ("Limiter", 37, limiterBtn.getY(), 39, limiterBtn.getHeight(),
                juce::Justification::centredLeft);
    g.setFont (juce::Font (10.0f * sy, juce::Font::bold));
    g.drawText (formatPeakDb (proc.peakNow.load (relaxed)),
                78, limiterBtn.getY(), 52, limiterBtn.getHeight(),
                juce::Justification::centredLeft);

    // Small output waveform glyph to the right of the Output label.
    const float sx = (float) getWidth() / 800.0f;
    const float outputCentreY = (float) outputBtn.getBounds().getCentreY();
    const float outputX = 762.0f * sx;
    g.setColour (outputBtn.getToggleState() ? ui::kMasterWave : ui::kSecondaryText);
    g.drawLine (outputX, outputCentreY, outputX + 16.0f * sx, outputCentreY, 0.8f * sy);
    g.drawLine (outputX + 3.0f * sx, outputCentreY - 2.0f * sy,
                outputX + 3.0f * sx, outputCentreY + 2.0f * sy, 1.2f * sy);
    g.drawLine (outputX + 6.0f * sx, outputCentreY - 4.0f * sy,
                outputX + 6.0f * sx, outputCentreY + 4.0f * sy, 1.2f * sy);
    g.drawLine (outputX + 9.0f * sx, outputCentreY - 5.0f * sy,
                outputX + 9.0f * sx, outputCentreY + 5.0f * sy, 1.2f * sy);
    g.drawLine (outputX + 12.0f * sx, outputCentreY - 3.0f * sy,
                outputX + 12.0f * sx, outputCentreY + 3.0f * sy, 1.2f * sy);
}

void WaveformControlBar::resized()
{
    const float sx = (float) getWidth() / 800.0f;
    const float sy = (float) getHeight() / 42.0f;
    auto px = [sx] (float v) { return juce::roundToInt (v * sx); };
    auto py = [sy] (float v) { return juce::roundToInt (v * sy); };

    limiterBtn.setBounds (px (18), py (14), px (16), py (22));
    preLeadSlider.setBounds ({});

    collapseTabBtn.setBounds (px (338), 0, px (124), getHeight());

    detailBtn.setBounds (px (568), py (12), px (62), py (25));
    zoomOutBtn.setButtonText ("Zoom");
    zoomOutBtn.setBounds (px (646), py (12), px (30), py (25));
    zoomLevelBtn.setBounds (px (678), py (12), px (30), py (25));
    zoomInBtn.setBounds (px (710), py (12), px (12), py (25));
    outputBtn.setButtonText ("Output");
    outputBtn.setBounds (px (724), py (12), px (34), py (25));

    triggerSettingsBtn.setBounds ({});
    fireTriggerBtn.setBounds ({});
    gridBtn.setBounds ({});
    secondsBtn.setBounds ({});
}
