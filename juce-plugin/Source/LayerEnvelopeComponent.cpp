#include "LayerEnvelopeComponent.h"

#include <cmath>

namespace ui = wavelens::ui;

namespace
{
    constexpr float kHandleRadius = 4.6f;
    constexpr float kTensionRadius = 4.6f;
    constexpr int kCurveSteps = 96;
    constexpr int kHeaderHeight = 20;
    constexpr int kBottomReadoutHeight = 20;
    // SVG editor region: 226 px waveform + 8 px gutter + 134 px controls.
    constexpr float kWaveformWidthRatio = 226.0f / 368.0f;
    constexpr float kWaveformGapRatio = 8.0f / 368.0f;
    constexpr float kRulerHeight = 0.0f;
    constexpr float kOverviewHeight = 0.0f;
    constexpr float kMaxWaveformZoom = 32.0f;

    juce::String formatEnvelopeTime (float value)
    {
        if (value >= 1000.0f)
            return juce::String (value / 1000.0f, 2) + "s";
        return juce::String (value, value < 10.0f ? 1 : 0) + "ms";
    }

    juce::String formatLayerDb (double linearGain)
    {
        if (linearGain <= 0.001)
            return "-inf";
        const double db = 20.0 * std::log10 (linearGain);
        return juce::String (db > 0.05 ? "+" : "") + juce::String (db, 1) + " dB";
    }
}

//==============================================================================
// Combined front/back trim control

DualTrimRangeComponent::DualTrimRangeComponent (WaveLensProcessor& p,
                                                int layerIndex,
                                                juce::Colour accentColour)
    : processor (p),
      layer (juce::jlimit (0, WaveLensProcessor::kMaxSignals - 1, layerIndex)),
      accent (accentColour)
{
    startValue = processor.apvts.getRawParameterValue (ids::layerTrimStart (layer));
    endValue = processor.apvts.getRawParameterValue (ids::layerTrimEnd (layer));
    startParameter = dynamic_cast<juce::RangedAudioParameter*> (
        processor.apvts.getParameter (ids::layerTrimStart (layer)));
    endParameter = dynamic_cast<juce::RangedAudioParameter*> (
        processor.apvts.getParameter (ids::layerTrimEnd (layer)));

    auto prepareEditor = [this] (juce::TextEditor& editor, const juce::String& tip, bool isStart)
    {
        editor.setMultiLine (false);
        editor.setReturnKeyStartsNewLine (false);
        editor.setInputRestrictions (8, "0123456789.%");
        editor.setJustification (juce::Justification::centred);
        editor.setFont (juce::Font (9.0f));
        editor.setTooltip (tip);
        editor.setColour (juce::TextEditor::backgroundColourId, ui::kControlBg);
        editor.setColour (juce::TextEditor::textColourId, ui::kMainText);
        editor.setColour (juce::TextEditor::outlineColourId, ui::kBorder);
        editor.setColour (juce::TextEditor::focusedOutlineColourId, accent);
        editor.setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.25f));
        auto* editorPtr = &editor;
        editor.onReturnKey = [editorPtr] { editorPtr->giveAwayKeyboardFocus(); };
        editor.onFocusLost = [this, isStart] { commitEditor (isStart); };
        addAndMakeVisible (editor);
    };

    prepareEditor (startEditor, "Front trim start position (0% to 99%)", true);
    prepareEditor (endEditor, "Back trim end position (1% to 100%)", false);

    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    refreshEditors();
    startTimerHz (15);
}

DualTrimRangeComponent::~DualTrimRangeComponent()
{
    stopTimer();
    endThumbGesture();
}

void DualTrimRangeComponent::timerCallback()
{
    refreshEditors();
    repaint();
}

void DualTrimRangeComponent::refreshEditors()
{
    if (startValue == nullptr || endValue == nullptr)
        return;

    const float start = startValue->load (std::memory_order_relaxed);
    const float end = endValue->load (std::memory_order_relaxed);

    if (std::abs (start - lastStart) > 0.0001f)
    {
        lastStart = start;
        if (! startEditor.hasKeyboardFocus (true))
            startEditor.setText (juce::String (start * 100.0f, 1) + "%", juce::dontSendNotification);
    }
    if (std::abs (end - lastEnd) > 0.0001f)
    {
        lastEnd = end;
        if (! endEditor.hasKeyboardFocus (true))
            endEditor.setText (juce::String (end * 100.0f, 1) + "%", juce::dontSendNotification);
    }
}

void DualTrimRangeComponent::commitEditor (bool isStart)
{
    auto& editor = isStart ? startEditor : endEditor;
    const auto numeric = editor.getText().retainCharacters ("0123456789.-");
    if (numeric.isEmpty())
    {
        refreshEditors();
        return;
    }

    const float percentage = (float) numeric.getDoubleValue();
    setTrimValue (isStart, percentage * 0.01f, true);
    refreshEditors();
    repaint();
}

void DualTrimRangeComponent::setTrimValue (bool isStart, float value, bool sendGesture)
{
    if (startValue == nullptr || endValue == nullptr)
        return;

    constexpr float minimumGap = 0.005f;
    const float currentStart = startValue->load (std::memory_order_relaxed);
    const float currentEnd = endValue->load (std::memory_order_relaxed);
    value = isStart
        ? juce::jlimit (0.0f, juce::jmax (0.0f, currentEnd - minimumGap), value)
        : juce::jlimit (juce::jmin (1.0f, currentStart + minimumGap), 1.0f, value);

    auto* parameter = isStart ? startParameter : endParameter;
    if (parameter == nullptr)
        return;

    if (sendGesture)
        parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    if (sendGesture)
        parameter->endChangeGesture();

    refreshEditors();
    repaint();
}

void DualTrimRangeComponent::beginThumbGesture()
{
    if (gestureActive || activeThumb == ActiveThumb::None)
        return;

    auto* parameter = activeThumb == ActiveThumb::Start ? startParameter : endParameter;
    if (parameter != nullptr)
    {
        parameter->beginChangeGesture();
        gestureActive = true;
    }
}

void DualTrimRangeComponent::endThumbGesture()
{
    if (! gestureActive)
    {
        activeThumb = ActiveThumb::None;
        return;
    }

    auto* parameter = activeThumb == ActiveThumb::Start ? startParameter : endParameter;
    if (parameter != nullptr)
        parameter->endChangeGesture();

    gestureActive = false;
    activeThumb = ActiveThumb::None;
    refreshEditors();
}

juce::Rectangle<float> DualTrimRangeComponent::getTrackBounds() const noexcept
{
    return trackBounds;
}

void DualTrimRangeComponent::paint (juce::Graphics& g)
{
    const float uiScale = juce::jmin ((float) getWidth() / 134.0f,
                                      (float) getHeight() / 32.0f);
    auto bounds = getLocalBounds().toFloat().reduced (0.5f * uiScale);
    g.setColour (ui::kControlBg);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (ui::kBorder);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    g.setFont (juce::Font (8.5f * uiScale, juce::Font::bold));
    g.setColour (ui::kSecondaryText);
    const float halfW = bounds.getWidth() * 0.5f;
    g.drawText ("Str Trim", bounds.getX() + 4.0f * uiScale, bounds.getY() + 1.0f * uiScale,
                halfW - 5.0f * uiScale, 9.0f * uiScale, juce::Justification::centredLeft);
    g.drawText ("Bk Trim", bounds.getCentreX() + 1.0f * uiScale, bounds.getY() + 1.0f * uiScale,
                halfW - 5.0f * uiScale, 9.0f * uiScale, juce::Justification::centredRight);

    if (startValue == nullptr || endValue == nullptr || trackBounds.isEmpty())
        return;

    const float start = juce::jlimit (0.0f, 1.0f, startValue->load (std::memory_order_relaxed));
    const float end = juce::jlimit (start + 0.005f, 1.0f, endValue->load (std::memory_order_relaxed));
    const float startX = trackBounds.getX() + start * trackBounds.getWidth();
    const float endX = trackBounds.getX() + end * trackBounds.getWidth();
    const float cy = trackBounds.getCentreY();

    g.setColour (ui::kMainBg);
    g.fillRoundedRectangle (trackBounds, 2.5f);
    g.setColour (accent.withAlpha (0.24f));
    g.fillRoundedRectangle (juce::Rectangle<float> (startX, trackBounds.getY(),
                                                     juce::jmax (0.0f, endX - startX),
                                                     trackBounds.getHeight()), 2.5f);
    g.setColour (accent);
    g.drawLine (startX, cy, endX, cy, 2.0f);

    const float radius = juce::jlimit (3.0f * uiScale, 4.5f * uiScale,
                                        trackBounds.getHeight() * 0.60f);
    for (const float x : { startX, endX })
    {
        g.setColour (ui::kPanelBg);
        g.fillEllipse (x - radius - 1.0f, cy - radius - 1.0f,
                       (radius + 1.0f) * 2.0f, (radius + 1.0f) * 2.0f);
        g.setColour (accent);
        g.fillEllipse (x - radius, cy - radius, radius * 2.0f, radius * 2.0f);
    }
}

void DualTrimRangeComponent::resized()
{
    const float uiScale = juce::jmin ((float) getWidth() / 134.0f,
                                      (float) getHeight() / 32.0f);
    auto bounds = getLocalBounds().reduced (juce::roundToInt (4.0f * uiScale),
                                            juce::roundToInt (2.0f * uiScale));
    const int labelHeight = juce::jmax (1, juce::roundToInt (8.0f * uiScale));
    const int inputHeight = juce::jmax (1, juce::roundToInt (11.0f * uiScale));
    const int inputGap = juce::jmax (1, juce::roundToInt (3.0f * uiScale));
    const int halfWidth = (bounds.getWidth() - inputGap) / 2;

    startEditor.setFont (juce::Font (9.0f * uiScale));
    endEditor.setFont (juce::Font (9.0f * uiScale));
    startEditor.setBounds (bounds.getX(), bounds.getY() + labelHeight,
                           halfWidth, inputHeight);
    endEditor.setBounds (bounds.getX() + halfWidth + inputGap,
                         bounds.getY() + labelHeight,
                         halfWidth, inputHeight);

    const int trackY = juce::jmin (getHeight() - juce::jmax (2, juce::roundToInt (5.0f * uiScale)),
                                   bounds.getY() + labelHeight + inputHeight + juce::roundToInt (2.0f * uiScale));
    trackBounds = juce::Rectangle<float> ((float) bounds.getX(), (float) trackY,
                                          (float) bounds.getWidth(),
                                          (float) juce::jmax (juce::roundToInt (4.0f * uiScale),
                                                              getHeight() - trackY - juce::roundToInt (2.0f * uiScale)));
}

void DualTrimRangeComponent::mouseDown (const juce::MouseEvent& event)
{
    if (! trackBounds.contains (event.position) || startValue == nullptr || endValue == nullptr)
        return;

    const float startX = trackBounds.getX() + startValue->load (std::memory_order_relaxed) * trackBounds.getWidth();
    const float endX = trackBounds.getX() + endValue->load (std::memory_order_relaxed) * trackBounds.getWidth();
    const float startDistance = std::abs (event.position.x - startX);
    const float endDistance = std::abs (event.position.x - endX);

    activeThumb = startDistance <= endDistance ? ActiveThumb::Start : ActiveThumb::End;
    beginThumbGesture();

    const float norm = juce::jlimit (0.0f, 1.0f,
        (event.position.x - trackBounds.getX()) / juce::jmax (1.0f, trackBounds.getWidth()));
    setTrimValue (activeThumb == ActiveThumb::Start, norm, false);
}

void DualTrimRangeComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (activeThumb == ActiveThumb::None)
        return;

    const float norm = juce::jlimit (0.0f, 1.0f,
        (event.position.x - trackBounds.getX()) / juce::jmax (1.0f, trackBounds.getWidth()));
    setTrimValue (activeThumb == ActiveThumb::Start, norm, false);
}

void DualTrimRangeComponent::mouseUp (const juce::MouseEvent&)
{
    endThumbGesture();
}

//==============================================================================

//==============================================================================
// SVG phase-invert marker. Disabled until polarity inversion is implemented.

PhaseInvertPlaceholder::PhaseInvertPlaceholder()
    : juce::Button ("PhaseInvertUnavailable")
{
    setTooltip ("Phase inversion is not supported by the current audio engine");
    setEnabled (false);
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void PhaseInvertPlaceholder::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    juce::ignoreUnused (highlighted, down);
    const auto b = getLocalBounds().toFloat();
    const float uiScale = (float) getHeight() / 18.0f;
    const float cx = b.getCentreX();
    const float cy = b.getCentreY();
    const float r = juce::jmin (3.5f * uiScale, b.getHeight() * 0.28f);
    g.setColour (ui::kDisabledText);
    g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f * uiScale);
    g.drawLine (cx - r, cy + r, cx + r, cy - r, 1.0f * uiScale);
}

//==============================================================================

LayerEnvelopeComponent::LayerEnvelopeComponent (WaveLensProcessor& p,
                                                int layerIndex,
                                                juce::Colour traceColour)
    : processor (p),
      layer (juce::jlimit (0, WaveLensProcessor::kMaxSignals - 1, layerIndex)),
      colour (traceColour)
{
    auto& state = processor.apvts;
    pAttack = state.getRawParameterValue (ids::layerAttack (layer));
    pHold = state.getRawParameterValue (ids::layerHold (layer));
    pDecay = state.getRawParameterValue (ids::layerDecay (layer));
    pSustain = state.getRawParameterValue (ids::layerSustain (layer));
    pRelease = state.getRawParameterValue (ids::layerRelease (layer));
    pAttackCurve = state.getRawParameterValue (ids::layerAttackCurve (layer));
    pDecayCurve = state.getRawParameterValue (ids::layerDecayCurve (layer));
    pReleaseCurve = state.getRawParameterValue (ids::layerReleaseCurve (layer));
    pAttackTension = state.getRawParameterValue (ids::layerAttackTension (layer));
    pDecayTension = state.getRawParameterValue (ids::layerDecayTension (layer));
    pReleaseTension = state.getRawParameterValue (ids::layerReleaseTension (layer));
    pEnvEnabled = state.getRawParameterValue (ids::layerEnvEnabled (layer));
    pReverse = state.getRawParameterValue (ids::layerReverse (layer));
    pPitch = state.getRawParameterValue (ids::layerPitch (layer));
    pTrimStart = state.getRawParameterValue (ids::layerTrimStart (layer));
    pTrimEnd = state.getRawParameterValue (ids::layerTrimEnd (layer));
    pFadeIn = state.getRawParameterValue (ids::layerFadeIn (layer));
    pFadeOut = state.getRawParameterValue (ids::layerFadeOut (layer));

    envEnableBtn.setButtonText ("ENV");
    envEnableBtn.setClickingTogglesState (true);
    envEnableBtn.setTooltip ("Enable or bypass FL amplitude envelope");
    envEnableBtn.getProperties().set ("accentColour", (juce::int64) colour.getARGB());
    addAndMakeVisible (envEnableBtn);
    envEnableBtn.setVisible (false);
    envEnableAttachment = std::make_unique<ButtonAtt> (state, ids::layerEnvEnabled (layer), envEnableBtn);

    reverseBtn.setButtonText ("REV");
    reverseBtn.setClickingTogglesState (true);
    reverseBtn.setTooltip ("Reverse sample playback");
    reverseBtn.getProperties().set ("accentColour", (juce::int64) colour.getARGB());
    addAndMakeVisible (phaseInvertBtn);
    addAndMakeVisible (reverseBtn);
    reverseAttachment = std::make_unique<ButtonAtt> (state, ids::layerReverse (layer), reverseBtn);

    const juce::StringArray flCurveNames { "Hold", "Smooth", "Single curve", "Half sine", "Stairs", "Smooth stairs", "Pulse", "Wave" };
    for (auto* box : { &attackCurveBox, &decayCurveBox, &releaseCurveBox })
    {
        box->addItemList (flCurveNames, 1);
        box->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*box);
    }
    attackCurveBox.setTooltip ("Attack segment FL curve mode");
    decayCurveBox.setTooltip ("Decay segment FL curve mode");
    releaseCurveBox.setTooltip ("Release segment FL curve mode");

    attackCurveAttachment = std::make_unique<ComboAtt> (state, ids::layerAttackCurve (layer), attackCurveBox);
    decayCurveAttachment = std::make_unique<ComboAtt> (state, ids::layerDecayCurve (layer), decayCurveBox);
    releaseCurveAttachment = std::make_unique<ComboAtt> (state, ids::layerReleaseCurve (layer), releaseCurveBox);

    // The SVG design uses a vertical Volume fader in its own card.
    gainSlider.setSliderStyle (juce::Slider::LinearVertical);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setDoubleClickReturnValue (true, 1.0);
    gainSlider.setColour (juce::Slider::trackColourId, colour);
    gainSlider.setTooltip ("Layer volume (-inf to +6.0 dB). Double-click to reset to 0.0 dB.");
    addAndMakeVisible (gainSlider);
    gainAttachment = std::make_unique<SliderAtt> (state, ids::layerGain (layer), gainSlider);
    // NOTE: set after the attachment - SliderParameterAttachment overwrites
    // textFromValueFunction with the parameter's own getText().
    gainSlider.textFromValueFunction = [] (double v) { return formatLayerDb (v); };

    auto prepareTile = [&] (juce::Slider& slider,
                            const char* label,
                            const juce::String& tooltip,
                            juce::Colour fill,
                            bool bipolar,
                            double defaultVal)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setSliderSnapsToMousePosition (false);
        slider.setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        slider.setDoubleClickReturnValue (true, defaultVal);
        slider.setColour (juce::Slider::trackColourId, fill);
        slider.setTooltip (tooltip + " (Double-click to reset)");
        slider.getProperties().set ("isTile", true);
        slider.getProperties().set ("tileLabel", label);
        slider.getProperties().set ("bipolar", bipolar);
        addAndMakeVisible (slider);
    };

    const juce::Colour secondaryFill = colour.interpolatedWith (ui::kMainText, 0.16f);
    prepareTile (delaySlider,     "SHIFT",    "Layer timing shift (-100 ms to +100 ms)",              colour,        true,  0.0);
    prepareTile (pitchSlider,     "PITCH",    "Layer pitch (-24 to +24 semitones)",                   colour,        true,  0.0);
    pitchSlider.getProperties().set ("compactPitchReadout", true);
    prepareTile (panSlider,       "PAN",      "Stereo pan (Left / Centre / Right)",                   colour,        true,  0.0);
    prepareTile (widthSlider,     "WIDTH",    "Stereo width (0% mono, 100% original, 200% wide)",    colour,        false, 1.0);
    prepareTile (pogoSlider,      "POGO",     "FL Sampler style pitch-bend envelope (-100% to +100%)", secondaryFill, true,  0.0);
    prepareTile (fadeInSlider,    "FADE IN",  "Sample fade-in duration (0 ms to 2000 ms)",            colour,        false, 0.0);
    prepareTile (fadeOutSlider,   "FADE OUT", "Sample fade-out duration (0 ms to 5000 ms)",           colour,        false, 0.0);
    trimRange = std::make_unique<DualTrimRangeComponent> (processor, layer, colour);
    addAndMakeVisible (*trimRange);
    prepareTile (hpSlider,        "HP",       "Layer high-pass filter (20 Hz to 1 kHz)",              colour,        false, 20.0);
    prepareTile (lpSlider,        "LP",       "Layer low-pass filter (1 kHz to 20 kHz)",              colour,        false, 20000.0);

    delayAttachment     = std::make_unique<SliderAtt> (state, ids::layerDelay (layer),     delaySlider);
    pitchAttachment     = std::make_unique<SliderAtt> (state, ids::layerPitch (layer),     pitchSlider);
    panAttachment       = std::make_unique<SliderAtt> (state, ids::layerPan (layer),       panSlider);
    widthAttachment     = std::make_unique<SliderAtt> (state, ids::layerWidth (layer),     widthSlider);
    pogoAttachment      = std::make_unique<SliderAtt> (state, ids::layerPogo (layer),      pogoSlider);
    fadeInAttachment    = std::make_unique<SliderAtt> (state, ids::layerFadeIn (layer),    fadeInSlider);
    fadeOutAttachment   = std::make_unique<SliderAtt> (state, ids::layerFadeOut (layer),   fadeOutSlider);
    hpAttachment        = std::make_unique<SliderAtt> (state, ids::layerHighPass (layer),  hpSlider);
    lpAttachment        = std::make_unique<SliderAtt> (state, ids::layerLowPass (layer),   lpSlider);

    delaySlider.textFromValueFunction = [] (double v) { return juce::String (v > 0.0 ? "+" : "") + juce::String (v, 0) + "ms"; };
    pitchSlider.textFromValueFunction = [] (double v) { return juce::String (v > 0.0 ? "+" : "") + juce::String (v, 1) + "st"; };
    panSlider.textFromValueFunction   = [] (double value)
    {
        if (std::abs (value) < 0.005)
            return juce::String ("C");
        return juce::String (std::abs (value) * 100.0, 0) + (value < 0.0 ? "L" : "R");
    };
    widthSlider.textFromValueFunction  = [] (double value) { return juce::String (value * 100.0, 0) + "%"; };
    pogoSlider.textFromValueFunction    = [] (double v) { return juce::String (v > 0.0 ? "+" : "") + juce::String (v * 100.0, 0) + "%"; };
    fadeInSlider.textFromValueFunction  = [] (double v) { return formatEnvelopeTime ((float) v); };
    fadeOutSlider.textFromValueFunction = [] (double v) { return formatEnvelopeTime ((float) v); };
    hpSlider.textFromValueFunction = [] (double hz)
    {
        if (hz >= 999.5)
            return juce::String (hz / 1000.0, 1) + " kHz";
        return juce::String (juce::roundToInt (hz)) + " Hz";
    };
    lpSlider.textFromValueFunction = hpSlider.textFromValueFunction;

    for (auto* s : { &gainSlider, &delaySlider, &pitchSlider, &panSlider, &widthSlider, &pogoSlider,
                     &fadeInSlider, &fadeOutSlider, &hpSlider, &lpSlider })
        s->updateText();

    auto prepareZoomBtn = [this] (juce::TextButton& b, const juce::String& text, const juce::String& tip,
                                  juce::Colour textCol = ui::kMainText)
    {
        b.setButtonText (text);
        b.setTooltip (tip);
        b.setColour (juce::TextButton::textColourOffId, textCol);
        b.setColour (juce::TextButton::textColourOnId, textCol);
        addAndMakeVisible (b);
    };

    prepareZoomBtn (focusStartBtn, "|<",   "Center zoom on Start (Trim F)", colour.brighter (0.25f));
    prepareZoomBtn (focusEndBtn,   ">|",   "Center zoom on End (Trim B)",   colour.brighter (0.25f));
    prepareZoomBtn (zoomFitBtn,    "Fit",  "Zoom to fit Start-End trim range");
    prepareZoomBtn (zoomOutBtn,    "-",    "Zoom out waveform");
    prepareZoomBtn (zoomResetBtn,  "1.0x", "Reset waveform zoom to 1.0x", ui::kSecondaryText);
    prepareZoomBtn (zoomInBtn,     "+",    "Zoom in waveform");

    focusStartBtn.onClick = [this] { focusViewportAt (pTrimStart->load(), 4.0f); resized(); repaint(); };
    focusEndBtn.onClick   = [this] { focusViewportAt (pTrimEnd->load(), 4.0f);   resized(); repaint(); };
    zoomFitBtn.onClick    = [this] { zoomToFitTrim(); resized(); repaint(); };
    zoomOutBtn.onClick    = [this]
    {
        const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
        const float v0 = viewStartNorm;
        const float v1 = v0 + span;
        const float t0 = pTrimStart->load();
        const float t1 = pTrimEnd->load();
        const bool startIn = t0 >= v0 - 0.001f && t0 <= v1 + 0.001f;
        const bool endIn   = t1 >= v0 - 0.001f && t1 <= v1 + 0.001f;
        float anchor = v0 + span * 0.5f;
        if (startIn && ! endIn) anchor = t0;
        else if (endIn && ! startIn) anchor = t1;
        zoomAroundNormalized (anchor, 1.0f / 1.6f);
        resized();
        repaint();
    };
    zoomResetBtn.onClick = [this] { clampViewport (0.0f, 1.0f); resized(); repaint(); };
    zoomInBtn.onClick = [this]
    {
        const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
        const float v0 = viewStartNorm;
        const float v1 = v0 + span;
        const float t0 = pTrimStart->load();
        const float t1 = pTrimEnd->load();
        const bool startIn = t0 >= v0 - 0.001f && t0 <= v1 + 0.001f;
        const bool endIn   = t1 >= v0 - 0.001f && t1 <= v1 + 0.001f;
        float anchor = v0 + span * 0.5f;
        if (startIn && ! endIn) anchor = t0;
        else if (endIn && ! startIn) anchor = t1;
        zoomAroundNormalized (anchor, 1.6f);
        resized();
        repaint();
    };
    // These controls remain available in the parameter/state model but are not
    // part of this compact SVG layout. Reverse remains available from the sample menu;
    // curve modes remain available by right-clicking the editable envelope segments.
    reverseBtn.setVisible (false);
    attackCurveBox.setVisible (false);
    decayCurveBox.setVisible (false);
    releaseCurveBox.setVisible (false);
    focusStartBtn.setVisible (false);
    focusEndBtn.setVisible (false);
    zoomFitBtn.setVisible (false);
    zoomOutBtn.setVisible (false);
    zoomResetBtn.setVisible (false);
    zoomInBtn.setVisible (false);
    fadeInSlider.setVisible (false);
    fadeOutSlider.setVisible (false);
    hpSlider.setVisible (false);
    lpSlider.setVisible (false);

    updateZoomReadout();

    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setOpaque (false);
    startTimerHz (15);
}

LayerEnvelopeComponent::~LayerEnvelopeComponent()
{
    stopTimer();
    endGesture();
}

LayeredSampleEngine::LayerParameters LayerEnvelopeComponent::readLayerParameters() const noexcept
{
    LayeredSampleEngine::LayerParameters p;
    p.pitchSemitones = pPitch != nullptr ? pPitch->load() : 0.0f;
    p.attackMs = pAttack->load();
    p.holdMs = pHold->load();
    p.decayMs = pDecay->load();
    p.sustain = pSustain->load();
    p.releaseMs = pRelease->load();
    p.attackCurve = juce::roundToInt (pAttackCurve->load());
    p.decayCurve = juce::roundToInt (pDecayCurve->load());
    p.releaseCurve = juce::roundToInt (pReleaseCurve->load());
    p.attackTension = pAttackTension->load();
    p.decayTension = pDecayTension->load();
    p.releaseTension = pReleaseTension->load();
    p.envEnabled = true;
    p.reverse = pReverse->load() > 0.5f;
    p.trimStart = pTrimStart->load();
    p.trimEnd = pTrimEnd->load();
    p.fadeInMs = pFadeIn->load();
    p.fadeOutMs = pFadeOut->load();
    return p;
}

void LayerEnvelopeComponent::clampViewport (float nextViewStart, float nextZoom) noexcept
{
    const float safeZoom = juce::jlimit (1.0f, kMaxWaveformZoom, nextZoom);
    if (safeZoom <= 1.0001f)
    {
        zoomFactor = 1.0f;
        viewStartNorm = 0.0f;
    }
    else
    {
        zoomFactor = safeZoom;
        const float maxStart = juce::jmax (0.0f, 1.0f - (1.0f / zoomFactor));
        viewStartNorm = juce::jlimit (0.0f, maxStart, nextViewStart);
    }
    updateZoomReadout();
}

void LayerEnvelopeComponent::zoomAroundNormalized (float anchorNorm, float factor) noexcept
{
    const float curSpan = 1.0f / juce::jmax (1.0f, zoomFactor);
    const float safeAnchor = juce::jlimit (0.0f, 1.0f, anchorNorm);
    const float anchorRatio = juce::jlimit (0.0f, 1.0f, (safeAnchor - viewStartNorm) / curSpan);
    const float nextZoom = juce::jlimit (1.0f, kMaxWaveformZoom, zoomFactor * (factor > 0.0f ? factor : 1.0f));
    const float nextSpan = 1.0f / nextZoom;
    clampViewport (safeAnchor - anchorRatio * nextSpan, nextZoom);
}

void LayerEnvelopeComponent::focusViewportAt (float targetNorm, float defaultZoomIfUnzoomed) noexcept
{
    const float nextZoom = zoomFactor <= 1.05f
        ? juce::jlimit (1.0f, kMaxWaveformZoom, defaultZoomIfUnzoomed)
        : zoomFactor;
    const float span = 1.0f / nextZoom;
    clampViewport (juce::jlimit (0.0f, 1.0f, targetNorm) - span * 0.5f, nextZoom);
}

void LayerEnvelopeComponent::zoomToFitTrim() noexcept
{
    const float t0 = juce::jlimit (0.0f, 0.99f, pTrimStart->load());
    const float t1 = juce::jlimit (t0 + 0.005f, 1.0f, pTrimEnd->load());
    const float span = juce::jmax (0.01f, t1 - t0);
    if (span >= 0.96f)
    {
        clampViewport (0.0f, 1.0f);
        return;
    }
    const float pad = span * 0.08f;
    const float paddedStart = juce::jmax (0.0f, t0 - pad);
    const float paddedEnd = juce::jmin (1.0f, t1 + pad);
    const float targetSpan = juce::jmax (1.0f / kMaxWaveformZoom, paddedEnd - paddedStart);
    clampViewport (paddedStart, juce::jlimit (1.0f, kMaxWaveformZoom, 1.0f / targetSpan));
}

float LayerEnvelopeComponent::normToPlotX (float norm, const juce::Rectangle<float>& plot) const noexcept
{
    const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
    return plot.getX() + ((norm - viewStartNorm) / span) * plot.getWidth();
}

float LayerEnvelopeComponent::plotXToNorm (float x, const juce::Rectangle<float>& plot) const noexcept
{
    const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
    const float rel = (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth());
    return juce::jlimit (0.0f, 1.0f, viewStartNorm + rel * span);
}

void LayerEnvelopeComponent::updateZoomReadout()
{
    const bool zoomed = zoomFactor > 1.01f;
    juce::String txt = ! zoomed ? "1.0x"
        : (zoomFactor >= 10.0f ? juce::String (juce::roundToInt (zoomFactor)) + "x"
                               : juce::String (zoomFactor, 1) + "x");
    zoomResetBtn.setButtonText (txt);
    zoomResetBtn.setColour (juce::TextButton::textColourOffId,
                            zoomed ? colour.brighter (0.2f) : ui::kSecondaryText);
    zoomOutBtn.setEnabled (zoomFactor > 1.01f);
    zoomInBtn.setEnabled (zoomFactor < kMaxWaveformZoom - 0.01f);
}

LayerEnvelopeComponent::Geometry LayerEnvelopeComponent::getGeometry (float totalDurationMs) const
{
    Geometry result;
    const bool isZoomed = zoomFactor > 1.01f;
    auto bounds = getLocalBounds().toFloat();
    bounds.setRight (bounds.getX() + bounds.getWidth() * kWaveformWidthRatio);
    const float scaleY = (float) getHeight() / 152.0f;
    bounds.removeFromTop ((float) juce::roundToInt ((float) kHeaderHeight * scaleY));
    bounds.removeFromBottom ((float) (juce::roundToInt ((float) kBottomReadoutHeight * scaleY)
                                      + juce::roundToInt (kRulerHeight * scaleY)
                                      + (isZoomed ? juce::roundToInt (kOverviewHeight * scaleY) : 0)));
    result.plot = bounds;

    const auto layerParams = readLayerParameters();
    const float trimStart = juce::jlimit (0.0f, 0.99f, layerParams.trimStart);
    const float trimEnd = juce::jlimit (trimStart + 0.005f, 1.0f, layerParams.trimEnd);
    result.trimX0 = normToPlotX (trimStart, result.plot);
    result.trimX1 = normToPlotX (trimEnd, result.plot);
    result.trimW = juce::jmax (4.0f, result.trimX1 - result.trimX0);

    const float safeTotal = juce::jmax (1.0f, totalDurationMs);
    const float pitchSpeed = std::pow (2.0f, layerParams.pitchSemitones / 12.0f);
    result.trimmedDurationMs = juce::jmax (1.0f, safeTotal * (trimEnd - trimStart) / juce::jmax (0.01f, pitchSpeed));

    const auto sched = LayeredSampleEngine::computeEnvelopeSchedule (layerParams, result.trimmedDurationMs);
    const float attackEnd = sched.attackEndMs;
    const float holdEnd = sched.holdEndMs;
    const float decayEnd = sched.decayEndMs;
    const float releaseStart = sched.releaseStartMs;
    const float sustain = juce::jlimit (0.0f, 1.0f, layerParams.sustain);

    auto xAt = [&] (float ms)
    {
        return result.trimX0 + result.trimW * juce::jlimit (0.0f, result.trimmedDurationMs, ms) / result.trimmedDurationMs;
    };
    const float uiScale = (float) getHeight() / 152.0f;
    const float envTop = result.plot.getY() + 7.0f * uiScale;
    const float envBot = result.plot.getBottom() - 15.0f * uiScale;
    const float envH = juce::jmax (12.0f * uiScale, envBot - envTop);
    auto yAt = [&] (float level)
    {
        return envBot - envH * juce::jlimit (0.0f, 1.0f, level);
    };
    auto levelAt = [&] (float ms)
    {
        return LayeredSampleEngine::envelopeLevelAtMs (layerParams, sched, ms);
    };

    result.attack = { xAt (attackEnd), yAt (levelAt (attackEnd)) };
    result.hold = { xAt (holdEnd), yAt (levelAt (holdEnd)) };
    result.decay = { xAt (decayEnd), yAt (levelAt (decayEnd)) };
    const float sustainMid = decayEnd + (releaseStart - decayEnd) * 0.5f;
    result.sustain = { xAt (sustainMid), yAt (sustain) };
    const float relStartLevel = levelAt (releaseStart);
    result.release = { xAt (releaseStart), yAt (relStartLevel) };
    result.attackEndX = result.attack.x;
    result.holdEndX = result.hold.x;
    result.releaseStartX = result.release.x;

    const float attMidMs = attackEnd * 0.5f;
    result.tensionAttack = {
        xAt (attMidMs),
        yAt (LayeredSampleEngine::flSegmentLevel (0.5f, 0.0f, 1.0f, layerParams.attackCurve, layerParams.attackTension))
    };
    result.tensionAttackVisible = (result.attack.x - result.trimX0) >= 10.0f;

    const float decMidMs = holdEnd + (decayEnd - holdEnd) * 0.5f;
    result.tensionDecay = {
        xAt (decMidMs),
        yAt (LayeredSampleEngine::flSegmentLevel (0.5f, 1.0f, sustain, layerParams.decayCurve, layerParams.decayTension))
    };
    result.tensionDecayVisible = (result.decay.x - result.hold.x) >= 10.0f;

    const float relMidMs = releaseStart + sched.releaseMs * 0.5f;
    result.tensionRelease = {
        xAt (relMidMs),
        yAt (LayeredSampleEngine::flSegmentLevel (0.5f, relStartLevel, 0.0f, layerParams.releaseCurve, layerParams.releaseTension))
    };
    result.tensionReleaseVisible = (result.trimX1 - result.release.x) >= 10.0f;

    return result;
}

void LayerEnvelopeComponent::paint (juce::Graphics& g)
{
    auto fullBounds = getLocalBounds().toFloat();
    auto waveBox = fullBounds.removeFromLeft (fullBounds.getWidth() * kWaveformWidthRatio);
    const bool envEnabled = pEnvEnabled->load() > 0.5f;
    const juce::Colour flCurveColour = snap::bankAccentHi (layer);   // bright bank accent
    const juce::Colour flControlDot  = colour.interpolatedWith (ui::kMainText, 0.55f);

    g.setColour (ui::kMainBg);
    g.fillRoundedRectangle (waveBox, 4.0f);
    g.setColour (ui::kBorder);
    g.drawRoundedRectangle (waveBox.reduced (0.5f), 4.0f, 1.0f);

    const bool reversed = pReverse->load() > 0.5f;

    // Top strip is reserved for the sample name and slot navigation overlay.
    const float scaleY = (float) getHeight() / 152.0f;
    const float uiScale = scaleY;
    auto readoutBar = waveBox.removeFromTop ((float) juce::roundToInt ((float) kHeaderHeight * scaleY));
    g.setColour (ui::kInsetBg.withAlpha (0.92f));
    g.fillRect (readoutBar);
    g.setColour (ui::kBorder);
    g.drawHorizontalLine (juce::roundToInt (readoutBar.getBottom()), readoutBar.getX(), readoutBar.getRight());

    const auto preview = processor.getWaveformPreview (layer);
    const float totalDuration = preview.loaded ? (float) preview.durationMs : 1000.0f;
    const auto geometry = getGeometry (totalDuration);
    const auto plot = geometry.plot;
    const float vpSpan = 1.0f / juce::jmax (1.0f, zoomFactor);
    const float vpEnd = juce::jmin (1.0f, viewStartNorm + vpSpan);

    if (plot.getWidth() >= 24.0f && plot.getHeight() >= 24.0f)
    {
        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());

        // Subtle blue-grey grid
        g.setColour (ui::kGridLine.withAlpha (0.13f));
        const int gridDivs = juce::jlimit (8, 256, 8 * (1 << (int) std::floor (std::log2 (juce::jmax (1.0f, zoomFactor)))));
        const int firstGrid = juce::jmax (1, (int) std::ceil (viewStartNorm * (float) gridDivs));
        const int lastGrid = juce::jmin (gridDivs - 1, (int) std::floor (vpEnd * (float) gridDivs));
        for (int gIdx = firstGrid; gIdx <= lastGrid; ++gIdx)
        {
            const float x = normToPlotX ((float) gIdx / (float) gridDivs, plot);
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        }
        for (int r = 1; r < 4; ++r)
        {
            g.setColour (r == 2 ? ui::kGridLine.withAlpha (0.22f) : ui::kGridLine.withAlpha (0.10f));
            const float y = plot.getY() + plot.getHeight() * (float) r / 4.0f;
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        }

        if (preview.loaded)
        {
            const float centre = plot.getCentreY();
            const float amplitude = plot.getHeight() * 0.44f;
            g.setColour (ui::kNeutralWave);   // neutral grey-blue sample waveform
            const int cols = juce::jmax (1, (int) std::floor (plot.getWidth()));
            for (int col = 0; col < cols; ++col)
            {
                const float n0 = viewStartNorm + ((float) col / (float) cols) * vpSpan;
                const float n1 = viewStartNorm + ((float) (col + 1) / (float) cols) * vpSpan;
                const int b0 = juce::jlimit (0, LayeredSampleEngine::kPreviewPoints - 1,
                                             (int) std::floor (n0 * (float) LayeredSampleEngine::kPreviewPoints));
                const int b1 = juce::jlimit (b0, LayeredSampleEngine::kPreviewPoints - 1,
                                             (int) std::floor (n1 * (float) LayeredSampleEngine::kPreviewPoints));
                float minVal = 1.0f;
                float maxVal = -1.0f;
                for (int b = b0; b <= b1; ++b)
                {
                    const int srcIdx = reversed ? (LayeredSampleEngine::kPreviewPoints - 1 - b) : b;
                    minVal = juce::jmin (minVal, preview.minimum[(size_t) srcIdx]);
                    maxVal = juce::jmax (maxVal, preview.maximum[(size_t) srcIdx]);
                }
                const float x = plot.getX() + (float) col + 0.5f;
                const float top = centre - maxVal * amplitude;
                const float bottom = juce::jmax (top + 1.0f, centre - minVal * amplitude);
                g.drawVerticalLine (juce::roundToInt (x), top, bottom);
            }
        }
        else
        {
            g.setColour (ui::kSecondaryText.withAlpha (0.78f));
            g.setFont (juce::Font (12.0f * uiScale));
            g.drawText ("Drop audio file here or click sample browser above", plot.toNearestInt(),
                        juce::Justification::centred);
        }

        // Dim trimmed-out regions
        g.setColour (ui::kMainBg.withAlpha (0.72f));
        const float leftDimEnd = juce::jmin (plot.getRight(), geometry.trimX0);
        if (leftDimEnd > plot.getX())
            g.fillRect (plot.getX(), plot.getY(), leftDimEnd - plot.getX(), plot.getHeight());
        const float rightDimStart = juce::jmax (plot.getX(), geometry.trimX1);
        if (rightDimStart < plot.getRight())
            g.fillRect (rightDimStart, plot.getY(), plot.getRight() - rightDimStart, plot.getHeight());

        // Fade-in & Fade-out slopes
        const float fadeInPx = juce::jlimit (0.0f, geometry.trimW,
            (pFadeIn->load() / geometry.trimmedDurationMs) * geometry.trimW);
        const float fadeOutPx = juce::jlimit (0.0f, geometry.trimW,
            (pFadeOut->load() / geometry.trimmedDurationMs) * geometry.trimW);

        if (fadeInPx > 1.0f)
        {
            juce::Path pFade;
            pFade.startNewSubPath (geometry.trimX0, plot.getY());
            pFade.lineTo (geometry.trimX0 + fadeInPx, plot.getY());
            pFade.lineTo (geometry.trimX0, plot.getBottom());
            pFade.closeSubPath();
            g.setColour (ui::kMainBg.withAlpha (0.45f));
            g.fillPath (pFade);
            g.setColour (ui::kMainText.withAlpha (0.65f));
            g.drawLine (geometry.trimX0, plot.getBottom(), geometry.trimX0 + fadeInPx, plot.getY(), 1.2f);
        }
        if (fadeOutPx > 1.0f)
        {
            juce::Path pFade;
            pFade.startNewSubPath (geometry.trimX1, plot.getY());
            pFade.lineTo (geometry.trimX1 - fadeOutPx, plot.getY());
            pFade.lineTo (geometry.trimX1, plot.getBottom());
            pFade.closeSubPath();
            g.setColour (ui::kMainBg.withAlpha (0.45f));
            g.fillPath (pFade);
            g.setColour (ui::kMainText.withAlpha (0.65f));
            g.drawLine (geometry.trimX1 - fadeOutPx, plot.getY(), geometry.trimX1, plot.getBottom(), 1.2f);
        }

        const auto layerParams = readLayerParameters();
        const auto sched = LayeredSampleEngine::computeEnvelopeSchedule (layerParams, geometry.trimmedDurationMs);

        auto xAt = [&] (float timeMs)
        {
            return geometry.trimX0 + geometry.trimW * juce::jlimit (0.0f, geometry.trimmedDurationMs, timeMs) / geometry.trimmedDurationMs;
        };
        const float envTop = plot.getY() + 7.0f * uiScale;
        const float envBot = plot.getBottom() - 15.0f * uiScale;
        const float envH = juce::jmax (12.0f * uiScale, envBot - envTop);
        auto yAt = [&] (float level)
        {
            return envBot - envH * juce::jlimit (0.0f, 1.0f, level);
        };

        juce::Path envelope;
        envelope.startNewSubPath (xAt (0.0f), yAt (LayeredSampleEngine::evaluateCompleteEnvelopeAtMs (layerParams, sched, 0.0f)));

        for (int i = 1; i <= kCurveSteps; ++i)
        {
            const float timeMs = sched.attackEndMs * ((float) i / (float) kCurveSteps);
            envelope.lineTo (xAt (timeMs), yAt (LayeredSampleEngine::envelopeLevelAtMs (layerParams, sched, timeMs)));
        }

        envelope.lineTo (xAt (sched.holdEndMs), yAt (LayeredSampleEngine::envelopeLevelAtMs (layerParams, sched, sched.holdEndMs)));

        for (int i = 1; i <= kCurveSteps; ++i)
        {
            const float timeMs = sched.holdEndMs + (sched.decayEndMs - sched.holdEndMs) * ((float) i / (float) kCurveSteps);
            envelope.lineTo (xAt (timeMs), yAt (LayeredSampleEngine::envelopeLevelAtMs (layerParams, sched, timeMs)));
        }

        const float releaseStartLevel = LayeredSampleEngine::envelopeLevelAtMs (layerParams, sched, sched.releaseStartMs);
        envelope.lineTo (xAt (sched.releaseStartMs), yAt (releaseStartLevel));
        for (int i = 1; i <= kCurveSteps; ++i)
        {
            const float t = (float) i / (float) kCurveSteps;
            const float timeMs = sched.releaseStartMs + sched.releaseMs * t;
            envelope.lineTo (xAt (timeMs), yAt (LayeredSampleEngine::evaluateCompleteEnvelopeAtMs (layerParams, sched, timeMs)));
        }

        auto drawSectionMarker = [&] (float mx, const juce::String& tag)
        {
            if (mx < plot.getX() - 6.0f || mx > plot.getRight() + 6.0f)
                return;
            g.setColour (envEnabled ? ui::kMainText.withAlpha (0.40f) : ui::kSecondaryText.withAlpha (0.20f));
            g.drawVerticalLine (juce::roundToInt (mx), plot.getY(), plot.getBottom());

            const float bx = juce::jlimit (plot.getX() + 2.0f, plot.getRight() - 14.0f, mx + 1.0f);
            const float by = plot.getBottom() - 13.0f * uiScale;
            g.setColour (envEnabled ? ui::kMainText.withAlpha (0.72f) : ui::kDisabledText);
            g.fillRoundedRectangle (bx, by, 12.0f * uiScale, 12.0f * uiScale, 2.0f * uiScale);
            g.setColour (ui::kMainBg);
            g.setFont (juce::Font (8.5f * uiScale, juce::Font::bold));
            g.drawText (tag, juce::Rectangle<float> (bx, by, 12.0f * uiScale, 12.0f * uiScale), juce::Justification::centred);
        };

        drawSectionMarker (geometry.holdEndX, "D");
        drawSectionMarker (geometry.releaseStartX, "S");

        auto fill = envelope;
        fill.lineTo (geometry.trimX1, envBot);
        fill.lineTo (geometry.trimX0, envBot);
        fill.closeSubPath();
        g.setColour (envEnabled ? flCurveColour.withAlpha (0.14f) : ui::kSecondaryText.withAlpha (0.05f));
        g.fillPath (fill);

        g.setColour (envEnabled ? flCurveColour : ui::kSecondaryText.withAlpha (0.45f));
        g.strokePath (envelope, juce::PathStrokeType (envEnabled ? 1.9f : 1.3f,
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

        // Trim lines & flags
        g.setColour (flCurveColour);
        g.drawLine (geometry.trimX0, plot.getY(), geometry.trimX0, plot.getBottom(), 1.8f);
        g.drawLine (geometry.trimX1, plot.getY(), geometry.trimX1, plot.getBottom(), 1.8f);

        juce::Path frontFlag, backFlag;
        frontFlag.addTriangle (geometry.trimX0, plot.getY(),
                               geometry.trimX0 + 10.0f, plot.getY() + 5.5f,
                               geometry.trimX0, plot.getY() + 11.0f);
        backFlag.addTriangle (geometry.trimX1, plot.getY(),
                              geometry.trimX1 - 10.0f, plot.getY() + 5.5f,
                              geometry.trimX1, plot.getY() + 11.0f);
        g.fillPath (frontFlag);
        g.fillPath (backFlag);

        auto drawTensionHandle = [&] (juce::Point<float> pt, bool visible)
        {
            if (! visible)
                return;
            const float radius = kTensionRadius * uiScale;
            g.setColour (ui::kPanelBg);
            g.fillEllipse (pt.x - radius, pt.y - radius, 2.0f * radius, 2.0f * radius);
            g.setColour (envEnabled ? flCurveColour : ui::kSecondaryText.withAlpha (0.5f));
            g.drawEllipse (pt.x - radius, pt.y - radius, 2.0f * radius, 2.0f, 1.6f * uiScale);
        };

        drawTensionHandle (geometry.tensionAttack, geometry.tensionAttackVisible);
        drawTensionHandle (geometry.tensionDecay, geometry.tensionDecayVisible);
        drawTensionHandle (geometry.tensionRelease, geometry.tensionReleaseVisible);

        auto drawControlPoint = [&] (juce::Point<float> point, const juce::String& label)
        {
            if (point.x < plot.getX() - 12.0f || point.x > plot.getRight() + 12.0f)
                return;
            const float radius = kHandleRadius * uiScale;
            g.setColour (ui::kMainBg);
            g.fillEllipse (point.x - radius - 1.5f * uiScale, point.y - radius - 1.5f * uiScale,
                           2.0f * (radius + 1.5f * uiScale), 2.0f * (radius + 1.5f * uiScale));
            g.setColour (envEnabled ? flControlDot : ui::kDisabledText);
            g.fillEllipse (point.x - radius, point.y - radius, 2.0f * radius, 2.0f * radius);
            g.setColour (ui::kMainText);
            g.setFont (juce::Font (9.0f * uiScale, juce::Font::bold));
            const bool nearTop = (point.y - plot.getY()) < 18.0f * uiScale;
            const float labelX = juce::jlimit (plot.getX() + 1.0f * uiScale,
                                                plot.getRight() - 15.0f * uiScale,
                                                point.x - 7.0f * uiScale);
            const float labelY = nearTop ? (point.y + radius + 2.0f * uiScale)
                                         : (point.y - radius - 12.0f * uiScale);
            g.drawText (label, juce::Rectangle<float> (labelX, labelY, 14.0f * uiScale, 10.0f * uiScale),
                        juce::Justification::centred);
        };

        drawControlPoint (geometry.attack, "A");
        drawControlPoint (geometry.hold, "H");
        drawControlPoint (geometry.decay, "D");
        drawControlPoint (geometry.sustain, "S");
        drawControlPoint (geometry.release, "R");
        g.restoreState();
    }

    // Bottom readout row follows the SVG's three aligned Attack / Decay / Pitch groups.
    auto bottomReadout = waveBox.removeFromBottom (juce::roundToInt ((float) kBottomReadoutHeight * scaleY));
    g.setColour (ui::kInsetBg.withAlpha (0.92f));
    g.fillRect (bottomReadout);
    g.setColour (ui::kBorder);
    g.drawHorizontalLine (juce::roundToInt (bottomReadout.getY()), bottomReadout.getX(), bottomReadout.getRight());

    const float readoutY = bottomReadout.getY() + 3.0f * uiScale;
    const float readoutH = 13.0f * uiScale;
    const float baseX = bottomReadout.getX();
    g.setColour (ui::kSecondaryText);
    g.setFont (juce::Font (8.5f * uiScale));
    g.drawFittedText ("Attack",
                      juce::Rectangle<int> (juce::roundToInt (baseX + 9.0f * uiScale),
                                            juce::roundToInt (readoutY),
                                            juce::roundToInt (34.0f * uiScale),
                                            juce::roundToInt (readoutH)),
                      juce::Justification::centredLeft, 1);
    g.setColour (ui::kMainText);
    g.setFont (juce::Font (9.0f * uiScale, juce::Font::bold));
    g.drawFittedText (formatEnvelopeTime (pAttack->load()),
                      juce::Rectangle<int> (juce::roundToInt (baseX + 43.0f * uiScale),
                                            juce::roundToInt (readoutY),
                                            juce::roundToInt (34.0f * uiScale),
                                            juce::roundToInt (readoutH)),
                      juce::Justification::centredLeft, 1);

    g.setColour (ui::kSecondaryText);
    g.setFont (juce::Font (8.5f * uiScale));
    g.drawFittedText ("Decay",
                      juce::Rectangle<int> (juce::roundToInt (baseX + 84.0f * uiScale),
                                            juce::roundToInt (readoutY),
                                            juce::roundToInt (36.0f * uiScale),
                                            juce::roundToInt (readoutH)),
                      juce::Justification::centredLeft, 1);
    g.setColour (ui::kMainText);
    g.setFont (juce::Font (9.0f * uiScale, juce::Font::bold));
    g.drawFittedText (formatEnvelopeTime (pDecay->load()),
                      juce::Rectangle<int> (juce::roundToInt (baseX + 120.0f * uiScale),
                                            juce::roundToInt (readoutY),
                                            juce::roundToInt (40.0f * uiScale),
                                            juce::roundToInt (readoutH)),
                      juce::Justification::centredLeft, 1);

    // Bottom time ruler.
    auto ruler = waveBox.removeFromBottom (juce::roundToInt (kRulerHeight * scaleY));
    g.setColour (ui::kInsetBg.withAlpha (0.85f));
    g.fillRect (ruler);
    g.setColour (ui::kSecondaryText);
    g.setFont (juce::Font (9.5f * uiScale));
    for (int i = 0; i <= 4; ++i)
    {
        const float norm = viewStartNorm + vpSpan * ((float) i / 4.0f);
        const int ms = juce::roundToInt (totalDuration * norm);
        const juce::String lbl = ms == 0 ? "0:00"
            : "0:" + juce::String (ms / 1000).paddedLeft ('0', 2) + ":" + juce::String (ms % 1000).paddedLeft ('0', 3);
        const float tx = ruler.getX() + ruler.getWidth() * (float) i / 4.0f;
        const auto just = i == 0 ? juce::Justification::centredLeft
                        : (i == 4 ? juce::Justification::centredRight : juce::Justification::centred);
        const float boxX = i == 0 ? tx + 4.0f : (i == 4 ? tx - 64.0f : tx - 30.0f);
        g.drawText (lbl, juce::Rectangle<float> (boxX, ruler.getY(), 60.0f, ruler.getHeight()), just);
    }

    // Overview bar when zoomed.
    if (zoomFactor > 1.01f)
    {
        const float trimStart = juce::jlimit (0.0f, 0.99f, pTrimStart->load());
        const float trimEnd = juce::jlimit (trimStart + 0.005f, 1.0f, pTrimEnd->load());
        auto ov = waveBox.removeFromBottom (juce::roundToInt (kOverviewHeight * scaleY));
        g.setColour (ui::kMainBg.withAlpha (0.92f));
        g.fillRect (ov);
        g.setColour (ui::kBorder);
        g.drawHorizontalLine (juce::roundToInt (ov.getY()), ov.getX(), ov.getRight());

        const float trimBoxX = ov.getX() + trimStart * ov.getWidth();
        const float trimBoxW = juce::jmax (2.0f, (trimEnd - trimStart) * ov.getWidth());
        g.setColour (flCurveColour.withAlpha (0.30f));
        g.fillRect (trimBoxX, ov.getY() + 4.0f, trimBoxW, ov.getHeight() - 8.0f);

        const float thumbX = ov.getX() + viewStartNorm * ov.getWidth();
        const float thumbW = juce::jmax (10.0f, vpSpan * ov.getWidth());
        g.setColour (ui::kMainText.withAlpha (0.20f));
        g.fillRoundedRectangle (thumbX, ov.getY() + 1.5f, thumbW, ov.getHeight() - 3.0f, 2.0f);
        g.setColour (ui::kMainText.withAlpha (0.75f));
        g.drawRoundedRectangle (thumbX, ov.getY() + 1.5f, thumbW, ov.getHeight() - 3.0f, 2.0f, 1.0f);
    }

    // Parameter bank: compact Shift / Width / Pan / Pogo / Trim controls and
    // a dedicated vertical Volume fader. Sample wave and envelope remain live.
    g.setColour (ui::kPanelBg);
    g.fillRoundedRectangle (controlStripBounds.toFloat(), 4.0f);
    g.setColour (ui::kBorder);
    g.drawRoundedRectangle (controlStripBounds.toFloat().reduced (0.5f), 4.0f, 1.0f);

    g.setColour (ui::kControlBg);
    g.fillRoundedRectangle (gainCardBounds, 3.0f * uiScale);
    g.setColour (ui::kBorder);
    g.drawRoundedRectangle (gainCardBounds.reduced (0.5f * uiScale), 3.0f * uiScale, 1.0f * uiScale);

    // SVG Volume card: title, vertical track and dB value have independent, non-overlapping zones.
    g.setColour (ui::kSecondaryText);
    g.setFont (juce::Font (10.0f * uiScale, juce::Font::bold));
    g.drawFittedText ("Volume",
                      juce::Rectangle<int> (juce::roundToInt (gainCardBounds.getX()),
                                            juce::roundToInt (gainCardBounds.getY() + 2.0f * uiScale),
                                            juce::roundToInt (gainCardBounds.getWidth()),
                                            juce::roundToInt (14.0f * uiScale)),
                      juce::Justification::centred, 1);
    g.setColour (ui::kMainText);
    g.setFont (juce::Font (10.5f * uiScale, juce::Font::bold));
    g.drawFittedText (gainSlider.getTextFromValue (gainSlider.getValue()),
                      juce::Rectangle<int> (juce::roundToInt (gainCardBounds.getX() + 1.0f * uiScale),
                                            juce::roundToInt (gainCardBounds.getY() + 59.0f * uiScale),
                                            juce::roundToInt (gainCardBounds.getWidth() - 2.0f * uiScale),
                                            juce::roundToInt (15.0f * uiScale)),
                      juce::Justification::centred, 1);

}

juce::Rectangle<int> LayerEnvelopeComponent::getSampleHeaderBounds() const noexcept
{
    const int waveWidth = juce::roundToInt ((float) getWidth() * kWaveformWidthRatio);
    const float scaleY = (float) getHeight() / 152.0f;
    return { 0, 0, waveWidth, juce::jmax (1, juce::roundToInt ((float) kHeaderHeight * scaleY)) };
}

void LayerEnvelopeComponent::resized()
{
    auto full = getLocalBounds();
    const int waveW = juce::roundToInt ((float) full.getWidth() * kWaveformWidthRatio);
    auto waveArea = full.removeFromLeft (waveW);
    const int gutterW = juce::roundToInt ((float) getWidth() * kWaveformGapRatio);
    full.removeFromLeft (juce::jlimit (0, full.getWidth(), gutterW));
    auto controls = full;
    controlStripBounds = controls;

    const float scaleY = (float) getHeight() / 152.0f;
    auto headerBar = waveArea.removeFromTop (juce::roundToInt ((float) kHeaderHeight * scaleY));
    envEnableBtn.setBounds (headerBar.removeFromRight (40));

    auto readout = waveArea.removeFromBottom (juce::roundToInt ((float) kBottomReadoutHeight * scaleY));
    const int pitchWidth = juce::roundToInt (63.0f * scaleY);
    pitchSlider.setBounds (readout.removeFromRight (juce::jlimit (juce::roundToInt (52.0f * scaleY),
                                                                  readout.getWidth(), pitchWidth)));
    waveArea.removeFromBottom (juce::roundToInt (kRulerHeight * scaleY));
    overviewBarBounds = (zoomFactor > 1.01f)
        ? waveArea.removeFromBottom (juce::roundToInt (kOverviewHeight * scaleY)).toFloat()
        : juce::Rectangle<float>();

    const int trimHeight = juce::roundToInt (32.0f * scaleY);
    trimRange->setBounds (controls.removeFromBottom (juce::jlimit (24, controls.getHeight(), trimHeight)));

    const int colGap = juce::roundToInt (6.0f * (float) controls.getWidth() / 134.0f);
    const int leftW = juce::roundToInt ((float) controls.getWidth() * 69.0f / 134.0f);
    auto leftCol = controls.removeFromLeft (juce::jlimit (1, controls.getWidth(), leftW));
    controls.removeFromLeft (juce::jlimit (0, controls.getWidth(), colGap));
    auto rightCol = controls;

    const int cardH = juce::roundToInt (36.0f * scaleY);
    const int cardGap = juce::roundToInt (4.0f * scaleY);

    delaySlider.setBounds (leftCol.removeFromTop (juce::jlimit (1, leftCol.getHeight(), cardH)));
    const int phaseSize = juce::jlimit (12, 16, juce::roundToInt (14.0f * scaleY));
    phaseInvertBtn.setBounds (delaySlider.getRight() - phaseSize - 4,
                              delaySlider.getY() + 2, phaseSize, phaseSize);
    phaseInvertBtn.toFront (false);
    leftCol.removeFromTop (juce::jlimit (0, leftCol.getHeight(), cardGap));
    widthSlider.setBounds (leftCol.removeFromTop (juce::jlimit (1, leftCol.getHeight(), cardH)));
    leftCol.removeFromTop (juce::jlimit (0, leftCol.getHeight(), cardGap));
    panSlider.setBounds (leftCol.removeFromTop (juce::jlimit (1, leftCol.getHeight(), cardH)));

    gainCardBounds = rightCol.removeFromTop (juce::jmin (rightCol.getHeight(),
                                      juce::roundToInt (76.0f * scaleY))).toFloat();
    rightCol.removeFromTop (juce::jlimit (0, rightCol.getHeight(), cardGap));
    pogoSlider.setBounds (rightCol.removeFromTop (juce::jmin (rightCol.getHeight(), cardH)));
    const auto gainCard = gainCardBounds.toNearestInt();
    gainSlider.setBounds (gainCard.getX(),
                          gainCard.getY() + juce::roundToInt (21.0f * scaleY),
                          gainCard.getWidth(),
                          juce::roundToInt (36.0f * scaleY));
}


LayerEnvelopeComponent::Handle LayerEnvelopeComponent::hitTestHandle (juce::Point<float> pos,
                                                                      const Geometry& g,
                                                                      float maxRadius) const noexcept
{
    const float uiScale = (float) getHeight() / 152.0f;
    maxRadius *= uiScale;
    struct Candidate { Handle h; juce::Point<float> pt; bool active; };
    const Candidate list[] = {
        { Handle::TensionAttack,  g.tensionAttack,  g.tensionAttackVisible },
        { Handle::TensionDecay,   g.tensionDecay,   g.tensionDecayVisible },
        { Handle::TensionRelease, g.tensionRelease, g.tensionReleaseVisible },
        { Handle::Attack,         g.attack,         true },
        { Handle::Hold,           g.hold,           true },
        { Handle::Decay,          g.decay,          true },
        { Handle::Sustain,        g.sustain,        true },
        { Handle::Release,        g.release,        true },
    };

    float bestDist = maxRadius;
    Handle best = Handle::None;
    for (const auto& c : list)
    {
        if (! c.active) continue;
        const float d = pos.getDistanceFrom (c.pt);
        if (d < bestDist)
        {
            bestDist = d;
            best = c.h;
        }
    }

    if (best == Handle::None)
    {
        const bool startVisible = g.trimX0 >= g.plot.getX() - 10.0f * uiScale
                               && g.trimX0 <= g.plot.getRight() + 10.0f * uiScale;
        const bool endVisible = g.trimX1 >= g.plot.getX() - 10.0f * uiScale
                             && g.trimX1 <= g.plot.getRight() + 10.0f * uiScale;
        const float distStart = startVisible ? std::abs (pos.x - g.trimX0) : 1.0e6f;
        const float distEnd   = endVisible ? std::abs (pos.x - g.trimX1) : 1.0e6f;
        if (distStart <= 12.0f * uiScale && distStart <= distEnd)
            best = Handle::TrimStart;
        else if (distEnd <= 12.0f * uiScale)
            best = Handle::TrimEnd;
    }

    return best;
}

void LayerEnvelopeComponent::resolveCircleForContext (juce::Point<float> pos,
                                                      const Geometry& g,
                                                      Segment& outSegment,
                                                      juce::Point<float>& outCirclePos,
                                                      juce::String& outTitle) const noexcept
{
    const auto hit = hitTestHandle (pos, g, 18.0f);
    switch (hit)
    {
        case Handle::Attack:
            outSegment = Segment::Attack;
            outCirclePos = g.attack;
            outTitle = "Point A (Attack)";
            return;
        case Handle::TensionAttack:
            outSegment = Segment::Attack;
            outCirclePos = g.tensionAttack;
            outTitle = "Attack Tension";
            return;
        case Handle::Hold:
            outSegment = Segment::Decay;
            outCirclePos = g.hold;
            outTitle = "Point H (Decay Start)";
            return;
        case Handle::Decay:
            outSegment = Segment::Decay;
            outCirclePos = g.decay;
            outTitle = "Point D (Decay End)";
            return;
        case Handle::Sustain:
            outSegment = Segment::Decay;
            outCirclePos = g.sustain;
            outTitle = "Point S (Sustain)";
            return;
        case Handle::TensionDecay:
            outSegment = Segment::Decay;
            outCirclePos = g.tensionDecay;
            outTitle = "Decay Tension";
            return;
        case Handle::Release:
            outSegment = Segment::Release;
            outCirclePos = g.release;
            outTitle = "Point R (Release)";
            return;
        case Handle::TensionRelease:
            outSegment = Segment::Release;
            outCirclePos = g.tensionRelease;
            outTitle = "Release Tension";
            return;
        default:
            break;
    }

    outCirclePos = pos;
    if (pos.x <= g.attackEndX + 8.0f)
    {
        outSegment = Segment::Attack;
        outTitle = "Attack Curve";
    }
    else if (pos.x >= g.releaseStartX - 8.0f)
    {
        outSegment = Segment::Release;
        outTitle = "Release Curve";
    }
    else
    {
        outSegment = Segment::Decay;
        outTitle = "Decay Curve";
    }
}

void LayerEnvelopeComponent::showFlCurveMenu (Segment segment,
                                              juce::Point<float> circleLocalPos,
                                              const juce::String& title)
{
    const juce::String curveId = segment == Segment::Attack ? ids::layerAttackCurve (layer)
        : (segment == Segment::Decay ? ids::layerDecayCurve (layer) : ids::layerReleaseCurve (layer));
    const juce::String tensionId = segment == Segment::Attack ? ids::layerAttackTension (layer)
        : (segment == Segment::Decay ? ids::layerDecayTension (layer) : ids::layerReleaseTension (layer));
    const int currentMode = juce::roundToInt (segment == Segment::Attack ? pAttackCurve->load()
        : (segment == Segment::Decay ? pDecayCurve->load() : pReleaseCurve->load()));
    const bool envOn = pEnvEnabled->load() > 0.5f;

    juce::PopupMenu menu;
    menu.addSectionHeader (title);

    const char* modes[] = { "Hold", "Smooth", "Single curve", "Half sine", "Stairs", "Smooth stairs", "Pulse", "Wave" };
    for (int i = 0; i < 8; ++i)
        menu.addItem (100 + i, modes[i], true, currentMode == i);

    menu.addSeparator();
    menu.addItem (200, "Reset Tension (0%)");
    menu.addItem (201, envOn ? "Disable Envelope" : "Enable Envelope");

    const auto screenCirclePt = localPointToGlobal (circleLocalPos.roundToInt());
    const juce::Rectangle<int> anchorRect (screenCirclePt.x - 6, screenCirclePt.y - 6, 12, 12);

    auto options = juce::PopupMenu::Options()
                       .withTargetScreenArea (anchorRect)
                       .withStandardItemHeight (20);
    if (auto* top = getTopLevelComponent())
        options = options.withParentComponent (top);

    juce::Component::SafePointer<LayerEnvelopeComponent> safeThis (this);
    menu.showMenuAsync (options, [safeThis, curveId, tensionId, envOn] (int result)
    {
        if (safeThis == nullptr)
            return;
        if (result >= 100 && result < 108)
            safeThis->setParameter (curveId, (float) (result - 100));
        else if (result == 200)
            safeThis->setParameter (tensionId, 0.0f);
        else if (result == 201)
            safeThis->setParameter (ids::layerEnvEnabled (safeThis->layer), envOn ? 0.0f : 1.0f);
    });
}

void LayerEnvelopeComponent::beginGesture (Handle handle)
{
    activeHandle = handle;
    juce::String parameterId;
    switch (handle)
    {
        case Handle::TrimStart:      parameterId = ids::layerTrimStart (layer); break;
        case Handle::TrimEnd:        parameterId = ids::layerTrimEnd (layer); break;
        case Handle::Attack:         parameterId = ids::layerAttack (layer); break;
        case Handle::Hold:           parameterId = ids::layerHold (layer); break;
        case Handle::Decay:          parameterId = ids::layerDecay (layer); break;
        case Handle::Sustain:        parameterId = ids::layerSustain (layer); break;
        case Handle::Release:        parameterId = ids::layerRelease (layer); break;
        case Handle::TensionAttack:  parameterId = ids::layerAttackTension (layer); break;
        case Handle::TensionDecay:   parameterId = ids::layerDecayTension (layer); break;
        case Handle::TensionRelease: parameterId = ids::layerReleaseTension (layer); break;
        default: break;
    }

    if (parameterId.isNotEmpty())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.apvts.getParameter (parameterId)))
            parameter->beginChangeGesture();
}

void LayerEnvelopeComponent::endGesture()
{
    if (activeHandle == Handle::None)
        return;

    juce::String parameterId;
    switch (activeHandle)
    {
        case Handle::TrimStart:      parameterId = ids::layerTrimStart (layer); break;
        case Handle::TrimEnd:        parameterId = ids::layerTrimEnd (layer); break;
        case Handle::Attack:         parameterId = ids::layerAttack (layer); break;
        case Handle::Hold:           parameterId = ids::layerHold (layer); break;
        case Handle::Decay:          parameterId = ids::layerDecay (layer); break;
        case Handle::Sustain:        parameterId = ids::layerSustain (layer); break;
        case Handle::Release:        parameterId = ids::layerRelease (layer); break;
        case Handle::TensionAttack:  parameterId = ids::layerAttackTension (layer); break;
        case Handle::TensionDecay:   parameterId = ids::layerDecayTension (layer); break;
        case Handle::TensionRelease: parameterId = ids::layerReleaseTension (layer); break;
        default: break;
    }

    if (parameterId.isNotEmpty())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.apvts.getParameter (parameterId)))
            parameter->endChangeGesture();

    activeHandle = Handle::None;
}

void LayerEnvelopeComponent::setParameter (const juce::String& parameterId, float value)
{
    if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.apvts.getParameter (parameterId)))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void LayerEnvelopeComponent::applyDrag (juce::Point<float> position)
{
    if (activeHandle == Handle::OverviewPan && overviewBarBounds.getWidth() > 1.0f)
    {
        const float norm = juce::jlimit (0.0f, 1.0f,
            (position.x - overviewBarBounds.getX()) / overviewBarBounds.getWidth());
        const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
        clampViewport (norm - span * 0.5f, zoomFactor);
        repaint();
        return;
    }

    const auto preview = processor.getWaveformPreview (layer);
    const auto geometry = getGeometry (preview.loaded ? (float) preview.durationMs : 1000.0f);

    if (activeHandle == Handle::Pan)
    {
        const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
        const float dx = position.x - dragStartX;
        clampViewport (dragStartViewStart - (dx / juce::jmax (1.0f, geometry.plot.getWidth())) * span,
                       zoomFactor);
        repaint();
        return;
    }

    if (activeHandle == Handle::TensionAttack
        || activeHandle == Handle::TensionDecay
        || activeHandle == Handle::TensionRelease)
    {
        const float uiScale = (float) getHeight() / 152.0f;
        const float delta = (dragStartY - position.y) / (85.0f * juce::jmax (0.1f, uiScale));
        const float nextTen = juce::jlimit (-1.0f, 1.0f, dragStartTension + delta);
        if (activeHandle == Handle::TensionAttack)
            setParameter (ids::layerAttackTension (layer), nextTen);
        else if (activeHandle == Handle::TensionDecay)
            setParameter (ids::layerDecayTension (layer), nextTen);
        else
            setParameter (ids::layerReleaseTension (layer), nextTen);
        repaint();
        return;
    }

    if (activeHandle == Handle::TrimStart)
    {
        const float rawNorm = plotXToNorm (position.x, geometry.plot);
        const float norm = juce::jlimit (0.0f, juce::jmax (0.0f, pTrimEnd->load() - 0.005f), rawNorm);
        setParameter (ids::layerTrimStart (layer), norm);
        repaint();
        return;
    }
    if (activeHandle == Handle::TrimEnd)
    {
        const float rawNorm = plotXToNorm (position.x, geometry.plot);
        const float norm = juce::jlimit (juce::jmin (1.0f, pTrimStart->load() + 0.005f), 1.0f, rawNorm);
        setParameter (ids::layerTrimEnd (layer), norm);
        repaint();
        return;
    }

    const float duration = juce::jmax (1.0f, geometry.trimmedDurationMs);
    const float x = juce::jlimit (0.0f, 1.0f,
                                  (position.x - geometry.trimX0)
                                      / juce::jmax (1.0f, geometry.trimW));
    const float timeMs = x * duration;

    switch (activeHandle)
    {
        case Handle::Attack:
            setParameter (ids::layerAttack (layer), juce::jlimit (0.1f, 2000.0f, timeMs));
            break;
        case Handle::Hold:
            setParameter (ids::layerHold (layer),
                          juce::jlimit (0.0f, 2000.0f, timeMs - pAttack->load()));
            break;
        case Handle::Decay:
            setParameter (ids::layerDecay (layer),
                          juce::jlimit (0.1f, 5000.0f, timeMs - pAttack->load() - pHold->load()));
            break;
        case Handle::Sustain:
        {
            const float envTop = geometry.plot.getY() + 7.0f;
            const float uiScale = (float) getHeight() / 152.0f;
            const float envBot = geometry.plot.getBottom() - 15.0f * uiScale;
            const float envH = juce::jmax (12.0f * uiScale, envBot - envTop);
            const float level = juce::jlimit (0.0f, 1.0f, (envBot - position.y) / envH);
            setParameter (ids::layerSustain (layer), level);
            break;
        }
        case Handle::Release:
            setParameter (ids::layerRelease (layer), juce::jlimit (0.1f, 10000.0f, duration - timeMs));
            break;
        default:
            break;
    }

    repaint();
}

void LayerEnvelopeComponent::mouseDown (const juce::MouseEvent& event)
{
    const auto preview = processor.getWaveformPreview (layer);
    const auto geometry = getGeometry (preview.loaded ? (float) preview.durationMs : 1000.0f);

    if (event.mods.isPopupMenu())
    {
        if (geometry.plot.contains (event.position))
        {
            Segment segment = Segment::Decay;
            juce::Point<float> circlePos = event.position;
            juce::String title;
            resolveCircleForContext (event.position, geometry, segment, circlePos, title);
            showFlCurveMenu (segment, circlePos, title);
        }
        return;
    }

    endGesture();

    if (zoomFactor > 1.01f && overviewBarBounds.contains (event.position))
    {
        activeHandle = Handle::OverviewPan;
        applyDrag (event.position);
        return;
    }

    if ((event.mods.isMiddleButtonDown() || event.mods.isAltDown())
        && zoomFactor > 1.01f && geometry.plot.contains (event.position))
    {
        activeHandle = Handle::Pan;
        dragStartX = event.position.x;
        dragStartViewStart = viewStartNorm;
        return;
    }

    const Handle bestHandle = hitTestHandle (event.position, geometry);
    if (bestHandle != Handle::None)
    {
        dragStartX = event.position.x;
        dragStartY = event.position.y;
        dragStartViewStart = viewStartNorm;
        if (bestHandle == Handle::TensionAttack) dragStartTension = pAttackTension->load();
        else if (bestHandle == Handle::TensionDecay) dragStartTension = pDecayTension->load();
        else if (bestHandle == Handle::TensionRelease) dragStartTension = pReleaseTension->load();
        beginGesture (bestHandle);
    }
}

void LayerEnvelopeComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (activeHandle != Handle::None)
        applyDrag (event.position);
}

void LayerEnvelopeComponent::mouseUp (const juce::MouseEvent&)
{
    if (activeHandle == Handle::Pan || activeHandle == Handle::OverviewPan)
    {
        activeHandle = Handle::None;
        return;
    }
    endGesture();
}

void LayerEnvelopeComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    const auto preview = processor.getWaveformPreview (layer);
    const auto geometry = getGeometry (preview.loaded ? (float) preview.durationMs : 1000.0f);
    const Handle hit = hitTestHandle (event.position, geometry);
    if (hit == Handle::TensionAttack) setParameter (ids::layerAttackTension (layer), 0.0f);
    else if (hit == Handle::TensionDecay) setParameter (ids::layerDecayTension (layer), 0.0f);
    else if (hit == Handle::TensionRelease) setParameter (ids::layerReleaseTension (layer), 0.0f);
}

void LayerEnvelopeComponent::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto preview = processor.getWaveformPreview (layer);
    const auto geometry = getGeometry (preview.loaded ? (float) preview.durationMs : 1000.0f);
    if (! geometry.plot.contains (event.position))
    {
        juce::Component::mouseWheelMove (event, wheel);
        return;
    }

    const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
    if (event.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : -wheel.deltaY;
        clampViewport (viewStartNorm + delta * span * 0.35f, zoomFactor);
        resized();
        repaint();
        return;
    }

    if (std::abs (wheel.deltaY) > 0.0001f)
    {
        const float anchorNorm = plotXToNorm (event.position.x, geometry.plot);
        const float factor = wheel.deltaY > 0.0f ? 1.35f : (1.0f / 1.35f);
        zoomAroundNormalized (anchorNorm, factor);
        resized();
        repaint();
    }
}