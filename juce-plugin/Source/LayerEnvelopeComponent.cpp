#include "LayerEnvelopeComponent.h"

#include <cmath>

namespace ui = wavelens::ui;

namespace
{
    constexpr float kHandleRadius = 4.6f;
    constexpr float kTensionRadius = 3.6f;
    constexpr int kCurveSteps = 96;
    constexpr int kHeaderHeight = 22;
    constexpr float kRulerHeight = 16.0f;
    constexpr float kOverviewHeight = 14.0f;
    constexpr float kMaxWaveformZoom = 32.0f;
    constexpr float kTrimHandleRadius = 3.0f;

    /** Fraction of the editor width used by the sample box (SVG: 226/399). */
    constexpr float kSampleBoxWidthRatio = 226.0f / 399.0f;
    /** Editor width in SVG units (the box is 226 of these wide). */
    constexpr float kBoxUnitX = 226.0f / kSampleBoxWidthRatio;

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
        return juce::String (db > 0.05 ? "+" : "") + juce::String (db, 2) + "dB";
    }

    /** Min/max waveform amplitude of one display column, honouring reverse. */
    float columnPeak (const LayeredSampleEngine::WaveformPreview& preview,
                      bool reversed, float viewStart, float viewSpan,
                      int col, int cols, bool positive)
    {
        const float n0 = viewStart + ((float) col / (float) cols) * viewSpan;
        const float n1 = viewStart + ((float) (col + 1) / (float) cols) * viewSpan;
        const int b0 = juce::jlimit (0, LayeredSampleEngine::kPreviewPoints - 1,
                                     (int) std::floor (n0 * (float) LayeredSampleEngine::kPreviewPoints));
        const int b1 = juce::jlimit (b0, LayeredSampleEngine::kPreviewPoints - 1,
                                     (int) std::floor (n1 * (float) LayeredSampleEngine::kPreviewPoints));
        float best = positive ? -1.0f : 1.0f;
        for (int b = b0; b <= b1; ++b)
        {
            const int srcIdx = reversed ? (LayeredSampleEngine::kPreviewPoints - 1 - b) : b;
            const float v = positive ? preview.maximum[(size_t) srcIdx]
                                     : preview.minimum[(size_t) srcIdx];
            best = positive ? juce::jmax (best, v) : juce::jmin (best, v);
        }
        return best;
    }
}

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
    pPolarity = state.getRawParameterValue (ids::layerPolarity (layer));
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
    envEnableAttachment = std::make_unique<ButtonAtt> (state, ids::layerEnvEnabled (layer), envEnableBtn);

    // REV switch next to ENV: same toggle-pill style as ENV (accent border
    // + tint when latched on); labelled with text instead of the arrows icon.
    reverseBtn.setButtonText ("REV");
    reverseBtn.setClickingTogglesState (true);
    reverseBtn.setTooltip ("Reverse sample playback");
    reverseBtn.getProperties().set ("accentColour", (juce::int64) colour.getARGB());
    addAndMakeVisible (reverseBtn);
    reverseAttachment = std::make_unique<ButtonAtt> (state, ids::layerReverse (layer), reverseBtn);

    // SVG phase-invert icon on the Shift card: circle with a slash.
    phaseBtn.setTooltip ("Invert layer polarity (phase)");
    phaseBtn.setClickingTogglesState (true);
    phaseBtn.getProperties().set ("isPhaseIcon", true);
    phaseBtn.getProperties().set ("accentColour", (juce::int64) colour.getARGB());
    addAndMakeVisible (phaseBtn);
    phaseAttachment = std::make_unique<ButtonAtt> (state, ids::layerPolarity (layer), phaseBtn);

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

    // SVG "Volume" fader card: vertical track with accent active span
    // (#6d7ef2 / #985ee6) and small circular thumb; value readout below.
    gainSlider.setSliderStyle (juce::Slider::LinearVertical);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setDoubleClickReturnValue (true, 1.0);
    gainSlider.setColour (juce::Slider::trackColourId, snap::bankAccent (layer));
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

    const juce::Colour secondaryFill = colour;
    prepareTile (delaySlider,     "Shift",    "Layer timing shift (-100 ms to +100 ms)",              colour,        true,  0.0);
    prepareTile (pitchSlider,     "Pitch",    "Layer pitch (-24 to +24 semitones)",                   colour,        true,  0.0);
    prepareTile (panSlider,       "Pan",      "Stereo pan (Left / Centre / Right)",                   colour,        true,  0.0);
    prepareTile (widthSlider,     "Width",    "Stereo width (0% mono, 100% original, 200% wide)",    colour,        false, 1.0);
    prepareTile (pogoSlider,      "Pogo",     "FL Sampler style pitch-bend envelope (-100% to +100%)", secondaryFill, true,  0.0);
    prepareTile (fadeInSlider,    "FADE IN",  "Sample fade-in duration (0 ms to 2000 ms)",            colour,        false, 0.0);
    prepareTile (fadeOutSlider,   "FADE OUT", "Sample fade-out duration (0 ms to 5000 ms)",           colour,        false, 0.0);
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

    // SVG trim card uses a single dual-handle range: "Str Trim" (front) and
    // "Bk Trim" (back) share one track, so the standalone tile sliders hide.
    prepareTile (trimStartSlider, "TRIM F", "Sample front trim (0% to 99%)", secondaryFill, false, 0.0);
    prepareTile (trimEndSlider,   "TRIM B", "Sample back trim (0.5% to 100%)", secondaryFill, false, 1.0);
    trimStartAttachment = std::make_unique<SliderAtt> (state, ids::layerTrimStart (layer), trimStartSlider);
    trimEndAttachment   = std::make_unique<SliderAtt> (state, ids::layerTrimEnd (layer),   trimEndSlider);

    trimStartSlider.onValueChange = [this]
    {
        const float maxStart = juce::jmax (0.0f, (float) trimEndSlider.getValue() - 0.02f);
        if (trimStartSlider.getValue() > maxStart)
            trimStartSlider.setValue (maxStart, juce::sendNotificationSync);
    };
    trimEndSlider.onValueChange = [this]
    {
        const float minEnd = juce::jmin (1.0f, (float) trimStartSlider.getValue() + 0.02f);
        if (trimEndSlider.getValue() < minEnd)
            trimEndSlider.setValue (minEnd, juce::sendNotificationSync);
    };

    delaySlider.textFromValueFunction = [] (double v) { return juce::String (v > 0.0 ? "+" : "") + juce::String (v, 0) + "ms"; };
    pitchSlider.textFromValueFunction = [] (double v) { return juce::String (v, 2); };
    panSlider.textFromValueFunction   = [] (double value)
    {
        if (std::abs (value) < 0.005)
            return juce::String ("C");
        return juce::String (std::abs (value) * 100.0, 0) + (value < 0.0 ? "L" : "R");
    };
    widthSlider.textFromValueFunction  = [] (double value) { return juce::String (value * 100.0, 0) + "%"; };
    pogoSlider.textFromValueFunction    = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    fadeInSlider.textFromValueFunction  = [] (double v) { return formatEnvelopeTime ((float) v); };
    fadeOutSlider.textFromValueFunction = [] (double v) { return formatEnvelopeTime ((float) v); };
    trimStartSlider.textFromValueFunction = [] (double v) { return juce::String (v * 100.0, 0) + "%"; };
    trimEndSlider.textFromValueFunction   = [] (double v) { return juce::String (v * 100.0, 0) + "%"; };
    hpSlider.textFromValueFunction = [] (double hz)
    {
        if (hz >= 999.5)
            return juce::String (hz / 1000.0, 1) + " kHz";
        return juce::String (juce::roundToInt (hz)) + " Hz";
    };
    lpSlider.textFromValueFunction = hpSlider.textFromValueFunction;

    for (auto* s : { &gainSlider, &delaySlider, &pitchSlider, &panSlider, &widthSlider, &pogoSlider,
                     &fadeInSlider, &fadeOutSlider, &trimStartSlider, &trimEndSlider,
                     &hpSlider, &lpSlider })
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
    zoomOutBtn.onClick = [this]
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

    // The compact SVG layout only shows: sample box, ENV + reverse, zoom-less
    // mini waveform, and the parameter card deck. Everything else stays
    // functional but hidden (context menus / sample menu still reach them).
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
    trimStartSlider.setVisible (false);
    trimEndSlider.setVisible (false);

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
    auto bounds = getLocalBounds().toFloat();
    // SVG sample box: x 16..242, y 400..552; the name row takes the top 20
    // and the Attack/Decay/Pitch readout the bottom 16.
    const float sx = bounds.getWidth() / 399.0f;
    const float sy = bounds.getHeight() / 158.0f;
    result.plot = juce::Rectangle<float> (16.0f * sx, 20.0f * sy,
                                          226.0f * sx, (152.0f - 20.0f - 16.0f) * sy);

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
    const float envTop = result.plot.getY() + 7.0f;
    const float envBot = result.plot.getBottom() - 15.0f;
    const float envH = juce::jmax (12.0f, envBot - envTop);
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
    const float sx = fullBounds.getWidth() / 399.0f;
    const float sy = fullBounds.getHeight() / 158.0f;
    auto svg = [sx, sy] (float x, float y, float w, float h)
    {
        return juce::Rectangle<float> (x * sx, (y - 400.0f) * sy, w * sx, h * sy);
    };

    auto waveBox = svg (16, 400, 226, 152);
    const bool envEnabled = pEnvEnabled->load() > 0.5f;
    const juce::Colour flCurveColour = colour;   // bank trace colour

    // ---- SVG sample box: #131118 fill, #282632 frame, radius 2 ----
    g.setColour (snap::inset);
    g.fillRoundedRectangle (waveBox, 2.0f);
    g.setColour (ui::kBorder);
    g.drawRoundedRectangle (waveBox.reduced (0.5f), 2.0f, 1.0f);

    const bool reversed = pReverse->load() > 0.5f;
    const float trimStart = juce::jlimit (0.0f, 0.99f, pTrimStart->load());
    const float trimEnd = juce::jlimit (trimStart + 0.005f, 1.0f, pTrimEnd->load());

    // (The sample name row is the real SampleBrowserComponent, positioned
    // inside the box header by SampleGroupComponent::resized().)

    const auto preview = processor.getWaveformPreview (layer);
    const float totalDuration = preview.loaded ? (float) preview.durationMs : 1000.0f;
    const auto geometry = getGeometry (totalDuration);
    const auto plot = geometry.plot;
    const float vpSpan = 1.0f / juce::jmax (1.0f, zoomFactor);

    if (plot.getWidth() >= 24.0f && plot.getHeight() >= 24.0f)
    {
        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());

        // SVG "Mini Baseline": solid #363240 (blue bank) / #3c314d (purple)
        const float centre = plot.getCentreY();
        g.setColour (layer < 3 ? juce::Colour (0xff36384d) : juce::Colour (0xff3c314d));
        g.drawLine (plot.getX(), centre, plot.getRight(), centre, 0.8f);

        if (preview.loaded)
        {
            // Filled waveform envelope + stroke trace (mini-blue/purple-grad)
            const float amplitude = plot.getHeight() * 0.44f;
            const int cols = juce::jmax (1, (int) std::floor (plot.getWidth()));
            juce::Path fillPath;
            fillPath.startNewSubPath (plot.getX(), centre);
            for (int col = 0; col < cols; ++col)
                fillPath.lineTo (plot.getX() + (float) col + 0.5f, centre - columnPeak (preview, reversed, viewStartNorm, vpSpan, col, cols, true) * amplitude);
            for (int col = cols - 1; col >= 0; --col)
                fillPath.lineTo (plot.getX() + (float) col + 0.5f, centre - columnPeak (preview, reversed, viewStartNorm, vpSpan, col, cols, false) * amplitude);
            fillPath.closeSubPath();
            g.setColour (flCurveColour.withAlpha (layer < 3 ? 0.35f : 0.30f));
            g.fillPath (fillPath);

            g.setColour (flCurveColour);
            for (int col = 0; col < cols; ++col)
            {
                const float maxV = columnPeak (preview, reversed, viewStartNorm, vpSpan, col, cols, true);
                const float minV = columnPeak (preview, reversed, viewStartNorm, vpSpan, col, cols, false);
                const float x = plot.getX() + (float) col + 0.5f;
                const float top = centre - maxV * amplitude;
                const float bottom = juce::jmax (top + 1.0f, centre - minV * amplitude);
                g.drawVerticalLine (juce::roundToInt (x), top, bottom);
            }
        }
        else
        {
            g.setColour (ui::kSecondaryText);
            g.setFont (juce::Font (11.0f));
            g.drawText ("Drop audio file here or click sample browser above", plot.toNearestInt(),
                        juce::Justification::centred);
        }

        // Dim trimmed-out regions
        g.setColour (snap::inset.withAlpha (0.75f));
        const float leftDimEnd = juce::jmin (plot.getRight(), geometry.trimX0);
        if (leftDimEnd > plot.getX())
            g.fillRect (plot.getX(), plot.getY(), leftDimEnd - plot.getX(), plot.getHeight());
        const float rightDimStart = juce::jmax (plot.getX(), geometry.trimX1);
        if (rightDimStart < plot.getRight())
            g.fillRect (rightDimStart, plot.getY(), plot.getRight() - rightDimStart, plot.getHeight());

        // FL envelope curve: bank-accent stroke, translucent fill under it.
        const auto layerParams = readLayerParameters();
        const auto sched = LayeredSampleEngine::computeEnvelopeSchedule (layerParams, geometry.trimmedDurationMs);

        auto xAt = [&] (float timeMs)
        {
            return geometry.trimX0 + geometry.trimW * juce::jlimit (0.0f, geometry.trimmedDurationMs, timeMs) / geometry.trimmedDurationMs;
        };
        const float envTop = plot.getY() + 7.0f;
        const float envBot = plot.getBottom() - 15.0f;
        const float envH = juce::jmax (12.0f, envBot - envTop);
        auto yAt = [&] (float level)
        {
            return envBot - envH * juce::jlimit (0.0f, 1.0f, level);
        };

        // Build the FL envelope from the engine's real curve evaluator so
        // Hold / Smooth / Single curve / Half sine / Stairs / Pulse / Wave
        // all render exactly as they sound.
        juce::Path envelope;
        envelope.startNewSubPath (xAt (0.0f), yAt (LayeredSampleEngine::evaluateCompleteEnvelopeAtMs (layerParams, sched, 0.0f)));
        const int totalSteps = kCurveSteps * 3;
        for (int i = 1; i <= totalSteps; ++i)
        {
            const float t = (float) i / (float) totalSteps;
            const float timeMs = t * geometry.trimmedDurationMs;
            envelope.lineTo (xAt (timeMs), yAt (LayeredSampleEngine::evaluateCompleteEnvelopeAtMs (layerParams, sched, timeMs)));
        }

        if (envEnabled)
        {
            auto fill = envelope;
            fill.lineTo (geometry.trimX1, envBot);
            fill.lineTo (geometry.trimX0, envBot);
            fill.closeSubPath();
            g.setColour (snap::bankAccent (layer).withAlpha (0.12f));
            g.fillPath (fill);
        }

        g.setColour (envEnabled ? snap::bankAccent (layer) : ui::kDisabledText);
        g.strokePath (envelope, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

        // FL-style handles (always visible, envelope on or off):
        //  - ADSR points: gold filled circles with dark ring
        //  - Tension handles: small hollow circles in the bank accent
        auto drawFlPoint = [&] (juce::Point<float> point)
        {
            if (point.x < plot.getX() - 12.0f || point.x > plot.getRight() + 12.0f)
                return;
            g.setColour (snap::flOutline);
            g.fillEllipse (point.x - kHandleRadius - 1.0f, point.y - kHandleRadius - 1.0f,
                           2.0f * (kHandleRadius + 1.0f), 2.0f * (kHandleRadius + 1.0f));
            g.setColour (snap::flPoint);
            g.fillEllipse (point.x - kHandleRadius, point.y - kHandleRadius,
                           2.0f * kHandleRadius, 2.0f * kHandleRadius);
        };
        auto drawTensionPoint = [&] (juce::Point<float> point, bool visible)
        {
            if (! visible || point.x < plot.getX() - 8.0f || point.x > plot.getRight() + 8.0f)
                return;
            g.setColour (snap::bankAccent (layer).brighter (0.35f));
            g.drawEllipse (point.x - kTensionRadius, point.y - kTensionRadius,
                           2.0f * kTensionRadius, 2.0f * kTensionRadius, 1.3f);
        };

        drawFlPoint (geometry.attack);
        drawFlPoint (geometry.hold);
        drawFlPoint (geometry.decay);
        drawFlPoint (geometry.sustain);
        drawFlPoint (geometry.release);
        drawTensionPoint (geometry.tensionAttack, geometry.tensionAttackVisible);
        drawTensionPoint (geometry.tensionDecay, geometry.tensionDecayVisible);
        drawTensionPoint (geometry.tensionRelease, geometry.tensionReleaseVisible);

        // Trim lines in the bank accent
        g.setColour (snap::bankAccent (layer).withAlpha (0.9f));
        g.drawLine (geometry.trimX0, plot.getY(), geometry.trimX0, plot.getBottom(), 1.4f);
        g.drawLine (geometry.trimX1, plot.getY(), geometry.trimX1, plot.getBottom(), 1.4f);

        g.restoreState();
    }

    // Bottom "Attack x% / Decay y% / Pitch z" readout row (SVG y=542)
    const auto bottomReadout = svg (16, 528, 226, 18);
    const auto labelFont = juce::Font (juce::jlimit (8.5f, 10.5f, 10.5f));

    pitchReadoutBounds = svg (208.0f, 528.0f, 32.0f, 18.0f).toNearestInt();
    const auto attackPct = juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f,
                    pAttack->load() / juce::jmax (1.0f, pAttack->load() + pHold->load() + pDecay->load())) * 100.0f));
    const auto decayPct = juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f,
                    pDecay->load() / juce::jmax (1.0f, pAttack->load() + pHold->load() + pDecay->load())) * 100.0f));
    const auto pitchTxt = pitchSlider.getTextFromValue (pitchSlider.getValue());

    // SVG layout: Attack @26/63, Decay @103/139, Pitch @179/208
    const auto cell = [&] (float x, float w)
    {
        return bottomReadout.toFloat().withX ((16.0f + x) * sx).withWidth (w * sx);
    };
    g.setFont (labelFont);
    g.setColour (ui::kSecondaryText);
    g.drawText ("Attack", cell (10.0f, 40.0f), juce::Justification::centredLeft, true);
    g.drawText ("Decay", cell (87.0f, 40.0f), juce::Justification::centredLeft, true);
    g.drawText ("Pitch", cell (162.0f, 30.0f), juce::Justification::centredLeft, true);
    g.setColour (ui::kMainText);
    g.drawText (attackPct + "%", cell (47.0f, 40.0f), juce::Justification::centredLeft, true);
    g.drawText (decayPct + "%", cell (123.0f, 40.0f), juce::Justification::centredLeft, true);
    g.drawText (pitchTxt, cell (193.0f, 32.0f), juce::Justification::centredLeft, true);

    // ---- SVG "Volume" fader card (59x76, #17151d): centred label on top,
    // vertical track 421..457, thumb r3, centred value at 470 ----
    if (volCardBounds.getWidth() > 10.0f)
    {
        const auto card = volCardBounds;
        g.setColour (ui::kControlBg);
        g.fillRoundedRectangle (card, 2.0f);

        const float unit = card.getHeight() / 76.0f; // one SVG unit
        g.setColour (ui::kSecondaryText);
        g.setFont (juce::Font (juce::jmax (9.5f, 10.5f * unit)));
        g.drawText ("Volume",
                    juce::Rectangle<float> (card.getX(), card.getY() + 6.0f * unit,
                                            card.getWidth(), 13.0f * unit),
                    juce::Justification::centred);
        g.setColour (ui::kMainText);
        g.setFont (juce::Font (juce::jmax (10.5f, 11.0f * unit)));
        g.drawText (gainSlider.getTextFromValue (gainSlider.getValue()),
                    juce::Rectangle<float> (card.getX(), card.getY() + 60.0f * unit,
                                            card.getWidth(), 14.0f * unit),
                    juce::Justification::centred);
    }

    // ---- SVG trim card (134x32, #17151d): Str/Bk labels at y532, track
    // x 262..372 (12 SVG units inset), y 543, accent span + r3 thumbs ----
    if (trimCardBounds.getWidth() > 10.0f)
    {
        auto card = trimCardBounds;
        const float ux = card.getWidth() / 134.0f; // one SVG x unit
        const float uy = card.getHeight() / 32.0f; // one SVG y unit
        g.setColour (ui::kControlBg);
        g.fillRoundedRectangle (card, 2.0f);

        g.setFont (juce::Font (juce::jmax (9.0f, 10.0f * uy)));
        g.setColour (ui::kSecondaryText);
        g.drawText ("Str Trim", card.getX() + 8.0f * ux, card.getY() + 6.0f * uy,
                    50.0f * ux, 12.0f * uy, juce::Justification::centredLeft, false);
        g.drawText ("Bk Trim", card.getRight() - 58.0f * ux, card.getY() + 6.0f * uy,
                    50.0f * ux, 12.0f * uy, juce::Justification::centredRight, false);

        const float trackY = card.getY() + 23.0f * uy;
        const float trackX0 = card.getX() + 12.0f * ux;
        const float trackX1 = card.getRight() - 12.0f * ux;

        g.setColour (snap::tileTrack);
        g.drawLine (trackX0, trackY, trackX1, trackY, 2.2f);

        // Accent active span, inset 2 SVG units from the track ends so a
        // full trim still shows the track tips (SVG x 264..370 of 262..372)
        const float spanX0 = juce::jmax (trackX0, trackX0 + 2.0f * ux
                                         + trimStart * (trackX1 - trackX0 - 4.0f * ux));
        const float spanX1 = juce::jmin (trackX1, trackX1 - 2.0f * ux
                                         - (1.0f - trimEnd) * (trackX1 - trackX0 - 4.0f * ux));
        g.setColour (snap::bankAccent (layer));
        if (spanX1 > spanX0)
            g.drawLine (spanX0, trackY, spanX1, trackY, 2.2f);

        // #e6e4ed r=3 thumbs sit on the accent span ends
        g.setColour (snap::thumb);
        g.fillEllipse (spanX0 - 3.0f, trackY - 3.0f, 6.0f, 6.0f);
        g.fillEllipse (spanX1 - 3.0f, trackY - 3.0f, 6.0f, 6.0f);
    }
}

void LayerEnvelopeComponent::resized()
{
    // All geometry comes from the colour SVG's 399x186 panel coordinates.
    // The editor occupies the panel area below the 28px header (y 400..558
    // in SVG space), so the editor is 158 units tall and its local origin
    // maps to SVG (0, 400).
    auto full = getLocalBounds();
    const float sx = (float) full.getWidth() / 399.0f;
    const float sy = (float) full.getHeight() / 158.0f;
    auto at = [sx, sy] (float svgX, float svgY, float svgW, float svgH)
    {
        return juce::Rectangle<int> (juce::roundToInt (svgX * sx),
                                     juce::roundToInt ((svgY - 400.0f) * sy),
                                     juce::roundToInt (svgW * sx),
                                     juce::roundToInt (svgH * sy));
    };

    // ---- sample box content (box x 16..242, y 400..552) ----
    sampleHeaderBounds = at (16, 400, 226, 20);
    auto headerBar = sampleHeaderBounds.reduced (3, 1);
    // REV sits next to ENV; the phase invert icon lives on the Shift card
    // per the SVG. Both are 30px text pills with a ~12px gap between them.
    reverseBtn.setBounds (headerBar.removeFromRight (30));
    headerBar.removeFromRight (12);
    envEnableBtn.setBounds (headerBar.removeFromRight (30));

    // ---- SVG card grid (x 250..384) ----
    delaySlider.setBounds (at (250, 400, 69, 36));   // Shift  (+ phase icon)
    widthSlider.setBounds (at (250, 440, 69, 36));   // Width
    panSlider.setBounds   (at (250, 480, 69, 36));   // Pan

    // SVG phase icon: circle cx308 cy410 r3.5 -> 18x14 button on the card.
    // The tile sliders were added after it, so bring it in front of the
    // Shift card fill.
    phaseBtn.setBounds (at (299, 403, 18, 14));
    phaseBtn.toFront (false);

    // Volume fader: label row (413) + track (421..457) + value (470) inside
    // the 59x76 card; positions scale with the SVG unit mapping.
    volCardBounds = at (325, 400, 59, 76).toFloat();
    gainCardBounds = juce::Rectangle<float> (volCardBounds.getX(),
                                             volCardBounds.getY() + 13.0f * sy,
                                             (float) volCardBounds.getWidth(), 14.0f);
    const int trackTop = volCardBounds.getY() + juce::roundToInt (21.0f * sy);
    const int trackBottom = volCardBounds.getY() + juce::roundToInt (57.0f * sy);
    const int trackX = volCardBounds.getCentreX() - 9;
    gainSlider.setBounds (trackX, trackTop, 18, juce::jmax (10, trackBottom - trackTop));

    pogoSlider.setBounds (at (325, 480, 59, 36));   // Pogo

    // Dual-handle trim card 134x32 at the bottom of the grid
    trimCardBounds = at (250, 520, 134, 32).toFloat();
    // Hidden standalone trim sliders cover the same span so drags on the
    // card hit real Slider components.
    trimStartSlider.setBounds (trimCardBounds.withWidth (trimCardBounds.getWidth() * 0.5f).toNearestInt());
    trimEndSlider.setBounds (trimCardBounds.withTrimmedLeft (trimCardBounds.getWidth() * 0.5f).toNearestInt());

    // Hidden controls keep their attachments alive; park them off-canvas.
    auto park = [&] (juce::Component& c) { c.setBounds (-40, -40, 20, 20); };
    park (pitchSlider);
    park (fadeInSlider);
    park (fadeOutSlider);
    park (hpSlider);
    park (lpSlider);
    park (attackCurveBox);
    park (decayCurveBox);
    park (releaseCurveBox);
    park (focusStartBtn);
    park (focusEndBtn);
    park (zoomFitBtn);
    park (zoomOutBtn);
    park (zoomResetBtn);
    park (zoomInBtn);

    overviewBarBounds = juce::Rectangle<float>();
    controlStripBounds = full.toFloat();
}

LayerEnvelopeComponent::Handle LayerEnvelopeComponent::hitTestHandle (juce::Point<float> pos,
                                                                      const Geometry& g,
                                                                      float maxRadius) const noexcept
{
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
        const bool startVisible = g.trimX0 >= g.plot.getX() - 10.0f
                               && g.trimX0 <= g.plot.getRight() + 10.0f;
        const bool endVisible = g.trimX1 >= g.plot.getX() - 10.0f
                             && g.trimX1 <= g.plot.getRight() + 10.0f;
        const float distStart = startVisible ? std::abs (pos.x - g.trimX0) : 1.0e6f;
        const float distEnd   = endVisible ? std::abs (pos.x - g.trimX1) : 1.0e6f;
        if (distStart <= 12.0f && distStart <= distEnd)
            best = Handle::TrimStart;
        else if (distEnd <= 12.0f)
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
            outTitle = "Point 1/4 (Attack)";
            return;
        case Handle::TensionAttack:
            outSegment = Segment::Attack;
            outCirclePos = g.tensionAttack;
            outTitle = "Attack segment";
            return;
        case Handle::Hold:
            outSegment = Segment::Decay;
            outCirclePos = g.hold;
            outTitle = "Point 2/4 (Decay start)";
            return;
        case Handle::Decay:
            outSegment = Segment::Decay;
            outCirclePos = g.decay;
            outTitle = "Point 3/4 (Sustain)";
            return;
        case Handle::Sustain:
            outSegment = Segment::Decay;
            outCirclePos = g.sustain;
            outTitle = "Point 3/4 (Sustain)";
            return;
        case Handle::TensionDecay:
            outSegment = Segment::Decay;
            outCirclePos = g.tensionDecay;
            outTitle = "Decay segment";
            return;
        case Handle::Release:
            outSegment = Segment::Release;
            outCirclePos = g.release;
            outTitle = "Point 4/4 (Release)";
            return;
        case Handle::TensionRelease:
            outSegment = Segment::Release;
            outCirclePos = g.tensionRelease;
            outTitle = "Release segment";
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
    const float currentTension = segment == Segment::Attack ? pAttackTension->load()
        : (segment == Segment::Decay ? pDecayTension->load() : pReleaseTension->load());
    const bool envOn = pEnvEnabled->load() > 0.5f;

    // FL Studio "Point n/m" right-click menu: Mode radio list with the 8
    // curve types, a tension reset and the ADSR section entries.
    juce::PopupMenu menu;
    menu.addSectionHeader (title);

    static const char* modes[] = { "Hold", "Smooth", "Single curve", "Half sine",
                                   "Stairs", "Smooth stairs", "Pulse", "Wave" };
    juce::PopupMenu modeMenu;
    for (int i = 0; i < 8; ++i)
        modeMenu.addItem (100 + i, modes[i], true, currentMode == i);
    menu.addSubMenu ("Mode", modeMenu);

    menu.addSeparator();
    menu.addItem (200, "Reset tension (0%)", true, std::abs (currentTension) < 0.005f);

    juce::PopupMenu adsrMenu;
    adsrMenu.addItem (201, "Enabled", true, envOn);
    adsrMenu.addSeparator();
    adsrMenu.addItem (202, "Reset segment times");
    adsrMenu.addItem (203, "Reset all envelope points");
    menu.addSubMenu ("ADSR section", adsrMenu);

    const auto screenCirclePt = localPointToGlobal (circleLocalPos.roundToInt());
    const juce::Rectangle<int> anchorRect (screenCirclePt.x - 6, screenCirclePt.y - 6, 12, 12);

    auto options = juce::PopupMenu::Options()
                       .withTargetScreenArea (anchorRect)
                       .withStandardItemHeight (20);
    if (auto* top = getTopLevelComponent())
        options = options.withParentComponent (top);

    juce::Component::SafePointer<LayerEnvelopeComponent> safeThis (this);
    menu.showMenuAsync (options, [safeThis, curveId, tensionId, segment, envOn] (int result)
    {
        if (safeThis == nullptr)
            return;
        if (result >= 100 && result < 108)
            safeThis->setParameter (curveId, (float) (result - 100));
        else if (result == 200)
            safeThis->setParameter (tensionId, 0.0f);
        else if (result == 201)
            safeThis->setParameter (ids::layerEnvEnabled (safeThis->layer), envOn ? 0.0f : 1.0f);
        else if (result == 202)
        {
            // Reset just this segment's timing to the FL defaults
            if (segment == Segment::Attack)
            {
                safeThis->setParameter (ids::layerAttack (safeThis->layer), 210.0f);
                safeThis->setParameter (ids::layerAttackTension (safeThis->layer), 0.35f);
                safeThis->setParameter (ids::layerAttackCurve (safeThis->layer), 2.0f);
            }
            else if (segment == Segment::Decay)
            {
                safeThis->setParameter (ids::layerDecay (safeThis->layer), 310.0f);
                safeThis->setParameter (ids::layerSustain (safeThis->layer), 0.76f);
                safeThis->setParameter (ids::layerDecayTension (safeThis->layer), -0.35f);
                safeThis->setParameter (ids::layerDecayCurve (safeThis->layer), 2.0f);
            }
            else
            {
                safeThis->setParameter (ids::layerRelease (safeThis->layer), 480.0f);
                safeThis->setParameter (ids::layerReleaseTension (safeThis->layer), -0.45f);
                safeThis->setParameter (ids::layerReleaseCurve (safeThis->layer), 2.0f);
            }
        }
        else if (result == 203)
        {
            safeThis->setParameter (ids::layerAttack (safeThis->layer), 210.0f);
            safeThis->setParameter (ids::layerHold (safeThis->layer), 0.0f);
            safeThis->setParameter (ids::layerDecay (safeThis->layer), 310.0f);
            safeThis->setParameter (ids::layerSustain (safeThis->layer), 0.76f);
            safeThis->setParameter (ids::layerRelease (safeThis->layer), 480.0f);
            safeThis->setParameter (ids::layerAttackTension (safeThis->layer), 0.35f);
            safeThis->setParameter (ids::layerDecayTension (safeThis->layer), -0.35f);
            safeThis->setParameter (ids::layerReleaseTension (safeThis->layer), -0.45f);
            safeThis->setParameter (ids::layerAttackCurve (safeThis->layer), 2.0f);
            safeThis->setParameter (ids::layerDecayCurve (safeThis->layer), 2.0f);
            safeThis->setParameter (ids::layerReleaseCurve (safeThis->layer), 2.0f);
        }
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
        case Handle::Pitch:           parameterId = ids::layerPitch (layer); break;
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
        case Handle::Pitch:           parameterId = ids::layerPitch (layer); break;
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
    draggingTrimCard = false;
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

    if (activeHandle == Handle::Pitch)
    {
        const float delta = (position.x - dragStartX) * 0.5f;
        setParameter (ids::layerPitch (layer), juce::jlimit (-24.0f, 24.0f, dragStartPitch + delta));
        repaint();
        return;
    }

    if (activeHandle == Handle::TensionAttack
        || activeHandle == Handle::TensionDecay
        || activeHandle == Handle::TensionRelease)
    {
        const float delta = (dragStartY - position.y) / 85.0f;
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

    // Dragging on the dual-handle trim card updates the matching trim end.
    if (activeHandle == Handle::TrimStart)
    {
        if (draggingTrimCard)
        {
            const float ux = trimCardBounds.getWidth() / 134.0f;
            const float trackX0 = trimCardBounds.getX() + 14.0f * ux;
            const float trackX1 = trimCardBounds.getRight() - 14.0f * ux;
            const float norm = juce::jlimit (0.0f, juce::jmax (0.0f, pTrimEnd->load() - 0.005f),
                                             (position.x - trackX0) / juce::jmax (1.0f, trackX1 - trackX0));
            setParameter (ids::layerTrimStart (layer), norm);
            repaint();
            return;
        }
        const float rawNorm = plotXToNorm (position.x, geometry.plot);
        const float norm = juce::jlimit (0.0f, juce::jmax (0.0f, pTrimEnd->load() - 0.005f), rawNorm);
        setParameter (ids::layerTrimStart (layer), norm);
        repaint();
        return;
    }
    if (activeHandle == Handle::TrimEnd)
    {
        if (draggingTrimCard)
        {
            const float ux = trimCardBounds.getWidth() / 134.0f;
            const float trackX0 = trimCardBounds.getX() + 14.0f * ux;
            const float trackX1 = trimCardBounds.getRight() - 14.0f * ux;
            const float norm = juce::jlimit (juce::jmin (1.0f, pTrimStart->load() + 0.005f), 1.0f,
                                             (position.x - trackX0) / juce::jmax (1.0f, trackX1 - trackX0));
            setParameter (ids::layerTrimEnd (layer), norm);
            repaint();
            return;
        }
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
            const float envBot = geometry.plot.getBottom() - 15.0f;
            const float envH = juce::jmax (12.0f, envBot - envTop);
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
    draggingTrimCard = false;

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

    // The compact Pitch readout doubles as a drag control, keeping the SVG
    // card layout while retaining direct access to the per-layer pitch value.
    if (pitchReadoutBounds.contains (event.position))
    {
        dragStartX = event.position.x;
        dragStartPitch = pPitch->load();
        beginGesture (Handle::Pitch);
        return;
    }

    // Dual-handle trim card: nearest handle wins, using the same inset span
    // as the painted trim track.
    if (trimCardBounds.contains (event.position))
    {
        draggingTrimCard = true;
        const float ux = trimCardBounds.getWidth() / 134.0f;
        const float trackX0 = trimCardBounds.getX() + 14.0f * ux;
        const float trackX1 = trimCardBounds.getRight() - 14.0f * ux;
        const float startX = trackX0 + pTrimStart->load() * (trackX1 - trackX0);
        const float backX = trackX0 + pTrimEnd->load() * (trackX1 - trackX0);
        const Handle h = (std::abs (event.position.x - startX) <= std::abs (event.position.x - backX))
            ? Handle::TrimStart : Handle::TrimEnd;
        beginGesture (h);
        applyDrag (event.position);
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
    if (pitchReadoutBounds.contains (event.position))
    {
        setParameter (ids::layerPitch (layer), 0.0f);
        repaint();
        return;
    }
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