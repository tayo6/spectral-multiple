#include "LayerEnvelopeComponent.h"

#include <cmath>

namespace
{
    constexpr float kHandleRadius = 4.6f;
    constexpr float kTensionRadius = 4.6f;
    constexpr int kCurveSteps = 96;
    constexpr int kFlBarHeight = 26;
    constexpr int kControlStripHeight = 102;
    constexpr int kHeaderHeight = 22;
    constexpr float kRulerHeight = 16.0f;
    constexpr float kOverviewHeight = 14.0f;
    constexpr float kMaxWaveformZoom = 32.0f;

    juce::String formatEnvelopeTime (float value)
    {
        if (value >= 1000.0f)
            return juce::String (value / 1000.0f, 2) + "s";
        return juce::String (value, value < 10.0f ? 1 : 0) + "ms";
    }

    juce::String formatTrimPercent (float norm)
    {
        const float pct = norm * 100.0f;
        if (std::abs (pct - std::round (pct)) < 0.05f)
            return juce::String (juce::roundToInt (pct)) + "%";
        return juce::String (pct, 1) + "%";
    }
}

LayerEnvelopeComponent::LayerEnvelopeComponent (WaveLensProcessor& p,
                                               int layerIndex,
                                               juce::Colour traceColour)
    : processor (p), layer (juce::jlimit (0, WaveLensProcessor::kMaxSignals - 1, layerIndex)),
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
    pTrimStart = state.getRawParameterValue (ids::layerTrimStart (layer));
    pTrimEnd = state.getRawParameterValue (ids::layerTrimEnd (layer));
    pFadeIn = state.getRawParameterValue (ids::layerFadeIn (layer));
    pFadeOut = state.getRawParameterValue (ids::layerFadeOut (layer));

    const juce::Colour flCurveColour (0xff57d6ff);

    envEnableBtn.setButtonText ("ENV");
    envEnableBtn.setClickingTogglesState (true);
    envEnableBtn.setTooltip ("Enable or bypass FL envelope");
    envEnableBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff222831));
    envEnableBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1e3240));
    envEnableBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff95a4b8));
    envEnableBtn.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible (envEnableBtn);
    envEnableAttachment = std::make_unique<ButtonAtt> (state, ids::layerEnvEnabled (layer), envEnableBtn);

    reverseBtn.setButtonText ("REV");
    reverseBtn.setClickingTogglesState (true);
    reverseBtn.setTooltip ("Reverse sample playback");
    reverseBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232230));
    reverseBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff7b00));
    reverseBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffc9cce2));
    reverseBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff111111));
    addAndMakeVisible (reverseBtn);
    reverseAttachment = std::make_unique<ButtonAtt> (state, ids::layerReverse (layer), reverseBtn);

    const juce::StringArray flCurveNames { "Hold", "Smooth", "Single curve", "Half sine", "Stairs", "Smooth stairs", "Pulse", "Wave" };

    auto prepareCurveBox = [&] (juce::ComboBox& box, const juce::String& tooltip)
    {
        box.addItemList (flCurveNames, 1);
        box.setTooltip (tooltip);
        box.setJustificationType (juce::Justification::centredLeft);
        box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1b1a26));
        box.setColour (juce::ComboBox::textColourId, juce::Colour (0xffe5e8f5));
        box.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff312f42));
        box.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff95a4b8));
        addAndMakeVisible (box);
    };

    prepareCurveBox (attackCurveBox, "Attack segment FL curve mode");
    prepareCurveBox (decayCurveBox, "Decay segment FL curve mode");
    prepareCurveBox (releaseCurveBox, "Release segment FL curve mode");

    attackCurveAttachment = std::make_unique<ComboAtt> (state, ids::layerAttackCurve (layer), attackCurveBox);
    decayCurveAttachment = std::make_unique<ComboAtt> (state, ids::layerDecayCurve (layer), decayCurveBox);
    releaseCurveAttachment = std::make_unique<ComboAtt> (state, ids::layerReleaseCurve (layer), releaseCurveBox);

    auto prepareRotary = [&] (juce::Slider& slider, const juce::String& tooltip, juce::Colour fill)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 48, 12);
        slider.setColour (juce::Slider::rotarySliderFillColourId, fill);
        slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff2d2b3b));
        slider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffeceeff));
        slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffd6d9ef));
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setTooltip (tooltip);
        addAndMakeVisible (slider);
    };

    const juce::Colour orangeAccent (0xffff7b00);
    prepareRotary (gainSlider, "Layer gain (0% to 200%)", colour);
    prepareRotary (pitchSlider, "Layer pitch: -24 to +24 semitones", colour);
    prepareRotary (pogoSlider, "FL Sampler style pitch-bend envelope (-100% to +100%)", orangeAccent);
    prepareRotary (trimStartSlider, "Sample front trim (0% to 98%)", orangeAccent);
    prepareRotary (trimEndSlider, "Sample back trim (2% to 100%)", orangeAccent);
    prepareRotary (fadeInSlider, "Sample fade-in duration (0 ms to 2000 ms)", colour);
    prepareRotary (fadeOutSlider, "Sample fade-out duration (0 ms to 5000 ms)", colour);
    prepareRotary (delaySlider, "Timing shift (-100 ms to +100 ms)", colour);
    prepareRotary (panSlider, "Layer pan: left / centre / right", colour);
    prepareRotary (highPassSlider, "Layer high-pass filter: 20 Hz to 1 kHz", colour);
    prepareRotary (lowPassSlider, "Layer low-pass filter: 1 kHz to 20 kHz", colour);

    gainAttachment = std::make_unique<SliderAtt> (state, ids::layerGain (layer), gainSlider);
    pitchAttachment = std::make_unique<SliderAtt> (state, ids::layerPitch (layer), pitchSlider);
    pogoAttachment = std::make_unique<SliderAtt> (state, ids::layerPogo (layer), pogoSlider);
    trimStartAttachment = std::make_unique<SliderAtt> (state, ids::layerTrimStart (layer), trimStartSlider);
    trimEndAttachment = std::make_unique<SliderAtt> (state, ids::layerTrimEnd (layer), trimEndSlider);
    fadeInAttachment = std::make_unique<SliderAtt> (state, ids::layerFadeIn (layer), fadeInSlider);
    fadeOutAttachment = std::make_unique<SliderAtt> (state, ids::layerFadeOut (layer), fadeOutSlider);
    delayAttachment = std::make_unique<SliderAtt> (state, ids::layerDelay (layer), delaySlider);
    panAttachment = std::make_unique<SliderAtt> (state, ids::layerPan (layer), panSlider);
    highPassAttachment = std::make_unique<SliderAtt> (state, ids::layerHighPass (layer), highPassSlider);
    lowPassAttachment = std::make_unique<SliderAtt> (state, ids::layerLowPass (layer), lowPassSlider);

    trimStartSlider.onValueChange = [this]
    {
        const float maxStart = juce::jmax (0.0f, (float) trimEndSlider.getValue() - 0.005f);
        if (trimStartSlider.getValue() > maxStart)
            trimStartSlider.setValue (maxStart, juce::sendNotificationSync);
    };
    trimEndSlider.onValueChange = [this]
    {
        const float minEnd = juce::jmin (1.0f, (float) trimStartSlider.getValue() + 0.005f);
        if (trimEndSlider.getValue() < minEnd)
            trimEndSlider.setValue (minEnd, juce::sendNotificationSync);
    };

    gainSlider.textFromValueFunction = [] (double v) { return juce::String (v * 100.0, 0) + "%"; };
    pitchSlider.textFromValueFunction = [] (double value)
    {
        return juce::String (value > 0.0 ? "+" : "") + juce::String (value, 1) + "st";
    };
    pogoSlider.textFromValueFunction = [] (double value)
    {
        return juce::String (value > 0.0 ? "+" : "") + juce::String (value * 100.0, 0) + "%";
    };
    trimStartSlider.textFromValueFunction = [] (double v) { return juce::String (v * 100.0, 0) + "%"; };
    trimEndSlider.textFromValueFunction = [] (double v) { return juce::String (v * 100.0, 0) + "%"; };
    fadeInSlider.textFromValueFunction = [] (double v) { return formatEnvelopeTime ((float) v); };
    fadeOutSlider.textFromValueFunction = [] (double v) { return formatEnvelopeTime ((float) v); };
    delaySlider.textFromValueFunction = [] (double v) { return juce::String (v > 0.0 ? "+" : "") + juce::String (v, 0) + "ms"; };
    panSlider.textFromValueFunction = [] (double value)
    {
        if (std::abs (value) < 0.005)
            return juce::String ("C");
        return juce::String (std::abs (value) * 100.0, 0) + (value < 0.0 ? "L" : "R");
    };
    auto hzText = [] (double value)
    {
        return value >= 1000.0 ? juce::String (value / 1000.0, 1) + "k"
                               : juce::String (value, 0);
    };
    highPassSlider.textFromValueFunction = hzText;
    lowPassSlider.textFromValueFunction = hzText;

    for (auto* s : { &gainSlider, &pitchSlider, &pogoSlider, &trimStartSlider, &trimEndSlider,
                     &fadeInSlider, &fadeOutSlider, &delaySlider, &panSlider, &highPassSlider, &lowPassSlider })
        s->updateText();

    auto prepareZoomBtn = [this] (juce::TextButton& b, const juce::String& text, const juce::String& tip,
                                  juce::Colour textCol = juce::Colour (0xffe7edf5))
    {
        b.setButtonText (text);
        b.setTooltip (tip);
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1e242d));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2c3440));
        b.setColour (juce::TextButton::textColourOffId, textCol);
        b.setColour (juce::TextButton::textColourOnId, textCol);
        addAndMakeVisible (b);
    };

    const juce::Colour orangeTxt (0xffff9f43);
    prepareZoomBtn (focusStartBtn, "|<", "Center zoom on Start (Trim F)", orangeTxt);
    prepareZoomBtn (focusEndBtn, ">|", "Center zoom on End (Trim B)", orangeTxt);
    prepareZoomBtn (zoomFitBtn, "Fit", "Zoom to fit Start-End trim range", juce::Colour (0xffd2dce8));
    prepareZoomBtn (zoomOutBtn, "-", "Zoom out waveform");
    prepareZoomBtn (zoomResetBtn, "1.0x", "Reset waveform zoom to 1.0x", juce::Colour (0xffaebacc));
    prepareZoomBtn (zoomInBtn, "+", "Zoom in waveform");

    focusStartBtn.onClick = [this]
    {
        focusViewportAt (pTrimStart->load(), 4.0f);
        resized();
        repaint();
    };
    focusEndBtn.onClick = [this]
    {
        focusViewportAt (pTrimEnd->load(), 4.0f);
        resized();
        repaint();
    };
    zoomFitBtn.onClick = [this]
    {
        zoomToFitTrim();
        resized();
        repaint();
    };
    zoomOutBtn.onClick = [this]
    {
        const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
        const float v0 = viewStartNorm;
        const float v1 = v0 + span;
        const float t0 = pTrimStart->load();
        const float t1 = pTrimEnd->load();
        const bool startIn = t0 >= v0 - 0.001f && t0 <= v1 + 0.001f;
        const bool endIn = t1 >= v0 - 0.001f && t1 <= v1 + 0.001f;
        float anchor = v0 + span * 0.5f;
        if (startIn && ! endIn) anchor = t0;
        else if (endIn && ! startIn) anchor = t1;
        zoomAroundNormalized (anchor, 1.0f / 1.6f);
        resized();
        repaint();
    };
    zoomResetBtn.onClick = [this]
    {
        clampViewport (0.0f, 1.0f);
        resized();
        repaint();
    };
    zoomInBtn.onClick = [this]
    {
        const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
        const float v0 = viewStartNorm;
        const float v1 = v0 + span;
        const float t0 = pTrimStart->load();
        const float t1 = pTrimEnd->load();
        const bool startIn = t0 >= v0 - 0.001f && t0 <= v1 + 0.001f;
        const bool endIn = t1 >= v0 - 0.001f && t1 <= v1 + 0.001f;
        float anchor = v0 + span * 0.5f;
        if (startIn && ! endIn) anchor = t0;
        else if (endIn && ! startIn) anchor = t1;
        zoomAroundNormalized (anchor, 1.6f);
        resized();
        repaint();
    };
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

float LayerEnvelopeComponent::applyPowerTension (float u, float tension) noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, u);
    const float ten = juce::jlimit (-1.0f, 1.0f, tension);
    if (std::abs (ten) < 1.0e-4f)
        return t;
    const float expVal = std::pow (2.0f, std::abs (ten) * 3.0f);
    return ten >= 0.0f ? (1.0f - std::pow (1.0f - t, expVal)) : std::pow (t, expVal);
}

float LayerEnvelopeComponent::flSegmentLevel (float progress, float y0, float y1, int mode, float tension) noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, progress);
    const float ten = juce::jlimit (-1.0f, 1.0f, tension);
    const int m = juce::jlimit (0, 7, mode);
    const float dirTen = (y1 >= y0) ? ten : -ten;

    float frac = t;
    switch (m)
    {
        case 0:
            frac = t >= 1.0f ? 1.0f : 0.0f;
            break;
        case 1:
        {
            const float s = applyPowerTension (t, dirTen);
            frac = s * s * (3.0f - 2.0f * s);
            break;
        }
        case 2:
            frac = applyPowerTension (t, dirTen);
            break;
        case 3:
        {
            const float hs = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t);
            frac = applyPowerTension (hs, dirTen);
            break;
        }
        case 4:
        {
            const float steps = juce::jmax (2.0f, std::round (2.0f + (ten + 1.0f) * 6.0f));
            frac = t >= 1.0f ? 1.0f : std::floor (t * steps) / steps;
            break;
        }
        case 5:
        {
            const float steps = juce::jmax (2.0f, std::round (2.0f + (ten + 1.0f) * 6.0f));
            if (t >= 1.0f)
                frac = 1.0f;
            else
            {
                const float scaled = t * steps;
                const float idx = std::floor (scaled);
                const float f = scaled - idx;
                const float smoothF = f * f * (3.0f - 2.0f * f);
                frac = (idx + smoothF) / steps;
            }
            break;
        }
        case 6:
        {
            const float cycles = juce::jmax (1.0f, std::round (1.0f + (ten + 1.0f) * 5.0f));
            if (t >= 1.0f)
                frac = 1.0f;
            else
                frac = std::fmod (t * cycles, 1.0f) < 0.5f ? 0.0f : 1.0f;
            break;
        }
        case 7:
        {
            const float cycles = juce::jmax (1.0f, std::round (1.0f + (ten + 1.0f) * 5.0f));
            frac = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi * (2.0f * cycles + 1.0f) * t));
            break;
        }
        default:
            frac = applyPowerTension (t, dirTen);
            break;
    }

    return y0 + (y1 - y0) * frac;
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
                            zoomed ? juce::Colour (0xffffb04a) : juce::Colour (0xffaebacc));
    zoomOutBtn.setEnabled (zoomFactor > 1.01f);
    zoomInBtn.setEnabled (zoomFactor < kMaxWaveformZoom - 0.01f);
}

LayerEnvelopeComponent::Geometry LayerEnvelopeComponent::getGeometry (float totalDurationMs) const
{
    Geometry result;
    const bool isZoomed = zoomFactor > 1.01f;
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromBottom ((float) (kControlStripHeight + kFlBarHeight + 10));
    bounds.removeFromTop ((float) kHeaderHeight);
    bounds.removeFromBottom (kRulerHeight + (isZoomed ? kOverviewHeight : 0.0f));
    result.plot = bounds;

    const float trimStart = juce::jlimit (0.0f, 0.99f, pTrimStart->load());
    const float trimEnd = juce::jlimit (trimStart + 0.005f, 1.0f, pTrimEnd->load());
    result.trimX0 = normToPlotX (trimStart, result.plot);
    result.trimX1 = normToPlotX (trimEnd, result.plot);
    result.trimW = juce::jmax (4.0f, result.trimX1 - result.trimX0);

    const float safeTotal = juce::jmax (1.0f, totalDurationMs);
    result.trimmedDurationMs = juce::jmax (1.0f, safeTotal * (trimEnd - trimStart));

    const float a = juce::jmax (0.1f, pAttack->load());
    const float h = juce::jmax (0.0f, pHold->load());
    const float d = juce::jmax (0.1f, pDecay->load());
    const float r = juce::jmax (0.1f, pRelease->load());
    const float sum = a + h + d + r;
    const float scale = sum > result.trimmedDurationMs ? (result.trimmedDurationMs / sum) : 1.0f;

    const float effA = a * scale;
    const float effH = h * scale;
    const float effD = d * scale;
    const float effR = r * scale;

    result.attackEndMs = effA;
    result.holdEndMs = effA + effH;
    result.decayEndMs = effA + effH + effD;
    result.releaseStartMs = juce::jmax (result.decayEndMs, result.trimmedDurationMs - effR);
    result.releaseMs = juce::jmax (0.1f, result.trimmedDurationMs - result.releaseStartMs);

    const float sustain = juce::jlimit (0.0f, 1.0f, pSustain->load());
    const int attMode = juce::roundToInt (pAttackCurve->load());
    const int decMode = juce::roundToInt (pDecayCurve->load());
    const int relMode = juce::roundToInt (pReleaseCurve->load());
    const float attTen = pAttackTension->load();
    const float decTen = pDecayTension->load();
    const float relTen = pReleaseTension->load();

    auto xAt = [&] (float ms)
    {
        return result.trimX0 + result.trimW * juce::jlimit (0.0f, result.trimmedDurationMs, ms) / result.trimmedDurationMs;
    };
    result.envTop = result.plot.getY() + 8.0f;
    result.envBot = result.plot.getBottom() - 8.0f;
    result.envH = juce::jmax (12.0f, result.envBot - result.envTop);
    auto yAt = [&] (float level)
    {
        return result.envBot - result.envH * juce::jlimit (0.0f, 1.0f, level);
    };

    const float attSpan = juce::jmax (0.05f, result.attackEndMs);
    const float decSpan = juce::jmax (0.05f, result.decayEndMs - result.holdEndMs);
    auto levelAt = [&] (float ms)
    {
        if (ms < result.attackEndMs)
            return flSegmentLevel (ms / attSpan, 0.0f, 1.0f, attMode, attTen);
        if (ms < result.holdEndMs)
            return 1.0f;
        if (ms < result.decayEndMs)
            return flSegmentLevel ((ms - result.holdEndMs) / decSpan, 1.0f, sustain, decMode, decTen);
        return sustain;
    };

    result.origin = { xAt (0.0f), yAt (0.0f) };
    result.attack = { xAt (result.attackEndMs), yAt (1.0f) };
    result.hold = { xAt (result.holdEndMs), yAt (1.0f) };
    result.holdVisible = (result.holdEndMs - result.attackEndMs) > 1.5f;
    result.sustain = { xAt (result.decayEndMs), yAt (sustain) };
    result.decay = result.sustain;
    const float relStartLevel = levelAt (result.releaseStartMs);
    result.release = { xAt (result.releaseStartMs), yAt (relStartLevel) };
    result.releaseVisible = (result.releaseStartMs - result.decayEndMs) > 4.0f;
    result.endPt = { xAt (result.trimmedDurationMs), yAt (0.0f) };
    result.attackEndX = result.attack.x;
    result.holdEndX = result.hold.x;
    result.releaseStartX = result.release.x;

    const float attMidMs = result.attackEndMs * 0.5f;
    result.tensionAttack = {
        xAt (attMidMs),
        yAt (flSegmentLevel (0.5f, 0.0f, 1.0f, attMode, attTen))
    };
    result.tensionAttackVisible = (result.attack.x - result.trimX0) >= 12.0f;

    const float decMidMs = result.holdEndMs + (result.decayEndMs - result.holdEndMs) * 0.5f;
    result.tensionDecay = {
        xAt (decMidMs),
        yAt (flSegmentLevel (0.5f, 1.0f, sustain, decMode, decTen))
    };
    result.tensionDecayVisible = (result.sustain.x - result.hold.x) >= 12.0f;

    const float relMidMs = result.releaseStartMs + result.releaseMs * 0.5f;
    result.tensionRelease = {
        xAt (relMidMs),
        yAt (flSegmentLevel (0.5f, relStartLevel, 0.0f, relMode, relTen))
    };
    result.tensionReleaseVisible = (result.trimX1 - result.release.x) >= 12.0f;

    return result;
}

void LayerEnvelopeComponent::paint (juce::Graphics& g)
{
    auto waveBox = getLocalBounds().withTrimmedBottom (kControlStripHeight + kFlBarHeight + 10).toFloat();
    const bool isPre = layer < 3;
    const bool envEnabled = pEnvEnabled->load() > 0.5f;
    const juce::Colour bgTop (0xff36404d);
    const juce::Colour bgBot (0xff28313c);
    const juce::Colour simWave (colour);
    const juce::Colour flCurveColour (0xff57d6ff);
    const juce::Colour flControlDot (0xffdcf6ff);

    envEnableBtn.setButtonText (envEnabled ? "ENV ON" : "ENV");

    juce::ColourGradient grad (bgTop, 0.0f, waveBox.getY(), bgBot, 0.0f, waveBox.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (waveBox, 3.0f);
    g.setColour (juce::Colour (0xff2b2b2b));
    g.drawRoundedRectangle (waveBox.reduced (0.5f), 3.0f, 1.0f);

    const bool reversed = pReverse->load() > 0.5f;
    const float trimStart = juce::jlimit (0.0f, 0.99f, pTrimStart->load());
    const float trimEnd = juce::jlimit (trimStart + 0.005f, 1.0f, pTrimEnd->load());

    // Top readout strip (clipped left of zoom buttons)
    auto readoutBar = waveBox.removeFromTop ((float) kHeaderHeight);
    g.setColour (juce::Colour (0xdb0e1319));
    g.fillRect (readoutBar);
    g.setColour (juce::Colour (0xffe7edf5));
    g.setFont (juce::Font (10.0f));
    const float fadeInVal = pFadeIn->load();
    const float fadeOutVal = pFadeOut->load();
    const juce::String fadeStr = (fadeInVal > 0.5f || fadeOutVal > 0.5f)
        ? ("  |  FI " + formatEnvelopeTime (fadeInVal) + " FO " + formatEnvelopeTime (fadeOutVal))
        : juce::String();
    auto textRect = readoutBar.reduced (8.0f, 0.0f);
    textRect.setRight (juce::jmax (textRect.getX() + 40.0f, (float) focusStartBtn.getX() - 8.0f));
    g.drawText ("A " + formatEnvelopeTime (pAttack->load())
                + "  H " + formatEnvelopeTime (pHold->load())
                + "  D " + formatEnvelopeTime (pDecay->load())
                + "  S " + juce::String (pSustain->load() * 100.0f, 0) + "%"
                + "  R " + formatEnvelopeTime (pRelease->load())
                + "   |   Trim " + formatTrimPercent (trimStart) + "-" + formatTrimPercent (trimEnd)
                + fadeStr
                + (reversed ? "  |  REV" : "")
                + (! envEnabled ? "  |  ENV OFF" : ""),
                textRect, juce::Justification::centredLeft, true);

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

        if (preview.loaded)
        {
            g.setColour (juce::Colour (isPre ? 0x45202a4a : 0x42361c48));
            g.fillRect (plot);
        }

        // Crisp Image-Line fine 16x8 grid
        const int gridCols = juce::jlimit (16, 512, 16 * (1 << (int) std::floor (std::log2 (juce::jmax (1.0f, zoomFactor)))));
        const int firstGrid = juce::jmax (1, (int) std::ceil (viewStartNorm * (float) gridCols));
        const int lastGrid = juce::jmin (gridCols - 1, (int) std::floor (vpEnd * (float) gridCols));
        for (int gIdx = firstGrid; gIdx <= lastGrid; ++gIdx)
        {
            g.setColour ((gIdx % 4 == 0) ? juce::Colour (0x24dceafa) : juce::Colour (0x10cddef0));
            const float x = normToPlotX ((float) gIdx / (float) gridCols, plot);
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        }
        for (int r = 1; r < 8; ++r)
        {
            g.setColour (r == 4 ? juce::Colour (0x2bdceafa)
                                : (r % 2 == 0 ? juce::Colour (0x1acddef0) : juce::Colour (0x0dcddef0)));
            const float y = plot.getY() + plot.getHeight() * (float) r / 8.0f;
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        }

        if (preview.loaded)
        {
            const float centre = plot.getCentreY();
            const float amplitude = plot.getHeight() * 0.44f;
            g.setColour (simWave.withAlpha (0.85f));
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

        // Dim trimmed-out front and back regions inside visible plot bounds
        g.setColour (juce::Colour (0x9e080a0e));
        const float leftDimEnd = juce::jmin (plot.getRight(), geometry.trimX0);
        if (leftDimEnd > plot.getX())
            g.fillRect (plot.getX(), plot.getY(), leftDimEnd - plot.getX(), plot.getHeight());
        const float rightDimStart = juce::jmax (plot.getX(), geometry.trimX1);
        if (rightDimStart < plot.getRight())
            g.fillRect (rightDimStart, plot.getY(), plot.getRight() - rightDimStart, plot.getHeight());

        // Fade-in & Fade-out visual slopes inside trimmed bounds
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
            g.setColour (juce::Colours::black.withAlpha (0.34f));
            g.fillPath (pFade);
            g.setColour (juce::Colour (0xffffd264).withAlpha (0.85f));
            g.drawLine (geometry.trimX0, plot.getBottom(), geometry.trimX0 + fadeInPx, plot.getY(), 1.25f);
        }
        if (fadeOutPx > 1.0f)
        {
            juce::Path pFade;
            pFade.startNewSubPath (geometry.trimX1, plot.getY());
            pFade.lineTo (geometry.trimX1 - fadeOutPx, plot.getY());
            pFade.lineTo (geometry.trimX1, plot.getBottom());
            pFade.closeSubPath();
            g.setColour (juce::Colours::black.withAlpha (0.34f));
            g.fillPath (pFade);
            g.setColour (juce::Colour (0xffffd264).withAlpha (0.85f));
            g.drawLine (geometry.trimX1 - fadeOutPx, plot.getY(), geometry.trimX1, plot.getBottom(), 1.25f);
        }

        const float sustain = pSustain->load();
        const int attackCurve = juce::roundToInt (pAttackCurve->load());
        const int decayCurve = juce::roundToInt (pDecayCurve->load());
        const int releaseCurve = juce::roundToInt (pReleaseCurve->load());
        const float attackTen = pAttackTension->load();
        const float decayTen = pDecayTension->load();
        const float releaseTen = pReleaseTension->load();

        const float attSpan = juce::jmax (0.05f, geometry.attackEndMs);
        const float decSpan = juce::jmax (0.05f, geometry.decayEndMs - geometry.holdEndMs);
        auto envelopeLevel = [&] (float timeMs)
        {
            if (timeMs < geometry.attackEndMs)
                return flSegmentLevel (timeMs / attSpan, 0.0f, 1.0f, attackCurve, attackTen);
            if (timeMs < geometry.holdEndMs)
                return 1.0f;
            if (timeMs < geometry.decayEndMs)
                return flSegmentLevel ((timeMs - geometry.holdEndMs) / decSpan, 1.0f, sustain, decayCurve, decayTen);
            return sustain;
        };
        auto xAt = [&] (float timeMs)
        {
            return geometry.trimX0 + geometry.trimW * juce::jlimit (0.0f, geometry.trimmedDurationMs, timeMs) / geometry.trimmedDurationMs;
        };
        auto yAt = [&] (float level)
        {
            return geometry.envBot - geometry.envH * juce::jlimit (0.0f, 1.0f, level);
        };

        juce::Path envelope;
        envelope.startNewSubPath (xAt (0.0f), yAt (0.0f));

        for (int i = 1; i <= kCurveSteps; ++i)
        {
            const float timeMs = geometry.attackEndMs * ((float) i / (float) kCurveSteps);
            envelope.lineTo (xAt (timeMs), yAt (envelopeLevel (timeMs)));
        }

        if (geometry.holdEndMs - geometry.attackEndMs > 0.5f)
            envelope.lineTo (xAt (geometry.holdEndMs), yAt (1.0f));

        for (int i = 1; i <= kCurveSteps; ++i)
        {
            const float timeMs = geometry.holdEndMs + (geometry.decayEndMs - geometry.holdEndMs) * ((float) i / (float) kCurveSteps);
            envelope.lineTo (xAt (timeMs), yAt (envelopeLevel (timeMs)));
        }

        if (geometry.releaseStartMs > geometry.decayEndMs + 0.5f)
            envelope.lineTo (xAt (geometry.releaseStartMs), yAt (envelopeLevel (geometry.releaseStartMs)));

        const float releaseStartLevel = envelopeLevel (geometry.releaseStartMs);
        for (int i = 1; i <= kCurveSteps; ++i)
        {
            const float t = (float) i / (float) kCurveSteps;
            const float level = flSegmentLevel (t, releaseStartLevel, 0.0f, releaseCurve, releaseTen);
            envelope.lineTo (xAt (geometry.releaseStartMs + geometry.releaseMs * t), yAt (level));
        }

        // FL Section Markers ([D] Decay start, [S] Sustain/Release split)
        auto drawSectionMarker = [&] (float mx, const juce::String& tag)
        {
            if (mx < plot.getX() - 6.0f || mx > plot.getRight() + 6.0f)
                return;
            g.setColour (envEnabled ? juce::Colour (0xb8f0f8ff) : juce::Colour (0x85dce8f5));
            g.drawVerticalLine (juce::roundToInt (mx), plot.getY(), plot.getBottom());

            const float bw = 11.0f;
            const float bh = 12.0f;
            const float bx = juce::jlimit (plot.getX() + 2.0f, plot.getRight() - bw - 2.0f, mx + 1.0f);
            const float by = plot.getBottom() - bh - 1.0f;
            juce::Path tab;
            tab.startNewSubPath (bx, by);
            tab.lineTo (bx + bw - 3.0f, by);
            tab.lineTo (bx + bw, by + 3.0f);
            tab.lineTo (bx + bw, by + bh);
            tab.lineTo (bx, by + bh);
            tab.closeSubPath();
            g.setColour (envEnabled ? juce::Colour (0xffcdd7e3) : juce::Colour (0xff9ca7b5));
            g.fillPath (tab);
            g.setColour (juce::Colour (0xff181e26));
            g.setFont (juce::Font (8.5f, juce::Font::bold));
            g.drawText (tag, juce::Rectangle<float> (bx, by, bw, bh), juce::Justification::centred);
        };

        drawSectionMarker (geometry.holdEndX, "D");
        drawSectionMarker (geometry.releaseStartX, "S");

        auto fill = envelope;
        fill.lineTo (geometry.trimX1, geometry.envBot);
        fill.lineTo (geometry.trimX0, geometry.envBot);
        fill.closeSubPath();
        juce::ColourGradient fillGrad (flCurveColour.withAlpha (envEnabled ? 0.28f : 0.18f),
                                       0.0f, geometry.envTop,
                                       flCurveColour.withAlpha (envEnabled ? 0.05f : 0.03f),
                                       0.0f, geometry.envBot, false);
        g.setGradientFill (fillGrad);
        g.fillPath (fill);

        g.setColour (envEnabled ? flCurveColour : flCurveColour.withAlpha (0.82f));
        g.strokePath (envelope, juce::PathStrokeType (envEnabled ? 2.15f : 1.85f,
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

        if (! preview.loaded)
        {
            g.setColour (juce::Colour (0x6bdae5f2));
            g.setFont (juce::Font (11.0f));
            g.drawText ("Drop Sample Here or Click Load Sample",
                        juce::Rectangle<float> (plot.getX(), plot.getBottom() - 28.0f, plot.getWidth(), 18.0f),
                        juce::Justification::centred);
        }

        if (! envEnabled)
        {
            g.setColour (juce::Colour (0xc48cdcf7));
            g.setFont (juce::Font (9.5f, juce::Font::bold));
            g.drawText ("BYPASSED - Click ENV",
                        juce::Rectangle<float> (plot.getRight() - 150.0f, plot.getY() + 6.0f, 142.0f, 14.0f),
                        juce::Justification::centredRight);
        }

        // Front Trim & Back Trim vertical bars + flags
        const bool isTrimmed = pTrimStart->load() > 0.001f || pTrimEnd->load() < 0.999f;
        g.setColour (juce::Colour (0xffff7b00).withAlpha (isTrimmed ? 1.0f : 0.55f));
        g.drawLine (geometry.trimX0, plot.getY(), geometry.trimX0, plot.getBottom(), isTrimmed ? 2.0f : 1.5f);
        g.drawLine (geometry.trimX1, plot.getY(), geometry.trimX1, plot.getBottom(), isTrimmed ? 2.0f : 1.5f);

        g.setColour (juce::Colour (0xffff7b00));
        juce::Path frontFlag, backFlag;
        frontFlag.addTriangle (geometry.trimX0, plot.getY(),
                               geometry.trimX0 + 10.0f, plot.getY() + 5.5f,
                               geometry.trimX0, plot.getY() + 11.0f);
        backFlag.addTriangle (geometry.trimX1, plot.getY(),
                              geometry.trimX1 - 10.0f, plot.getY() + 5.5f,
                              geometry.trimX1, plot.getY() + 11.0f);
        g.fillPath (frontFlag);
        g.fillPath (backFlag);

        // FL Tension Handles (hollow circles)
        auto drawTensionHandle = [&] (juce::Point<float> pt, bool visible)
        {
            if (! visible)
                return;
            const float r = kTensionRadius - 0.3f;
            g.setColour (juce::Colour (0xb82a3440));
            g.fillEllipse (pt.x - r, pt.y - r, 2.0f * r, 2.0f * r);
            g.setColour (envEnabled ? flCurveColour : flCurveColour.withAlpha (0.88f));
            g.drawEllipse (pt.x - r, pt.y - r, 2.0f * r, 2.0f * r, 1.5f);
        };

        drawTensionHandle (geometry.tensionAttack, geometry.tensionAttackVisible);
        drawTensionHandle (geometry.tensionDecay, geometry.tensionDecayVisible);
        drawTensionHandle (geometry.tensionRelease, geometry.tensionReleaseVisible);

        // Crisp FL Control Points (no permanent letters cluttering the diagram)
        auto drawControlPoint = [&] (juce::Point<float> point, bool small = false)
        {
            if (point.x < plot.getX() - 12.0f || point.x > plot.getRight() + 12.0f)
                return;
            const float r = small ? 3.2f : 3.8f;
            g.setColour (juce::Colour (0xff141920));
            g.fillEllipse (point.x - r - 1.4f, point.y - r - 1.4f, 2.0f * (r + 1.4f), 2.0f * (r + 1.4f));
            g.setColour (envEnabled ? flControlDot : juce::Colour (0xffbfefff));
            g.fillEllipse (point.x - r, point.y - r, 2.0f * r, 2.0f * r);
        };

        drawControlPoint (geometry.origin, true);
        drawControlPoint (geometry.attack);
        if (geometry.holdVisible) drawControlPoint (geometry.hold);
        drawControlPoint (geometry.sustain);
        if (geometry.releaseVisible) drawControlPoint (geometry.release);
        drawControlPoint (geometry.endPt, true);
        g.restoreState();
    }

    // Bottom time ruler (shows timestamps for current visible zoom window)
    auto ruler = waveBox.removeFromBottom (kRulerHeight);
    g.setColour (juce::Colour (0x850a0e14));
    g.fillRect (ruler);
    g.setColour (juce::Colour (0xffb9c6d6));
    g.setFont (juce::Font (9.5f));
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

    // Horizontal Overview / Pan Scrollbar strip when zoomed
    if (zoomFactor > 1.01f)
    {
        auto ov = waveBox.removeFromBottom (kOverviewHeight);
        g.setColour (juce::Colour (0xe60d1117));
        g.fillRect (ov);
        g.setColour (juce::Colour (0x1affffff));
        g.drawHorizontalLine (juce::roundToInt (ov.getY()), ov.getX(), ov.getRight());

        const float trimBoxX = ov.getX() + trimStart * ov.getWidth();
        const float trimBoxW = juce::jmax (2.0f, (trimEnd - trimStart) * ov.getWidth());
        g.setColour (juce::Colour (0x4dff7b00));
        g.fillRect (trimBoxX, ov.getY() + 4.0f, trimBoxW, ov.getHeight() - 8.0f);
        g.setColour (juce::Colour (0xffff7b00));
        g.drawVerticalLine (juce::roundToInt (trimBoxX), ov.getY() + 3.0f, ov.getBottom() - 3.0f);
        g.drawVerticalLine (juce::roundToInt (trimBoxX + trimBoxW), ov.getY() + 3.0f, ov.getBottom() - 3.0f);

        const float thumbX = ov.getX() + viewStartNorm * ov.getWidth();
        const float thumbW = juce::jmax (10.0f, vpSpan * ov.getWidth());
        g.setColour (juce::Colour (0x33ffffff));
        g.fillRoundedRectangle (thumbX, ov.getY() + 1.5f, thumbW, ov.getHeight() - 3.0f, 2.0f);
        g.setColour (juce::Colour (0xccdce6f2));
        g.drawRoundedRectangle (thumbX, ov.getY() + 1.5f, thumbW, ov.getHeight() - 3.0f, 2.0f, 1.0f);
    }

    // FL Studio Envelope Toolbar Strip
    g.setColour (juce::Colour (0xff1a1924));
    g.fillRoundedRectangle (flBarBounds.toFloat(), 3.0f);
    g.setColour (juce::Colour (0xff2d2b3c));
    g.drawRoundedRectangle (flBarBounds.toFloat().reduced (0.5f), 3.0f, 1.0f);

    const juce::Colour flCurveColour (0xff57d6ff);
    g.setFont (juce::Font (9.5f, juce::Font::bold));
    auto drawFlBoxLabel = [&] (const juce::ComboBox& box, const juce::String& lbl, float tensionVal)
    {
        g.setColour (juce::Colour (0xff8c91ad));
        g.drawText (lbl, box.getX() - 28, box.getY(), 26, box.getHeight(), juce::Justification::centredRight);
        const int pct = juce::roundToInt (tensionVal * 100.0f);
        g.setColour (std::abs (pct) > 0 ? flCurveColour : juce::Colour (0xff8c91ad));
        g.drawText ((pct > 0 ? "+" : "") + juce::String (pct) + "%",
                    box.getRight() + 3, box.getY(), 34, box.getHeight(), juce::Justification::centredLeft);
    };
    drawFlBoxLabel (attackCurveBox, "ATT", pAttackTension->load());
    drawFlBoxLabel (decayCurveBox, "DEC", pDecayTension->load());
    drawFlBoxLabel (releaseCurveBox, "REL", pReleaseTension->load());

    // Bottom Snapback Dark Control Strip
    g.setColour (juce::Colour (0xff17161f));
    g.fillRoundedRectangle (controlStripBounds.toFloat(), 3.0f);
    g.setColour (juce::Colour (0xff2d2b3c));
    g.drawRoundedRectangle (controlStripBounds.toFloat().reduced (0.5f), 3.0f, 1.0f);

    g.setColour (juce::Colour (0xff8f92ab));
    g.setFont (juce::Font (9.5f, juce::Font::bold));
    auto drawKnobLabel = [&] (const juce::Slider& s, const juce::String& label)
    {
        const auto b = s.getBounds();
        g.drawText (label, b.getX(), b.getY() - 11, b.getWidth(), 10, juce::Justification::centred);
    };
    drawKnobLabel (gainSlider, "Vol");
    drawKnobLabel (pitchSlider, "Pitch");
    drawKnobLabel (pogoSlider, "Pogo");
    drawKnobLabel (trimStartSlider, "Trim F");
    drawKnobLabel (trimEndSlider, "Trim B");
    drawKnobLabel (fadeInSlider, "Fade In");
    drawKnobLabel (fadeOutSlider, "Fade Out");
    drawKnobLabel (delaySlider, "Shift");
    drawKnobLabel (panSlider, "Pan");
    drawKnobLabel (highPassSlider, "HP");
    drawKnobLabel (lowPassSlider, "LP");
}

void LayerEnvelopeComponent::resized()
{
    auto area = getLocalBounds();
    controlStripBounds = area.removeFromBottom (kControlStripHeight);
    area.removeFromBottom (4);
    flBarBounds = area.removeFromBottom (kFlBarHeight);

    auto waveArea = area;
    auto headerBar = waveArea.removeFromTop (kHeaderHeight).reduced (4, 3);
    zoomInBtn.setBounds (headerBar.removeFromRight (20));
    zoomResetBtn.setBounds (headerBar.removeFromRight (38));
    zoomOutBtn.setBounds (headerBar.removeFromRight (20));
    headerBar.removeFromRight (4);
    zoomFitBtn.setBounds (headerBar.removeFromRight (26));
    headerBar.removeFromRight (3);
    focusEndBtn.setBounds (headerBar.removeFromRight (22));
    headerBar.removeFromRight (2);
    focusStartBtn.setBounds (headerBar.removeFromRight (22));

    auto bottomWave = waveArea.toFloat();
    bottomWave.removeFromBottom (kRulerHeight);
    overviewBarBounds = (zoomFactor > 1.01f)
        ? bottomWave.removeFromBottom (kOverviewHeight)
        : juce::Rectangle<float>();

    auto flRow = flBarBounds.reduced (6, 3);
    envEnableBtn.setBounds (flRow.removeFromLeft (44));
    flRow.removeFromLeft (8);

    const int segSlotW = juce::jmin (164, juce::jmax (96, (flRow.getWidth() - 12) / 3));
    auto placeSeg = [&] (juce::ComboBox& box)
    {
        auto slot = flRow.removeFromLeft (segSlotW);
        slot.removeFromLeft (28); // ATT/DEC/REL label
        slot.removeFromRight (38); // tension readout
        box.setBounds (slot);
        flRow.removeFromLeft (6);
    };
    placeSeg (attackCurveBox);
    placeSeg (decayCurveBox);
    placeSeg (releaseCurveBox);

    auto strip = controlStripBounds.reduced (8, 5);
    const int rowH = (strip.getHeight() - 4) / 2;
    auto row1 = strip.removeFromTop (rowH);
    strip.removeFromTop (4);
    auto row2 = strip;

    const int knobW = 50;
    const int knobGap = 4;

    // Row 1: [REV] [Gain] [Pitch] [Pogo] [Trim F] [Trim B] [Fade In] [Fade Out]
    reverseBtn.setBounds (row1.removeFromLeft (42).withSizeKeepingCentre (42, 26));
    row1.removeFromLeft (8);
    row1.removeFromTop (11);
    for (auto* s : { &gainSlider, &pitchSlider, &pogoSlider, &trimStartSlider, &trimEndSlider, &fadeInSlider, &fadeOutSlider })
    {
        s->setBounds (row1.removeFromLeft (knobW));
        row1.removeFromLeft (knobGap);
    }

    // Row 2: [Timing] [Pan] [HPF] [LPF]
    row2.removeFromTop (11);
    for (auto* s : { &delaySlider, &panSlider, &highPassSlider, &lowPassSlider })
    {
        s->setBounds (row2.removeFromLeft (knobW));
        row2.removeFromLeft (knobGap);
    }
}

LayerEnvelopeComponent::Handle LayerEnvelopeComponent::hitTestHandle (juce::Point<float> pos,
                                                                      const Geometry& g,
                                                                      float maxRadius) const noexcept
{
    struct Candidate { Handle h; juce::Point<float> pt; bool active; };
    const Candidate list[] = {
        { Handle::TensionAttack, g.tensionAttack, g.tensionAttackVisible },
        { Handle::TensionDecay, g.tensionDecay, g.tensionDecayVisible },
        { Handle::TensionRelease, g.tensionRelease, g.tensionReleaseVisible },
        { Handle::Attack, g.attack, true },
        { Handle::Hold, g.hold, g.holdVisible },
        { Handle::Sustain, g.sustain, true },
        { Handle::Release, g.release, g.releaseVisible },
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
        const float distEnd = endVisible ? std::abs (pos.x - g.trimX1) : 1.0e6f;
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
            outTitle = "Point D (Attack Peak)";
            return;
        case Handle::TensionAttack:
            outSegment = Segment::Attack;
            outCirclePos = g.tensionAttack;
            outTitle = "Attack Tension";
            return;
        case Handle::Hold:
            outSegment = Segment::Decay;
            outCirclePos = g.hold;
            outTitle = "Point H (Hold End)";
            return;
        case Handle::Sustain:
        case Handle::Decay:
            outSegment = Segment::Decay;
            outCirclePos = g.sustain;
            outTitle = "Point S (Sustain / Decay)";
            return;
        case Handle::TensionDecay:
            outSegment = Segment::Decay;
            outCirclePos = g.tensionDecay;
            outTitle = "Decay Tension";
            return;
        case Handle::Release:
            outSegment = Segment::Release;
            outCirclePos = g.releaseVisible ? g.release : g.endPt;
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

    if (pos.getDistanceFrom (g.endPt) <= 18.0f)
    {
        outSegment = Segment::Release;
        outCirclePos = g.endPt;
        outTitle = "Point End (Release)";
        return;
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
        case Handle::TrimStart: parameterId = ids::layerTrimStart (layer); break;
        case Handle::TrimEnd: parameterId = ids::layerTrimEnd (layer); break;
        case Handle::Attack: parameterId = ids::layerAttack (layer); break;
        case Handle::Hold: parameterId = ids::layerHold (layer); break;
        case Handle::Decay: parameterId = ids::layerDecay (layer); break;
        case Handle::Sustain: parameterId = ids::layerSustain (layer); break;
        case Handle::Release: parameterId = ids::layerRelease (layer); break;
        case Handle::TensionAttack: parameterId = ids::layerAttackTension (layer); break;
        case Handle::TensionDecay: parameterId = ids::layerDecayTension (layer); break;
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
        case Handle::TrimStart: parameterId = ids::layerTrimStart (layer); break;
        case Handle::TrimEnd: parameterId = ids::layerTrimEnd (layer); break;
        case Handle::Attack: parameterId = ids::layerAttack (layer); break;
        case Handle::Hold: parameterId = ids::layerHold (layer); break;
        case Handle::Decay: parameterId = ids::layerDecay (layer); break;
        case Handle::Sustain: parameterId = ids::layerSustain (layer); break;
        case Handle::Release: parameterId = ids::layerRelease (layer); break;
        case Handle::TensionAttack: parameterId = ids::layerAttackTension (layer); break;
        case Handle::TensionDecay: parameterId = ids::layerDecayTension (layer); break;
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
        {
            const float rest = juce::jmax (0.0f, pHold->load())
                             + juce::jmax (0.1f, pDecay->load())
                             + juce::jmax (0.1f, pRelease->load());
            const float clampedT = juce::jlimit (0.1f, juce::jmax (0.2f, duration - 1.0f), timeMs);
            const float nextAttack = (clampedT + rest <= duration)
                ? clampedT
                : (clampedT * rest) / juce::jmax (1.0f, duration - clampedT);
            setParameter (ids::layerAttack (layer), juce::jlimit (0.1f, 2000.0f, nextAttack));
            break;
        }
        case Handle::Hold:
            setParameter (ids::layerHold (layer),
                          juce::jlimit (0.0f, 2000.0f, timeMs - geometry.attackEndMs));
            break;
        case Handle::Sustain:
        {
            const float level = juce::jlimit (0.0f, 1.0f, (geometry.envBot - position.y) / geometry.envH);
            setParameter (ids::layerSustain (layer), level);
            if (! geometry.releaseVisible)
            {
                const float minT = juce::jmin (duration - 1.0f, geometry.holdEndMs + 1.0f);
                const float splitT = juce::jlimit (minT, juce::jmax (minT, duration - 1.0f), timeMs);
                const float prevSum = juce::jmax (0.1f, pAttack->load())
                                    + juce::jmax (0.0f, pHold->load())
                                    + juce::jmax (0.1f, pDecay->load())
                                    + juce::jmax (0.1f, pRelease->load());
                const float scale = prevSum > duration ? (prevSum / duration) : 1.0f;
                setParameter (ids::layerDecay (layer), juce::jlimit (0.1f, 5000.0f, (splitT - geometry.holdEndMs) * scale));
                setParameter (ids::layerRelease (layer), juce::jlimit (0.1f, 10000.0f, (duration - splitT) * scale));
            }
            else
            {
                setParameter (ids::layerDecay (layer),
                              juce::jlimit (0.1f, 5000.0f, timeMs - geometry.holdEndMs));
            }
            break;
        }
        case Handle::Release:
        {
            const float level = juce::jlimit (0.0f, 1.0f, (geometry.envBot - position.y) / geometry.envH);
            setParameter (ids::layerSustain (layer), level);
            setParameter (ids::layerRelease (layer), juce::jlimit (0.1f, 10000.0f, duration - timeMs));
            break;
        }
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
