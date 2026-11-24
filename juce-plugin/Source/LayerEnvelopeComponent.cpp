#include "LayerEnvelopeComponent.h"

#include <cmath>

namespace
{
    constexpr float kHandleRadius = 4.6f;
    constexpr float kTensionRadius = 4.6f;
    constexpr int kCurveSteps = 96;
    constexpr int kFlBarHeight = 28;
    constexpr int kControlStripHeight = 120;
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

    const bool isPre = layer < 3;
    const juce::Colour flCurveColour (isPre ? 0xffa78bfa : 0xffd08aff);

    envEnableBtn.setButtonText ("ENV");
    envEnableBtn.setClickingTogglesState (true);
    envEnableBtn.setTooltip ("Enable or bypass FL envelope");
    envEnableBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252235));
    envEnableBtn.setColour (juce::TextButton::buttonOnColourId, flCurveColour);
    envEnableBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffc2bad8));
    envEnableBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff14101c));
    addAndMakeVisible (envEnableBtn);
    envEnableAttachment = std::make_unique<ButtonAtt> (state, ids::layerEnvEnabled (layer), envEnableBtn);

    reverseBtn.setButtonText ("REV");
    reverseBtn.setClickingTogglesState (true);
    reverseBtn.setTooltip ("Reverse sample playback");
    reverseBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252235));
    reverseBtn.setColour (juce::TextButton::buttonOnColourId, flCurveColour);
    reverseBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe7e2f5));
    reverseBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff14101c));
    addAndMakeVisible (reverseBtn);
    reverseAttachment = std::make_unique<ButtonAtt> (state, ids::layerReverse (layer), reverseBtn);

    const juce::StringArray flCurveNames { "Hold", "Smooth", "Single curve", "Half sine", "Stairs", "Smooth stairs", "Pulse", "Wave" };

    auto prepareCurveBox = [&] (juce::ComboBox& box, const juce::String& tooltip)
    {
        box.addItemList (flCurveNames, 1);
        box.setTooltip (tooltip);
        box.setJustificationType (juce::Justification::centredLeft);
        box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff211d31));
        box.setColour (juce::ComboBox::textColourId, juce::Colour (0xffe5def4));
        box.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff51456f));
        box.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xffc4a8ff));
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
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 12);
        slider.setColour (juce::Slider::rotarySliderFillColourId, fill);
        slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff514968));
        slider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffd8c8ff));
        slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffe4def4));
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setTooltip (tooltip);
        addAndMakeVisible (slider);
    };

    const juce::Colour secondaryAccent (isPre ? juce::Colour (0xff7866d8)
                                               : juce::Colour (0xffa95fca));
    prepareRotary (gainSlider, "Layer gain (0% to 200%)", colour);
    prepareRotary (pitchSlider, "Layer pitch: -24 to +24 semitones", colour);
    prepareRotary (pogoSlider, "FL Sampler style pitch-bend envelope (-100% to +100%)", secondaryAccent);
    prepareRotary (trimStartSlider, "Sample front trim (0% to 98%)", secondaryAccent);
    prepareRotary (trimEndSlider, "Sample back trim (2% to 100%)", secondaryAccent);
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
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff242033));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4a3d70));
        b.setColour (juce::TextButton::textColourOffId, textCol);
        b.setColour (juce::TextButton::textColourOnId, textCol);
        addAndMakeVisible (b);
    };

    const juce::Colour orangeTxt (0xffc0a6ff);
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
                            zoomed ? juce::Colour (0xffc0a6ff) : juce::Colour (0xffaebacc));
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

    const float attack = juce::jmax (0.1f, pAttack->load());
    const float hold = juce::jmax (0.0f, pHold->load());
    const float decay = juce::jmax (0.1f, pDecay->load());
    const float release = juce::jmin (pRelease->load(), result.trimmedDurationMs * 0.5f);
    const float releaseStart = juce::jmax (0.0f, result.trimmedDurationMs - release);
    const float attackEnd = juce::jmin (attack, releaseStart);
    const float holdEnd = juce::jmin (attack + hold, releaseStart);
    const float decayEnd = juce::jmin (attack + hold + decay, releaseStart);
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
    const float envTop = result.plot.getY() + 7.0f;
    const float envBot = result.plot.getBottom() - 15.0f;
    const float envH = juce::jmax (12.0f, envBot - envTop);
    auto yAt = [&] (float level)
    {
        return envBot - envH * juce::jlimit (0.0f, 1.0f, level);
    };
    auto levelAt = [&] (float ms)
    {
        if (ms < attack)
            return flSegmentLevel (ms / attack, 0.0f, 1.0f, attMode, attTen);
        if (ms < attack + hold)
            return 1.0f;
        const float decayProgress = (ms - attack - hold) / decay;
        if (decayProgress < 1.0f)
            return flSegmentLevel (decayProgress, 1.0f, sustain, decMode, decTen);
        return sustain;
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
        yAt (flSegmentLevel (juce::jlimit (0.0f, 1.0f, attMidMs / attack), 0.0f, 1.0f, attMode, attTen))
    };
    result.tensionAttackVisible = (result.attack.x - result.trimX0) >= 10.0f;

    const float decMidMs = holdEnd + (decayEnd - holdEnd) * 0.5f;
    result.tensionDecay = {
        xAt (decMidMs),
        yAt (flSegmentLevel (juce::jlimit (0.0f, 1.0f, (decMidMs - attack - hold) / decay), 1.0f, sustain, decMode, decTen))
    };
    result.tensionDecayVisible = (result.decay.x - result.hold.x) >= 10.0f;

    const float relMidMs = releaseStart + release * 0.5f;
    result.tensionRelease = {
        xAt (relMidMs),
        yAt (flSegmentLevel (0.5f, relStartLevel, 0.0f, relMode, relTen))
    };
    result.tensionReleaseVisible = (result.trimX1 - result.release.x) >= 10.0f;

    return result;
}

void LayerEnvelopeComponent::paint (juce::Graphics& g)
{
    auto waveBox = getLocalBounds().withTrimmedBottom (kControlStripHeight + kFlBarHeight + 10).toFloat();
    const bool isPre = layer < 3;
    const bool envEnabled = pEnvEnabled->load() > 0.5f;
    const juce::Colour bgTop (isPre ? 0xff251a3d : 0xff2b1938);
    const juce::Colour bgBot (isPre ? 0xff171127 : 0xff1a1025);
    const juce::Colour simWave (isPre ? 0xffa99aff : 0xffd18cff);
    const juce::Colour flCurveColour (isPre ? 0xffa78bfa : 0xffd08aff);
    const juce::Colour flControlDot (isPre ? 0xffe0d6ff : 0xffffdfff);

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
    g.setColour (juce::Colour (0xb80c1016));
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

        // Dark violet translucent overlay + adaptive grid lines
        g.setColour (envEnabled ? juce::Colour (0x6b2a2140) : juce::Colour (0x57151022));
        g.fillRect (plot);

        g.setColour (juce::Colour (0x1dbaa4ff));
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
            g.setColour (r == 2 ? juce::Colour (0x35d4bdff) : juce::Colour (0x18baa4ff));
            const float y = plot.getY() + plot.getHeight() * (float) r / 4.0f;
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        }

        if (preview.loaded)
        {
            const float centre = plot.getCentreY();
            const float amplitude = plot.getHeight() * 0.44f;
            g.setColour (simWave.withAlpha (0.88f));
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
            g.setColour (juce::Colour (0x8ad7e2f0));
            g.setFont (juce::Font (12.0f));
            g.drawText ("Drop Sample Here or Click Load Sample", plot.toNearestInt(),
                        juce::Justification::centred);
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
            g.setColour (juce::Colour (0xffd4b9ff).withAlpha (0.85f));
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
            g.setColour (juce::Colour (0xffd4b9ff).withAlpha (0.85f));
            g.drawLine (geometry.trimX1 - fadeOutPx, plot.getY(), geometry.trimX1, plot.getBottom(), 1.25f);
        }

        const float attack = juce::jmax (0.1f, pAttack->load());
        const float hold = juce::jmax (0.0f, pHold->load());
        const float decay = juce::jmax (0.1f, pDecay->load());
        const float sustain = pSustain->load();
        const float release = juce::jmin (pRelease->load(), geometry.trimmedDurationMs * 0.5f);
        const float releaseStart = juce::jmax (0.0f, geometry.trimmedDurationMs - release);
        const int attackCurve = juce::roundToInt (pAttackCurve->load());
        const int decayCurve = juce::roundToInt (pDecayCurve->load());
        const int releaseCurve = juce::roundToInt (pReleaseCurve->load());
        const float attackTen = pAttackTension->load();
        const float decayTen = pDecayTension->load();
        const float releaseTen = pReleaseTension->load();

        auto envelopeLevel = [&] (float timeMs)
        {
            if (timeMs < attack)
                return flSegmentLevel (timeMs / attack, 0.0f, 1.0f, attackCurve, attackTen);
            if (timeMs < attack + hold)
                return 1.0f;
            const float p = (timeMs - attack - hold) / decay;
            if (p < 1.0f)
                return flSegmentLevel (p, 1.0f, sustain, decayCurve, decayTen);
            return sustain;
        };
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

        juce::Path envelope;
        envelope.startNewSubPath (xAt (0.0f), yAt (0.0f));

        const float attackEnd = juce::jmin (attack, releaseStart);
        for (int i = 1; i <= kCurveSteps; ++i)
        {
            const float timeMs = attackEnd * ((float) i / (float) kCurveSteps);
            envelope.lineTo (xAt (timeMs), yAt (envelopeLevel (timeMs)));
        }

        const float holdEnd = juce::jmin (attack + hold, releaseStart);
        envelope.lineTo (xAt (holdEnd), yAt (envelopeLevel (holdEnd)));

        const float decayEnd = juce::jmin (attack + hold + decay, releaseStart);
        for (int i = 1; i <= kCurveSteps; ++i)
        {
            const float timeMs = holdEnd + (decayEnd - holdEnd) * ((float) i / (float) kCurveSteps);
            envelope.lineTo (xAt (timeMs), yAt (envelopeLevel (timeMs)));
        }

        envelope.lineTo (xAt (releaseStart), yAt (envelopeLevel (releaseStart)));
        const float releaseStartLevel = envelopeLevel (releaseStart);
        for (int i = 1; i <= kCurveSteps; ++i)
        {
            const float t = (float) i / (float) kCurveSteps;
            const float level = flSegmentLevel (t, releaseStartLevel, 0.0f, releaseCurve, releaseTen);
            envelope.lineTo (xAt (releaseStart + release * t), yAt (level));
        }

        // FL Section Markers ([D] Decay start, [S] Sustain/Release split)
        auto drawSectionMarker = [&] (float mx, const juce::String& tag)
        {
            if (mx < plot.getX() - 6.0f || mx > plot.getRight() + 6.0f)
                return;
            g.setColour (envEnabled ? juce::Colour (0x8cebf2fa) : juce::Colour (0x40c8d2e0));
            g.drawVerticalLine (juce::roundToInt (mx), plot.getY(), plot.getBottom());

            const float bx = juce::jlimit (plot.getX() + 2.0f, plot.getRight() - 14.0f, mx + 1.0f);
            const float by = plot.getBottom() - 13.0f;
            g.setColour (envEnabled ? juce::Colour (0xffb9c3cf) : juce::Colour (0xff58626e));
            g.fillRoundedRectangle (bx, by, 12.0f, 12.0f, 2.0f);
            g.setColour (juce::Colour (0xff1d172b));
            g.setFont (juce::Font (8.5f, juce::Font::bold));
            g.drawText (tag, juce::Rectangle<float> (bx, by, 12.0f, 12.0f), juce::Justification::centred);
        };

        drawSectionMarker (geometry.holdEndX, "D");
        drawSectionMarker (geometry.releaseStartX, "S");

        auto fill = envelope;
        fill.lineTo (geometry.trimX1, envBot);
        fill.lineTo (geometry.trimX0, envBot);
        fill.closeSubPath();
        g.setColour (envEnabled ? flCurveColour.withAlpha (0.16f) : juce::Colour (0x10a0aab9));
        g.fillPath (fill);

        g.setColour (envEnabled ? flCurveColour : juce::Colour (0x73b4c0ce));
        g.strokePath (envelope, juce::PathStrokeType (envEnabled ? 2.0f : 1.4f,
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

        // Front Trim & Back Trim vertical bars + flags
        g.setColour (juce::Colour (0xffb79aff));
        g.drawLine (geometry.trimX0, plot.getY(), geometry.trimX0, plot.getBottom(), 2.0f);
        g.drawLine (geometry.trimX1, plot.getY(), geometry.trimX1, plot.getBottom(), 2.0f);

        juce::Path frontFlag, backFlag;
        frontFlag.addTriangle (geometry.trimX0, plot.getY(),
                               geometry.trimX0 + 11.0f, plot.getY() + 6.0f,
                               geometry.trimX0, plot.getY() + 12.0f);
        backFlag.addTriangle (geometry.trimX1, plot.getY(),
                              geometry.trimX1 - 11.0f, plot.getY() + 6.0f,
                              geometry.trimX1, plot.getY() + 12.0f);
        g.fillPath (frontFlag);
        g.fillPath (backFlag);

        // FL Tension Handles (hollow circles ○)
        auto drawTensionHandle = [&] (juce::Point<float> pt, bool visible)
        {
            if (! visible)
                return;
            g.setColour (juce::Colour (0xff201a30));
            g.fillEllipse (pt.x - kTensionRadius, pt.y - kTensionRadius,
                           2.0f * kTensionRadius, 2.0f * kTensionRadius);
            g.setColour (envEnabled ? flCurveColour : juce::Colour (0x80b4c0ce));
            g.drawEllipse (pt.x - kTensionRadius, pt.y - kTensionRadius,
                           2.0f * kTensionRadius, 2.0f * kTensionRadius, 1.7f);
        };

        drawTensionHandle (geometry.tensionAttack, geometry.tensionAttackVisible);
        drawTensionHandle (geometry.tensionDecay, geometry.tensionDecayVisible);
        drawTensionHandle (geometry.tensionRelease, geometry.tensionReleaseVisible);

        // Control Points (solid ● dots)
        auto drawControlPoint = [&] (juce::Point<float> point, const juce::String& label)
        {
            if (point.x < plot.getX() - 12.0f || point.x > plot.getRight() + 12.0f)
                return;
            g.setColour (juce::Colour (0xff181321));
            g.fillEllipse (point.x - kHandleRadius - 1.5f, point.y - kHandleRadius - 1.5f,
                           2.0f * (kHandleRadius + 1.5f), 2.0f * (kHandleRadius + 1.5f));
            g.setColour (envEnabled ? flControlDot : juce::Colour (0xff8a95a5));
            g.fillEllipse (point.x - kHandleRadius, point.y - kHandleRadius,
                           2.0f * kHandleRadius, 2.0f * kHandleRadius);
            g.setColour (juce::Colour (0xffeef3fa));
            g.setFont (juce::Font (9.0f, juce::Font::bold));
            const bool nearTop = (point.y - plot.getY()) < 18.0f;
            const float labelX = juce::jlimit (plot.getX() + 1.0f, plot.getRight() - 15.0f, point.x - 7.0f);
            const float labelY = nearTop ? (point.y + kHandleRadius + 2.0f) : (point.y - kHandleRadius - 12.0f);
            g.drawText (label, juce::Rectangle<float> (labelX, labelY, 14.0f, 10.0f),
                        juce::Justification::centred);
        };

        drawControlPoint (geometry.attack, "A");
        drawControlPoint (geometry.hold, "H");
        drawControlPoint (geometry.decay, "D");
        drawControlPoint (geometry.sustain, "S");
        drawControlPoint (geometry.release, "R");
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
        g.setColour (juce::Colour (0x4dc09cff));
        g.fillRect (trimBoxX, ov.getY() + 4.0f, trimBoxW, ov.getHeight() - 8.0f);
        g.setColour (juce::Colour (0xffb79aff));
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
    g.setColour (juce::Colour (0xff211d31));
    g.fillRoundedRectangle (flBarBounds.toFloat(), 3.0f);
    g.setColour (juce::Colour (0xff43385f));
    g.drawRoundedRectangle (flBarBounds.toFloat().reduced (0.5f), 3.0f, 1.0f);

    g.setFont (juce::Font (9.5f, juce::Font::bold));
    auto drawFlBoxLabel = [&] (const juce::ComboBox& box, const juce::String& lbl, float tensionVal)
    {
        g.setColour (juce::Colour (0xff95a4b8));
        g.drawText (lbl, box.getX() - 28, box.getY(), 26, box.getHeight(), juce::Justification::centredRight);
        const int pct = juce::roundToInt (tensionVal * 100.0f);
        g.setColour (std::abs (pct) > 0 ? flCurveColour : juce::Colour (0xff8a96a6));
        g.drawText ((pct > 0 ? "+" : "") + juce::String (pct) + "%",
                    box.getRight() + 3, box.getY(), 34, box.getHeight(), juce::Justification::centredLeft);
    };
    drawFlBoxLabel (attackCurveBox, "ATT", pAttackTension->load());
    drawFlBoxLabel (decayCurveBox, "DEC", pDecayTension->load());
    drawFlBoxLabel (releaseCurveBox, "REL", pReleaseTension->load());

    // Dark sample-control strip; all controls remain inside the selected layer panel.
    g.setColour (juce::Colour (0xff171522));
    g.fillRoundedRectangle (controlStripBounds.toFloat(), 3.0f);
    g.setColour (juce::Colour (0xff39324f));
    g.drawRoundedRectangle (controlStripBounds.toFloat().reduced (0.5f), 3.0f, 1.0f);

    g.setColour (juce::Colour (0xffc7c0df));
    g.setFont (juce::Font (9.5f, juce::Font::bold));
    auto drawKnobLabel = [&] (const juce::Slider& s, const juce::String& label)
    {
        const auto b = s.getBounds();
        g.drawText (label, b.getX(), b.getY() - 12, b.getWidth(), 11, juce::Justification::centred);
    };
    drawKnobLabel (gainSlider, "Gain");
    drawKnobLabel (pitchSlider, "Pitch");
    drawKnobLabel (pogoSlider, "Pogo");
    drawKnobLabel (trimStartSlider, "Trim F");
    drawKnobLabel (trimEndSlider, "Trim B");
    drawKnobLabel (fadeInSlider, "Fade In");
    drawKnobLabel (fadeOutSlider, "Fade Out");
    drawKnobLabel (delaySlider, "Timing");
    drawKnobLabel (panSlider, "Pan");
    drawKnobLabel (highPassSlider, "HPF");
    drawKnobLabel (lowPassSlider, "LPF");
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

    const int segSlotW = juce::jmin (164, juce::jmax (110, flRow.getWidth() / 3));
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

    const int knobW = 54;
    const int knobGap = 6;

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