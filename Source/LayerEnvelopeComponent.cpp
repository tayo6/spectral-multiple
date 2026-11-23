#include "LayerEnvelopeComponent.h"

#include <cmath>

namespace
{
    constexpr float kHandleRadius = 5.0f;
    constexpr int kCurveSteps = 48;
    constexpr int kMaximumUndoStates = 40;

    const juce::Colour kGraphBackground { 0xff202936 };
    const juce::Colour kGraphGrid { 0xff536171 };
    const juce::Colour kGraphText { 0xffdce4eb };
    const juce::Colour kGraphMuted { 0xff8e9ba8 };

    juce::String formatEnvelopeTime (float value)
    {
        if (value >= 1000.0f)
            return juce::String (value / 1000.0f, 2) + " s";
        return juce::String (value, value < 10.0f ? 1 : 0) + " ms";
    }

    void styleToolButton (juce::TextButton& button, const juce::Colour& accent)
    {
        button.setClickingTogglesState (true);
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1b2229));
        button.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.88f));
        button.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffdce4eb));
        button.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff10151a));
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
    pEnvelopeEnabled = state.getRawParameterValue (ids::layerEnvelopeEnabled (layer));

    auto prepareSlider = [&] (juce::Slider& slider, const juce::String& tooltip)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 38, 14);
        slider.setColour (juce::Slider::trackColourId, colour);
        slider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff303942));
        slider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffedf2f5));
        slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffe7edf2));
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff1b2229));
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff47525d));
        slider.setTooltip (tooltip);
        addAndMakeVisible (slider);
    };

    prepareSlider (panSlider, "Layer pan: left / centre / right");
    prepareSlider (pitchSlider, "Layer pitch: -24 to +24 semitones");
    prepareSlider (highPassSlider, "Layer high-pass filter: 20 Hz to 1 kHz");
    prepareSlider (lowPassSlider, "Layer low-pass filter: 1 kHz to 20 kHz");

    panAttachment = std::make_unique<SliderAtt> (state, ids::layerPan (layer), panSlider);
    pitchAttachment = std::make_unique<SliderAtt> (state, ids::layerPitch (layer), pitchSlider);
    highPassAttachment = std::make_unique<SliderAtt> (state, ids::layerHighPass (layer), highPassSlider);
    lowPassAttachment = std::make_unique<SliderAtt> (state, ids::layerLowPass (layer), lowPassSlider);

    panSlider.textFromValueFunction = [] (double value)
    {
        if (std::abs (value) < 0.005)
            return juce::String ("C");
        return juce::String (std::abs (value) * 100.0, 0) + (value < 0.0 ? "L" : "R");
    };
    pitchSlider.textFromValueFunction = [] (double value)
    {
        return juce::String (value > 0.0 ? "+" : "") + juce::String (value, 1);
    };
    auto hzText = [] (double value)
    {
        return value >= 1000.0 ? juce::String (value / 1000.0, 1) + "k"
                               : juce::String (value, 0);
    };
    highPassSlider.textFromValueFunction = hzText;
    lowPassSlider.textFromValueFunction = hzText;
    panSlider.updateText();
    pitchSlider.updateText();
    highPassSlider.updateText();
    lowPassSlider.updateText();

    muteButton.setButtonText ("M");
    muteButton.setClickingTogglesState (true);
    muteButton.setTooltip ("Mute this sample layer");
    muteButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1b2229));
    muteButton.setColour (juce::TextButton::buttonOnColourId, colour.withAlpha (0.9f));
    muteButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffaeb8c0));
    muteButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff10151a));
    addAndMakeVisible (muteButton);
    muteAttachment = std::make_unique<ButtonAtt> (state, ids::layerMute (layer), muteButton);

    auto setupTool = [&] (juce::TextButton& button, const juce::String& text, const juce::String& tip)
    {
        button.setButtonText (text);
        button.setTooltip (tip);
        styleToolButton (button, colour);
        addAndMakeVisible (button);
    };

    setupTool (enableButton, "ENV ON", "Enable or bypass this layer's envelope");
    enableButton.setComponentID ("envelope-enable");
    envelopeEnabledAttachment = std::make_unique<ButtonAtt> (
        state, ids::layerEnvelopeEnabled (layer), enableButton);

    setupTool (undoButton, "UNDO", "Undo the last envelope edit");
    undoButton.setClickingTogglesState (false);
    undoButton.onClick = [this] { undo(); };

    setupTool (freezeButton, "FREEZE", "Lock the envelope editor against changes");
    freezeButton.setToggleState (false, juce::dontSendNotification);
    freezeButton.onClick = [this] { frozen = freezeButton.getToggleState(); };

    setupTool (drawButton, "DRAW", "Draw mode: drag on a curve to adjust its tension");
    drawButton.onClick = [this] { drawMode = drawButton.getToggleState(); };

    setupTool (snapButton, "SNAP", "Snap envelope timing points to a 1/16 sample grid");
    snapButton.setToggleState (true, juce::dontSendNotification);
    snapButton.onClick = [this] { snapEnabled = snapButton.getToggleState(); };

    setupTool (slideButton, "SLIDE", "Slide mode: move the selected ADSR timing marker with its following section");
    slideButton.onClick = [this] { slidePoints = slideButton.getToggleState(); };

    setupTool (envelopeMenuButton, "•••", "Envelope presets and tools");
    envelopeMenuButton.setClickingTogglesState (false);
    envelopeMenuButton.onClick = [this] { showEnvelopeMenu(); };

    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setOpaque (false);
    startTimerHz (15);
}

LayerEnvelopeComponent::~LayerEnvelopeComponent()
{
    stopTimer();
    endGesture();
}

void LayerEnvelopeComponent::timerCallback()
{
    const bool enabled = pEnvelopeEnabled->load() > 0.5f;
    enableButton.setButtonText (enabled ? "ENV ON" : "ENV OFF");
    undoButton.setEnabled (! undoStack.empty());
    repaint();
}

float LayerEnvelopeComponent::curveValue (float progress, int curve, float tension) const noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, progress);
    float value = t;

    switch (juce::jlimit (0, 14, curve))
    {
        case 0: value = t; break;
        case 1: value = t * t; break;
        case 2: value = 1.0f - (1.0f - t) * (1.0f - t); break;
        case 3: value = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); break;
        case 4: value = (std::exp (5.0f * t) - 1.0f) / (std::exp (5.0f) - 1.0f); break;
        case 5: value = std::log1p (9.0f * t) / std::log (10.0f); break;
        case 6:
        case 9:
        {
            float u = t;
            for (int iteration = 0; iteration < 6; ++iteration)
            {
                const float oneMinus = 1.0f - u;
                const float x = 3.0f * oneMinus * oneMinus * u * 0.22f
                                + 3.0f * oneMinus * u * u * 0.42f + u * u * u;
                const float dx = 3.0f * oneMinus * oneMinus * 0.22f
                                 + 6.0f * oneMinus * u * (0.42f - 0.22f)
                                 + 3.0f * u * u * (1.0f - 0.42f);
                if (std::abs (dx) > 1.0e-5f)
                    u = juce::jlimit (0.0f, 1.0f, u + (t - x) / dx);
            }
            const float oneMinus = 1.0f - u;
            value = 3.0f * oneMinus * oneMinus * u * 0.02f
                    + 3.0f * oneMinus * u * u * 0.98f + u * u * u;
            break;
        }
        case 7: value = (t >= 0.999f ? 1.0f : 0.0f); break;
        case 8: value = t * t * (3.0f - 2.0f * t); break;
        case 10: value = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t); break;
        case 11: value = (t >= 0.999f ? 1.0f : std::floor (t * 4.0f) / 4.0f); break;
        case 12:
        {
            constexpr float steps = 4.0f;
            const float stepped = juce::jmin (1.0f, std::floor (t * steps) / steps);
            const float local = std::fmod (t * steps, 1.0f);
            value = juce::jmin (1.0f, stepped + (local * local * (3.0f - 2.0f * local)) / steps);
            if (t >= 0.999f) value = 1.0f;
            break;
        }
        case 13: value = (t < 0.82f ? 0.0f : (t - 0.82f) / 0.18f); break;
        case 14: value = juce::jlimit (0.0f, 1.0f, t + 0.13f * std::sin (juce::MathConstants<float>::twoPi * t)); break;
        default: break;
    }

    if (curve != 7 && curve != 11 && curve != 12 && curve != 13 && curve != 14
        && std::abs (tension) > 0.0005f)
    {
        const float exponent = std::pow (2.0f, -2.0f * juce::jlimit (-1.0f, 1.0f, tension));
        value = std::pow (juce::jlimit (0.000001f, 1.0f, value), exponent);
    }

    return juce::jlimit (0.0f, 1.0f, value);
}

juce::String LayerEnvelopeComponent::curveParameterId (int stage) const
{
    if (stage == AttackStage) return ids::layerAttackCurve (layer);
    if (stage == DecayStage) return ids::layerDecayCurve (layer);
    return ids::layerReleaseCurve (layer);
}

juce::String LayerEnvelopeComponent::tensionParameterId (int stage) const
{
    if (stage == AttackStage) return ids::layerAttackTension (layer);
    if (stage == DecayStage) return ids::layerDecayTension (layer);
    return ids::layerReleaseTension (layer);
}

int LayerEnvelopeComponent::curveForStage (int stage) const
{
    if (stage == AttackStage) return juce::roundToInt (pAttackCurve->load());
    if (stage == DecayStage) return juce::roundToInt (pDecayCurve->load());
    return juce::roundToInt (pReleaseCurve->load());
}

float LayerEnvelopeComponent::tensionForStage (int stage) const
{
    if (stage == AttackStage) return pAttackTension->load();
    if (stage == DecayStage) return pDecayTension->load();
    return pReleaseTension->load();
}

void LayerEnvelopeComponent::setCurveForStage (int stage, int curveIndex)
{
    if (stage < AttackStage || stage > ReleaseStage)
        return;

    if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
            processor.apvts.getParameter (curveParameterId (stage))))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) curveIndex));
        parameter->endChangeGesture();
    }
}

LayerEnvelopeComponent::Geometry LayerEnvelopeComponent::getGeometry (float durationMs) const
{
    Geometry result;
    auto bounds = getLocalBounds().toFloat().reduced (8.0f);
    bounds.removeFromTop (21.0f);
    bounds.removeFromBottom (17.0f + 23.0f + 23.0f + 2.0f + 23.0f + 6.0f);
    result.plot = bounds;
    result.durationMs = juce::jmax (1.0f, durationMs);

    const float attack = juce::jmax (0.1f, pAttack->load());
    const float hold = juce::jmax (0.0f, pHold->load());
    const float decay = juce::jmax (0.1f, pDecay->load());
    const float release = juce::jmin (pRelease->load(), result.durationMs * 0.5f);
    const float releaseStart = juce::jmax (0.0f, result.durationMs - release);
    const float attackEnd = juce::jmin (attack, releaseStart);
    const float holdEnd = juce::jmin (attack + hold, releaseStart);
    const float decayEnd = juce::jmin (attack + hold + decay, releaseStart);
    const float sustain = juce::jlimit (0.0f, 1.0f, pSustain->load());

    auto xAt = [&] (float milliseconds)
    {
        return result.plot.getX() + result.plot.getWidth()
               * juce::jlimit (0.0f, result.durationMs, milliseconds) / result.durationMs;
    };
    auto yAt = [&] (float level)
    {
        return result.plot.getBottom() - result.plot.getHeight() * juce::jlimit (0.0f, 1.0f, level);
    };

    const int attackCurve = curveForStage (AttackStage);
    const int decayCurve = curveForStage (DecayStage);
    const int releaseCurve = curveForStage (ReleaseStage);
    const float attackTension = tensionForStage (AttackStage);
    const float decayTension = tensionForStage (DecayStage);
    const float releaseTension = tensionForStage (ReleaseStage);

    auto levelAt = [&] (float milliseconds)
    {
        if (milliseconds < attack)
            return curveValue (milliseconds / juce::jmax (0.1f, attack), attackCurve, attackTension);
        if (milliseconds < attack + hold)
            return 1.0f;
        const float decayProgress = (milliseconds - attack - hold) / decay;
        if (decayProgress < 1.0f)
            return 1.0f + (sustain - 1.0f) * curveValue (decayProgress, decayCurve, decayTension);
        return sustain;
    };

    result.attackEndMs = attackEnd;
    result.holdEndMs = holdEnd;
    result.decayEndMs = decayEnd;
    result.releaseStartMs = releaseStart;
    result.sustainLevel = sustain;
    result.attack = { xAt (attackEnd), yAt (levelAt (attackEnd)) };
    result.hold = { xAt (holdEnd), yAt (levelAt (holdEnd)) };
    result.decay = { xAt (decayEnd), yAt (levelAt (decayEnd)) };
    const float sustainMid = decayEnd + (releaseStart - decayEnd) * 0.5f;
    result.sustain = { xAt (sustainMid), yAt (sustain) };
    result.release = { xAt (releaseStart), yAt (levelAt (releaseStart)) };

    const float attackMid = attackEnd * 0.5f;
    result.attackTension = { xAt (attackMid),
        yAt (curveValue (0.5f, attackCurve, attackTension)) };

    const float decaySpan = juce::jmax (0.0f, decayEnd - holdEnd);
    const float decayMidLevel = 1.0f + (sustain - 1.0f)
                                * curveValue (0.5f, decayCurve, decayTension);
    result.decayTension = { xAt (holdEnd + decaySpan * 0.5f), yAt (decayMidLevel) };

    const float releaseSpan = juce::jmax (0.0f, result.durationMs - releaseStart);
    const float releaseMidLevel = sustain
        * (1.0f - curveValue (0.5f, releaseCurve, releaseTension));
    result.releaseTension = { xAt (releaseStart + releaseSpan * 0.5f), yAt (releaseMidLevel) };

    return result;
}

float LayerEnvelopeComponent::timeFromX (float x, const Geometry& geometry) const noexcept
{
    return juce::jlimit (0.0f, geometry.durationMs,
        (x - geometry.plot.getX()) / juce::jmax (1.0f, geometry.plot.getWidth()) * geometry.durationMs);
}

float LayerEnvelopeComponent::snapTime (float milliseconds, const Geometry& geometry) const noexcept
{
    if (! snapEnabled)
        return milliseconds;

    const float grid = juce::jmax (0.1f, geometry.durationMs / 16.0f);
    return juce::jlimit (0.0f, geometry.durationMs, std::round (milliseconds / grid) * grid);
}

void LayerEnvelopeComponent::drawPoint (juce::Graphics& g,
                                        juce::Point<float> point,
                                        const juce::String& label,
                                        bool selected) const
{
    g.setColour (juce::Colour (0xff111820));
    g.fillEllipse (point.x - kHandleRadius - 1.3f, point.y - kHandleRadius - 1.3f,
                   2.0f * (kHandleRadius + 1.3f), 2.0f * (kHandleRadius + 1.3f));
    g.setColour (selected ? juce::Colours::white : colour);
    g.fillEllipse (point.x - kHandleRadius, point.y - kHandleRadius,
                   2.0f * kHandleRadius, 2.0f * kHandleRadius);
    g.setColour (juce::Colour (0xff111820));
    g.setFont (juce::Font (7.5f, juce::Font::bold));
    g.drawText (label, juce::Rectangle<int> ((int) point.x - 7, (int) point.y - 5, 14, 10),
                juce::Justification::centred);
}

void LayerEnvelopeComponent::drawEnvelopeCurve (juce::Graphics& g, const Geometry& geometry) const
{
    const auto plot = geometry.plot;
    const float duration = geometry.durationMs;
    const float attack = pAttack->load();
    const float hold = pHold->load();
    const float decay = pDecay->load();
    const float sustain = pSustain->load();
    const float release = juce::jmin (pRelease->load(), duration * 0.5f);
    const float releaseStart = juce::jmax (0.0f, duration - release);

    const int attackCurve = curveForStage (AttackStage);
    const int decayCurve = curveForStage (DecayStage);
    const int releaseCurve = curveForStage (ReleaseStage);

    auto xAt = [&] (float milliseconds)
    {
        return plot.getX() + plot.getWidth()
               * juce::jlimit (0.0f, duration, milliseconds) / duration;
    };
    auto yAt = [&] (float level)
    {
        return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, level);
    };

    juce::Path envelope;
    envelope.startNewSubPath (xAt (0.0f), yAt (0.0f));

    auto appendCurve = [&] (float startMs, float endMs, float startLevel, float endLevel,
                            int curve, float tension)
    {
        for (int i = 1; i <= kCurveSteps; ++i)
        {
            const float t = (float) i / (float) kCurveSteps;
            const float level = startLevel + (endLevel - startLevel) * curveValue (t, curve, tension);
            envelope.lineTo (xAt (startMs + (endMs - startMs) * t), yAt (level));
        }
    };

    const float attackEnd = juce::jmin (attack, releaseStart);
    appendCurve (0.0f, attackEnd, 0.0f, 1.0f, attackCurve, tensionForStage (AttackStage));

    const float holdEnd = juce::jmin (attack + hold, releaseStart);
    envelope.lineTo (xAt (holdEnd), yAt (1.0f));

    const float decayEnd = juce::jmin (attack + hold + decay, releaseStart);
    appendCurve (holdEnd, decayEnd, 1.0f, sustain, decayCurve, tensionForStage (DecayStage));

    envelope.lineTo (xAt (releaseStart), yAt (sustain));
    appendCurve (releaseStart, duration, sustain, 0.0f,
                 releaseCurve, tensionForStage (ReleaseStage));

    auto fill = envelope;
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();

    const bool enabled = pEnvelopeEnabled->load() > 0.5f;
    g.setColour (colour.withAlpha (enabled ? 0.12f : 0.035f));
    g.fillPath (fill);
    g.setColour (colour.withAlpha (enabled ? 0.98f : 0.35f));
    g.strokePath (envelope, juce::PathStrokeType (2.0f));

    // Vertical section markers and the draggable tension handles.
    const float markers[] = { geometry.attackEndMs, geometry.holdEndMs,
                              geometry.decayEndMs, geometry.releaseStartMs };
    for (int i = 0; i < 4; ++i)
    {
        const float x = xAt (markers[i]);
        g.setColour (colour.withAlpha (0.22f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
    }

    auto drawTension = [&] (juce::Point<float> point, int stage)
    {
        const bool active = selectedStage == stage;
        g.setColour (colour.withAlpha (0.9f));
        g.drawLine (point.x, point.y - 11.0f, point.x, point.y + 11.0f, 0.9f);
        g.setColour (juce::Colour (0xff101820));
        g.fillEllipse (point.x - 4.6f, point.y - 4.6f, 9.2f, 9.2f);
        g.setColour (active ? juce::Colours::white : colour.brighter (0.2f));
        g.drawEllipse (point.x - 4.2f, point.y - 4.2f, 8.4f, 8.4f, 1.2f);
        g.drawLine (point.x - 2.0f, point.y, point.x + 2.0f, point.y, 1.0f);
    };

    drawTension (geometry.attackTension, AttackStage);
    drawTension (geometry.decayTension, DecayStage);
    drawTension (geometry.releaseTension, ReleaseStage);

    drawPoint (g, geometry.attack, "A", activeHandle == Handle::Attack);
    drawPoint (g, geometry.hold, "H", activeHandle == Handle::Hold);
    drawPoint (g, geometry.decay, "D", activeHandle == Handle::Decay);
    drawPoint (g, geometry.sustain, "S", activeHandle == Handle::Sustain);
    drawPoint (g, geometry.release, "R", activeHandle == Handle::Release);
}

void LayerEnvelopeComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff171d23));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (colour.withAlpha (0.38f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    auto header = getLocalBounds().reduced (10, 0).removeFromTop (20).toFloat();
    g.setColour (kGraphText);
    g.setFont (juce::Font (11.0f, juce::Font::bold));
    g.drawText ("ENVELOPE  /  " + juce::String (layer < 3 ? "PRE " : "MAIN ")
                + juce::String (layer < 3 ? layer + 1 : layer - 2),
                header.removeFromLeft (175.0f).toNearestInt(), juce::Justification::centredLeft);
    g.setColour (kGraphMuted);
    g.setFont (juce::Font (9.0f));
    const auto readout = "A " + formatEnvelopeTime (pAttack->load())
        + "   H " + formatEnvelopeTime (pHold->load())
        + "   D " + formatEnvelopeTime (pDecay->load())
        + "   S " + juce::String (pSustain->load() * 100.0f, 0) + "%"
        + "   R " + formatEnvelopeTime (pRelease->load());
    g.drawText (readout, header.toNearestInt(), juce::Justification::centredRight);

    const auto preview = processor.getWaveformPreview (layer);
    const auto geometry = getGeometry (preview.loaded ? (float) preview.durationMs : 1000.0f);
    const auto plot = geometry.plot;
    if (plot.getWidth() < 24.0f || plot.getHeight() < 24.0f)
        return;

    g.setColour (kGraphBackground);
    g.fillRoundedRectangle (plot, 3.0f);
    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    g.setColour (kGraphGrid.withAlpha (0.34f));
    for (int i = 1; i < 8; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * (float) i / 8.0f;
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
    }
    for (int i = 1; i < 4; ++i)
    {
        const float y = plot.getY() + plot.getHeight() * (float) i / 4.0f;
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    }
    g.setColour (kGraphGrid.withAlpha (0.55f));
    g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());

    if (preview.loaded)
    {
        const float centre = plot.getCentreY();
        const float amplitude = plot.getHeight() * 0.43f;
        g.setColour (colour.withAlpha (0.42f));
        for (int i = 0; i < LayeredSampleEngine::kPreviewPoints; ++i)
        {
            const float x = plot.getX() + ((float) i + 0.5f) * plot.getWidth()
                            / (float) LayeredSampleEngine::kPreviewPoints;
            const float top = centre - preview.maximum[(size_t) i] * amplitude;
            const float bottom = centre - preview.minimum[(size_t) i] * amplitude;
            g.drawVerticalLine (juce::roundToInt (x), top, bottom);
        }
    }
    else
    {
        g.setColour (kGraphMuted);
        g.setFont (juce::Font (11.0f));
        g.drawText ("Load a sample to view its waveform", plot.toNearestInt(),
                    juce::Justification::centred);
    }

    drawEnvelopeCurve (g, geometry);
    g.restoreState();

    // FL-style ADSR section markers and names are kept outside the plot clipping area.
    auto sectionRow = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 1.0f,
                                               plot.getWidth(), 15.0f);
    const float xA = plot.getX() + plot.getWidth() * geometry.attackEndMs / geometry.durationMs;
    const float xH = plot.getX() + plot.getWidth() * geometry.holdEndMs / geometry.durationMs;
    const float xD = plot.getX() + plot.getWidth() * geometry.decayEndMs / geometry.durationMs;
    const float xR = plot.getX() + plot.getWidth() * geometry.releaseStartMs / geometry.durationMs;
    const float edges[] = { plot.getX(), xA, xH, xD, xR, plot.getRight() };
    const juce::String labels[] = { "ATTACK", "HOLD", "DECAY", "SUSTAIN", "RELEASE" };
    for (int i = 0; i < 5; ++i)
    {
        auto segment = juce::Rectangle<float> (edges[i], sectionRow.getY(),
                                                juce::jmax (0.0f, edges[i + 1] - edges[i]),
                                                sectionRow.getHeight());
        g.setColour (colour.withAlpha (i % 2 == 0 ? 0.15f : 0.08f));
        g.fillRect (segment);
        g.setColour (kGraphText.withAlpha (0.88f));
        g.setFont (juce::Font (8.0f, juce::Font::bold));
        g.drawFittedText (labels[i], segment.reduced (2.0f, 0.0f).toNearestInt(),
                          juce::Justification::centred, 1);
    }

    if (! pEnvelopeEnabled->load())
    {
        g.setColour (juce::Colours::white.withAlpha (0.72f));
        g.setFont (juce::Font (14.0f, juce::Font::bold));
        g.drawText ("ENVELOPE BYPASSED", plot.toNearestInt(), juce::Justification::centred);
    }

    auto drawControlLabel = [&] (const juce::Slider& slider, const juce::String& label)
    {
        const auto r = slider.getBounds();
        g.setColour (kGraphMuted);
        g.setFont (juce::Font (7.5f, juce::Font::bold));
        g.drawText (label, r.getX(), r.getY() - 7, r.getWidth(), 7,
                    juce::Justification::centredLeft);
    };
    drawControlLabel (panSlider, "PAN");
    drawControlLabel (pitchSlider, "PITCH");
    drawControlLabel (highPassSlider, "HPF");
    drawControlLabel (lowPassSlider, "LPF");
}

void LayerEnvelopeComponent::resized()
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (21);

    auto toolRow = area.removeFromBottom (23);
    area.removeFromBottom (4);
    auto filterRow = area.removeFromBottom (23);
    area.removeFromBottom (2);
    auto pitchRow = area.removeFromBottom (23);

    const int toolWidths[] = { 57, 45, 62, 48, 48, 48, 34 };
    juce::TextButton* tools[] = { &enableButton, &undoButton, &freezeButton,
                                  &drawButton, &snapButton, &slideButton, &envelopeMenuButton };
    for (int i = 0; i < 7; ++i)
    {
        tools[i]->setBounds (toolRow.removeFromLeft (toolWidths[i]).withHeight (22));
        if (i != 6)
            toolRow.removeFromLeft (3);
    }
    muteButton.setBounds (toolRow.removeFromRight (19).withHeight (22));

    auto placePair = [] (juce::Rectangle<int> row, juce::Slider& first, juce::Slider& second)
    {
        constexpr int gap = 8;
        const int cellWidth = (row.getWidth() - gap) / 2;
        first.setBounds (row.removeFromLeft (cellWidth).withTrimmedTop (7).withHeight (16));
        row.removeFromLeft (gap);
        second.setBounds (row.withTrimmedTop (7).withHeight (16));
    };

    placePair (pitchRow, panSlider, pitchSlider);
    placePair (filterRow, highPassSlider, lowPassSlider);
}

LayerEnvelopeComponent::Snapshot LayerEnvelopeComponent::captureSnapshot() const
{
    Snapshot s;
    s.attack = pAttack->load();
    s.hold = pHold->load();
    s.decay = pDecay->load();
    s.sustain = pSustain->load();
    s.release = pRelease->load();
    s.attackCurve = curveForStage (AttackStage);
    s.decayCurve = curveForStage (DecayStage);
    s.releaseCurve = curveForStage (ReleaseStage);
    s.attackTension = tensionForStage (AttackStage);
    s.decayTension = tensionForStage (DecayStage);
    s.releaseTension = tensionForStage (ReleaseStage);
    s.enabled = pEnvelopeEnabled->load() > 0.5f;
    return s;
}

void LayerEnvelopeComponent::pushUndo()
{
    undoStack.push_back (captureSnapshot());
    if ((int) undoStack.size() > kMaximumUndoStates)
        undoStack.erase (undoStack.begin());
    undoButton.setEnabled (! undoStack.empty());
}

void LayerEnvelopeComponent::restoreSnapshot (const Snapshot& s)
{
    setParameter (ids::layerAttack (layer), s.attack);
    setParameter (ids::layerHold (layer), s.hold);
    setParameter (ids::layerDecay (layer), s.decay);
    setParameter (ids::layerSustain (layer), s.sustain);
    setParameter (ids::layerRelease (layer), s.release);
    setParameter (ids::layerAttackCurve (layer), (float) s.attackCurve);
    setParameter (ids::layerDecayCurve (layer), (float) s.decayCurve);
    setParameter (ids::layerReleaseCurve (layer), (float) s.releaseCurve);
    setParameter (ids::layerAttackTension (layer), s.attackTension);
    setParameter (ids::layerDecayTension (layer), s.decayTension);
    setParameter (ids::layerReleaseTension (layer), s.releaseTension);
    setParameter (ids::layerEnvelopeEnabled (layer), s.enabled ? 1.0f : 0.0f);
    repaint();
}

void LayerEnvelopeComponent::undo()
{
    if (undoStack.empty())
        return;

    const auto previous = undoStack.back();
    undoStack.pop_back();
    restoreSnapshot (previous);
    undoButton.setEnabled (! undoStack.empty());
}

void LayerEnvelopeComponent::setParameter (const juce::String& parameterId, float value)
{
    if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
            processor.apvts.getParameter (parameterId)))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

int LayerEnvelopeComponent::stageForHandle (Handle handle) const
{
    switch (handle)
    {
        case Handle::Attack:
        case Handle::AttackTension: return AttackStage;
        case Handle::Decay:
        case Handle::Hold:
        case Handle::Sustain:
        case Handle::DecayTension: return DecayStage;
        case Handle::Release:
        case Handle::ReleaseTension: return ReleaseStage;
        default: return selectedStage;
    }
}

LayerEnvelopeComponent::Handle LayerEnvelopeComponent::nearestHandle (juce::Point<float> position,
                                                                       float maxDistance) const
{
    const auto preview = processor.getWaveformPreview (layer);
    const auto geometry = getGeometry (preview.loaded ? (float) preview.durationMs : 1000.0f);
    const juce::Point<float> points[] = {
        geometry.attack, geometry.hold, geometry.decay, geometry.sustain, geometry.release,
        geometry.attackTension, geometry.decayTension, geometry.releaseTension
    };
    const Handle types[] = {
        Handle::Attack, Handle::Hold, Handle::Decay, Handle::Sustain, Handle::Release,
        Handle::AttackTension, Handle::DecayTension, Handle::ReleaseTension
    };

    float best = maxDistance;
    Handle result = Handle::None;
    for (int i = 0; i < 8; ++i)
    {
        const float distance = position.getDistanceFrom (points[i]);
        if (distance < best)
        {
            best = distance;
            result = types[i];
        }
    }
    return result;
}

void LayerEnvelopeComponent::beginGesture (Handle handle)
{
    activeHandle = handle;
    selectedStage = stageForHandle (handle);
    pushUndo();

    juce::String parameterId;
    switch (handle)
    {
        case Handle::Attack: parameterId = ids::layerAttack (layer); break;
        case Handle::Hold: parameterId = ids::layerHold (layer); break;
        case Handle::Decay: parameterId = ids::layerDecay (layer); break;
        case Handle::Sustain: parameterId = ids::layerSustain (layer); break;
        case Handle::Release: parameterId = ids::layerRelease (layer); break;
        case Handle::AttackTension: parameterId = ids::layerAttackTension (layer); break;
        case Handle::DecayTension: parameterId = ids::layerDecayTension (layer); break;
        case Handle::ReleaseTension: parameterId = ids::layerReleaseTension (layer); break;
        default: break;
    }

    if (parameterId.isNotEmpty())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
                processor.apvts.getParameter (parameterId)))
            parameter->beginChangeGesture();
}

void LayerEnvelopeComponent::endGesture()
{
    if (activeHandle == Handle::None)
        return;

    juce::String parameterId;
    switch (activeHandle)
    {
        case Handle::Attack: parameterId = ids::layerAttack (layer); break;
        case Handle::Hold: parameterId = ids::layerHold (layer); break;
        case Handle::Decay: parameterId = ids::layerDecay (layer); break;
        case Handle::Sustain: parameterId = ids::layerSustain (layer); break;
        case Handle::Release: parameterId = ids::layerRelease (layer); break;
        case Handle::AttackTension: parameterId = ids::layerAttackTension (layer); break;
        case Handle::DecayTension: parameterId = ids::layerDecayTension (layer); break;
        case Handle::ReleaseTension: parameterId = ids::layerReleaseTension (layer); break;
        default: break;
    }

    if (parameterId.isNotEmpty())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
                processor.apvts.getParameter (parameterId)))
            parameter->endChangeGesture();

    activeHandle = Handle::None;
}

void LayerEnvelopeComponent::applyDrag (juce::Point<float> position)
{
    const auto preview = processor.getWaveformPreview (layer);
    const auto geometry = getGeometry (preview.loaded ? (float) preview.durationMs : 1000.0f);
    const float duration = juce::jmax (1.0f, geometry.durationMs);
    const float timeMs = snapTime (timeFromX (position.x, geometry), geometry);

    switch (activeHandle)
    {
        case Handle::Attack:
        {
            const float oldAttack = pAttack->load();
            const float nextAttack = juce::jlimit (0.1f, 2000.0f, timeMs);
            const float delta = nextAttack - oldAttack;
            setParameter (ids::layerAttack (layer), nextAttack);
            if (slidePoints)
            {
                setParameter (ids::layerHold (layer), juce::jlimit (0.0f, 2000.0f, pHold->load() - delta));
                setParameter (ids::layerDecay (layer), juce::jlimit (0.1f, 5000.0f, pDecay->load()));
            }
            break;
        }
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
            const float level = juce::jlimit (0.0f, 1.0f,
                (geometry.plot.getBottom() - position.y)
                    / juce::jmax (1.0f, geometry.plot.getHeight()));
            setParameter (ids::layerSustain (layer), level);
            break;
        }
        case Handle::Release:
            setParameter (ids::layerRelease (layer), juce::jlimit (0.1f, 10000.0f, duration - timeMs));
            break;
        case Handle::AttackTension:
        case Handle::DecayTension:
        case Handle::ReleaseTension:
        {
            const float level = juce::jlimit (0.0f, 1.0f,
                (geometry.plot.getBottom() - position.y)
                    / juce::jmax (1.0f, geometry.plot.getHeight()));
            float desired = 0.5f;
            if (activeHandle == Handle::AttackTension)
                desired = level;
            else if (activeHandle == Handle::DecayTension)
                desired = (1.0f - level) / juce::jmax (0.001f, 1.0f - pSustain->load());
            else
                desired = (pSustain->load() - level) / juce::jmax (0.001f, pSustain->load());

            const int stage = stageForHandle (activeHandle);
            const float neutral = curveValue (0.5f, curveForStage (stage), 0.0f);
            const float tension = juce::jlimit (-1.0f, 1.0f, (desired - neutral) * 2.0f);
            setParameter (tensionParameterId (stage), tension);
            break;
        }
        default:
            break;
    }

    repaint();
}

void LayerEnvelopeComponent::showPointMenu (int stage)
{
    stage = juce::jlimit (AttackStage, ReleaseStage, stage);
    selectedStage = stage;

    static const char* modeNames[] = {
        "Linear", "Convex", "Concave", "S-Curve", "Exponential", "Logarithmic", "Bezier",
        "Hold", "Smooth", "Single curve", "Half sine", "Stairs", "Smooth stairs", "Pulse", "Wave"
    };

    juce::PopupMenu modeMenu;
    const int currentCurve = curveForStage (stage);
    for (int i = 0; i < 15; ++i)
        modeMenu.addItem (100 + i, modeNames[i], true, currentCurve == i);

    juce::PopupMenu sectionMenu;
    sectionMenu.addItem (200, "Attack", true, stage == AttackStage);
    sectionMenu.addItem (201, "Decay", true, stage == DecayStage);
    sectionMenu.addItem (202, "Release", true, stage == ReleaseStage);

    juce::PopupMenu menu;
    menu.addItem (1, "Delete curve edit");
    menu.addSubMenu ("Mode", modeMenu);
    menu.addSectionHeader ("ADSR section");
    menu.addSubMenu ("Select section", sectionMenu);
    menu.showMenuAsync (juce::PopupMenu::Options(), [this, stage] (int result)
    {
        if (result == 1)
        {
            pushUndo();
            setCurveForStage (stage, 0);
            setParameter (tensionParameterId (stage), 0.0f);
        }
        else if (result >= 100 && result < 115)
        {
            pushUndo();
            setCurveForStage (stage, result - 100);
        }
        else if (result >= 200 && result <= 202)
        {
            selectedStage = result - 200;
        }
        repaint();
    });
}

void LayerEnvelopeComponent::savePreset()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Save envelope preset",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("WaveLens-Envelope.wlenv"),
        "*.wlenv");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::canSelectFiles
                          | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (! file.getFullPathName().isEmpty())
            {
                const auto s = captureSnapshot();
                const juce::String data =
                    juce::String (s.attack, 6) + "," + juce::String (s.hold, 6) + ","
                    + juce::String (s.decay, 6) + "," + juce::String (s.sustain, 6) + ","
                    + juce::String (s.release, 6) + "," + juce::String (s.attackCurve) + ","
                    + juce::String (s.decayCurve) + "," + juce::String (s.releaseCurve) + ","
                    + juce::String (s.attackTension, 6) + "," + juce::String (s.decayTension, 6) + ","
                    + juce::String (s.releaseTension, 6) + "," + juce::String (s.enabled ? 1 : 0);
                file.replaceWithText (data);
            }
        });
}

void LayerEnvelopeComponent::loadPreset()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Load envelope preset",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.wlenv");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (! file.existsAsFile())
                return;

            juce::StringArray tokens;
            tokens.addTokens (file.loadFileAsString(), ",", "\"");
            tokens.trim();
            tokens.removeEmptyStrings();
            if (tokens.size() < 12)
                return;

            pushUndo();
            setParameter (ids::layerAttack (layer), tokens[0].getFloatValue());
            setParameter (ids::layerHold (layer), tokens[1].getFloatValue());
            setParameter (ids::layerDecay (layer), tokens[2].getFloatValue());
            setParameter (ids::layerSustain (layer), tokens[3].getFloatValue());
            setParameter (ids::layerRelease (layer), tokens[4].getFloatValue());
            setParameter (ids::layerAttackCurve (layer), (float) tokens[5].getIntValue());
            setParameter (ids::layerDecayCurve (layer), (float) tokens[6].getIntValue());
            setParameter (ids::layerReleaseCurve (layer), (float) tokens[7].getIntValue());
            setParameter (ids::layerAttackTension (layer), tokens[8].getFloatValue());
            setParameter (ids::layerDecayTension (layer), tokens[9].getFloatValue());
            setParameter (ids::layerReleaseTension (layer), tokens[10].getFloatValue());
            setParameter (ids::layerEnvelopeEnabled (layer), tokens[11].getIntValue() != 0 ? 1.0f : 0.0f);
            repaint();
        });
}

void LayerEnvelopeComponent::showEnvelopeMenu()
{
    juce::PopupMenu tools;
    tools.addItem (1, "Undo last edit", ! undoStack.empty());
    tools.addItem (2, "Reset curve tensions");
    tools.addItem (3, "Reset envelope to default");

    juce::PopupMenu menu;
    menu.addItem (10, "Save preset...");
    menu.addItem (11, "Load preset...");
    menu.addSeparator();
    menu.addSubMenu ("Tools", tools);
    menu.showMenuAsync (juce::PopupMenu::Options(), [this] (int result)
    {
        if (result == 10) savePreset();
        else if (result == 11) loadPreset();
        else if (result == 1) undo();
        else if (result == 2)
        {
            pushUndo();
            setParameter (ids::layerAttackTension (layer), 0.0f);
            setParameter (ids::layerDecayTension (layer), 0.0f);
            setParameter (ids::layerReleaseTension (layer), 0.0f);
            repaint();
        }
        else if (result == 3)
        {
            pushUndo();
            restoreSnapshot (Snapshot {});
        }
    });
}

void LayerEnvelopeComponent::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        const auto handle = nearestHandle (event.position, 28.0f);
        const int stage = handle == Handle::None ? selectedStage : stageForHandle (handle);
        showPointMenu (stage);
        return;
    }

    if (frozen)
        return;

    endGesture();
    auto handle = nearestHandle (event.position, 18.0f);
    if (handle == Handle::None && drawMode)
    {
        const auto preview = processor.getWaveformPreview (layer);
        const auto geometry = getGeometry (preview.loaded ? (float) preview.durationMs : 1000.0f);
        const float timeMs = timeFromX (event.position.x, geometry);
        if (timeMs <= geometry.attackEndMs)
            handle = Handle::AttackTension;
        else if (timeMs <= geometry.decayEndMs)
            handle = Handle::DecayTension;
        else
            handle = Handle::ReleaseTension;
    }

    if (handle != Handle::None)
        beginGesture (handle);
}

void LayerEnvelopeComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (activeHandle != Handle::None && ! frozen)
        applyDrag (event.position);
}

void LayerEnvelopeComponent::mouseUp (const juce::MouseEvent&)
{
    endGesture();
}
