#include "LayerEnvelopeComponent.h"

#include <cmath>

namespace
{
    constexpr float kHandleRadius = 4.5f;
    constexpr int kCurveSteps = 28;

    juce::String formatEnvelopeTime (float value)
    {
        if (value >= 1000.0f)
            return juce::String (value / 1000.0f, 2) + "s";
        return juce::String (value, value < 10.0f ? 1 : 0) + "ms";
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

    const juce::StringArray shortNames { "Lin", "Conv", "Conc", "S", "Exp", "Log", "Bez" };

    auto prepareCurveBox = [&] (juce::ComboBox& box, const juce::String& tooltip)
    {
        box.addItemList (shortNames, 1);
        box.setTooltip (tooltip);
        box.setJustificationType (juce::Justification::centred);
        box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1b2229));
        box.setColour (juce::ComboBox::textColourId, juce::Colour (0xffe7edf2));
        box.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff47525d));
        box.setColour (juce::ComboBox::arrowColourId, colour);
        addAndMakeVisible (box);
    };

    prepareCurveBox (attackCurveBox, "Attack: Linear, convex, concave, S-curve, exponential, logarithmic or Bezier");
    prepareCurveBox (decayCurveBox, "Decay: Linear, convex, concave, S-curve, exponential, logarithmic or Bezier");
    prepareCurveBox (releaseCurveBox, "Release: Linear, convex, concave, S-curve, exponential, logarithmic or Bezier");

    attackCurveAttachment = std::make_unique<ComboAtt> (state, ids::layerAttackCurve (layer), attackCurveBox);
    decayCurveAttachment = std::make_unique<ComboAtt> (state, ids::layerDecayCurve (layer), decayCurveBox);
    releaseCurveAttachment = std::make_unique<ComboAtt> (state, ids::layerReleaseCurve (layer), releaseCurveBox);

    auto prepareSlider = [&] (juce::Slider& slider, const juce::String& tooltip)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 31, 14);
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

    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setOpaque (false);
    startTimerHz (15);
}

LayerEnvelopeComponent::~LayerEnvelopeComponent()
{
    stopTimer();
    endGesture();
}

float LayerEnvelopeComponent::curveValue (float progress, int curve) noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, progress);
    switch (juce::jlimit (0, 6, curve))
    {
        case 0: return t;
        case 1: return t * t;
        case 2: return 1.0f - (1.0f - t) * (1.0f - t);
        case 3: return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        case 4: return (std::exp (5.0f * t) - 1.0f) / (std::exp (5.0f) - 1.0f);
        case 5: return std::log1p (9.0f * t) / std::log (10.0f);
        case 6:
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
            return 3.0f * oneMinus * oneMinus * u * 0.02f
                   + 3.0f * oneMinus * u * u * 0.98f + u * u * u;
        }
        default: return t;
    }
}

LayerEnvelopeComponent::Geometry LayerEnvelopeComponent::getGeometry (float durationMs) const
{
    Geometry result;
    auto bounds = getLocalBounds().toFloat().reduced (5.0f);
    bounds.removeFromTop (15.0f);
    bounds.removeFromBottom (72.0f);
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

    auto xAt = [&] (float ms)
    {
        return result.plot.getX() + result.plot.getWidth() * ms / result.durationMs;
    };
    auto yAt = [&] (float level)
    {
        return result.plot.getBottom() - result.plot.getHeight() * juce::jlimit (0.0f, 1.0f, level);
    };
    auto levelAt = [&] (float ms)
    {
        if (ms < attack)
            return curveValue (ms / juce::jmax (0.1f, attack),
                               juce::roundToInt (pAttackCurve->load()));
        if (ms < attack + hold)
            return 1.0f;
        const float decayProgress = (ms - attack - hold) / decay;
        if (decayProgress < 1.0f)
            return 1.0f + (sustain - 1.0f)
                * curveValue (decayProgress, juce::roundToInt (pDecayCurve->load()));
        return sustain;
    };

    result.attack = { xAt (attackEnd), yAt (levelAt (attackEnd)) };
    result.hold = { xAt (holdEnd), yAt (levelAt (holdEnd)) };
    result.decay = { xAt (decayEnd), yAt (levelAt (decayEnd)) };
    const float sustainMid = decayEnd + (releaseStart - decayEnd) * 0.5f;
    result.sustain = { xAt (sustainMid), yAt (sustain) };
    result.release = { xAt (releaseStart), yAt (levelAt (releaseStart)) };
    return result;
}

void LayerEnvelopeComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff171d23));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (colour.withAlpha (0.35f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    const auto preview = processor.getWaveformPreview (layer);
    const float duration = preview.loaded ? (float) preview.durationMs : 1000.0f;
    const auto geometry = getGeometry (duration);
    const auto plot = geometry.plot;
    if (plot.getWidth() < 24.0f || plot.getHeight() < 24.0f)
        return;

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    g.setColour (juce::Colours::white.withAlpha (0.055f));
    for (int i = 1; i < 4; ++i)
    {
        const float y = plot.getY() + plot.getHeight() * (float) i / 4.0f;
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        const float x = plot.getX() + plot.getWidth() * (float) i / 4.0f;
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
    }
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());

    // Draw the loaded sample's actual peak envelope before the editable AHDSR curve.
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
        g.setColour (juce::Colour (0xff65717b));
        g.setFont (juce::Font (10.0f));
        g.drawText ("Load a sample to view its waveform", plot.toNearestInt(),
                    juce::Justification::centred);
    }

    const float attack = pAttack->load();
    const float hold = pHold->load();
    const float decay = pDecay->load();
    const float sustain = pSustain->load();
    const float release = juce::jmin (pRelease->load(), duration * 0.5f);
    const float releaseStart = juce::jmax (0.0f, duration - release);
    const int attackCurve = juce::roundToInt (pAttackCurve->load());
    const int decayCurve = juce::roundToInt (pDecayCurve->load());
    const int releaseCurve = juce::roundToInt (pReleaseCurve->load());

    auto envelopeLevel = [&] (float timeMs)
    {
        if (timeMs < attack)
            return curveValue (timeMs / juce::jmax (0.1f, attack), attackCurve);
        if (timeMs < attack + hold)
            return 1.0f;
        const float p = (timeMs - attack - hold) / juce::jmax (0.1f, decay);
        if (p < 1.0f)
            return 1.0f + (sustain - 1.0f) * curveValue (p, decayCurve);
        return sustain;
    };
    auto xAt = [&] (float timeMs)
    {
        return plot.getX() + plot.getWidth() * juce::jlimit (0.0f, duration, timeMs) / duration;
    };
    auto yAt = [&] (float level)
    {
        return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, level);
    };

    juce::Path envelope;
    envelope.startNewSubPath (xAt (0.0f), yAt (0.0f));

    const float attackEnd = juce::jmin (attack, releaseStart);
    for (int i = 1; i <= kCurveSteps; ++i)
    {
        const float t = (float) i / (float) kCurveSteps;
        const float timeMs = attackEnd * t;
        envelope.lineTo (xAt (timeMs), yAt (envelopeLevel (timeMs)));
    }

    const float holdEnd = juce::jmin (attack + hold, releaseStart);
    envelope.lineTo (xAt (holdEnd), yAt (envelopeLevel (holdEnd)));

    const float decayEnd = juce::jmin (attack + hold + decay, releaseStart);
    for (int i = 1; i <= kCurveSteps; ++i)
    {
        const float t = (float) i / (float) kCurveSteps;
        const float timeMs = holdEnd + (decayEnd - holdEnd) * t;
        envelope.lineTo (xAt (timeMs), yAt (envelopeLevel (timeMs)));
    }

    envelope.lineTo (xAt (releaseStart), yAt (envelopeLevel (releaseStart)));
    const float releaseStartLevel = envelopeLevel (releaseStart);
    for (int i = 1; i <= kCurveSteps; ++i)
    {
        const float t = (float) i / (float) kCurveSteps;
        const float level = releaseStartLevel * (1.0f - curveValue (t, releaseCurve));
        envelope.lineTo (xAt (releaseStart + release * t), yAt (level));
    }

    auto fill = envelope;
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();
    g.setColour (colour.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (colour.withAlpha (0.98f));
    g.strokePath (envelope, juce::PathStrokeType (1.65f));

    auto drawHandle = [&] (juce::Point<float> point, const juce::String& label)
    {
        g.setColour (juce::Colour (0xff10151a));
        g.fillEllipse (point.x - kHandleRadius - 1.0f, point.y - kHandleRadius - 1.0f,
                       2.0f * (kHandleRadius + 1.0f), 2.0f * (kHandleRadius + 1.0f));
        g.setColour (colour);
        g.fillEllipse (point.x - kHandleRadius, point.y - kHandleRadius,
                       2.0f * kHandleRadius, 2.0f * kHandleRadius);
        g.setColour (juce::Colour (0xff10151a));
        g.setFont (juce::Font (7.5f, juce::Font::bold));
        g.drawText (label, juce::Rectangle<float> (point.x - 6.0f, point.y - 15.0f, 12.0f, 9.0f),
                    juce::Justification::centred);
    };

    drawHandle (geometry.attack, "A");
    drawHandle (geometry.hold, "H");
    drawHandle (geometry.decay, "D");
    drawHandle (geometry.sustain, "S");
    drawHandle (geometry.release, "R");
    g.restoreState();

    g.setColour (juce::Colour (0xff9aa8b3));
    g.setFont (juce::Font (8.0f));
    auto readout = getLocalBounds().removeFromTop (14).reduced (6, 0);
    g.drawText ("A " + formatEnvelopeTime (pAttack->load())
                + "  H " + formatEnvelopeTime (pHold->load())
                + "  D " + formatEnvelopeTime (pDecay->load())
                + "  S " + juce::String (pSustain->load() * 100.0f, 0) + "%"
                + "  R " + formatEnvelopeTime (pRelease->load()),
                readout, juce::Justification::centredLeft, false);

    g.setColour (juce::Colour (0xffaeb8c0));
    g.setFont (juce::Font (7.5f));
    auto drawCurveLabel = [&] (const juce::ComboBox& box, const juce::String& label)
    {
        g.drawText (label, box.getX() - 8, box.getY(), 8, box.getHeight(),
                    juce::Justification::centred);
    };
    drawCurveLabel (attackCurveBox, "A");
    drawCurveLabel (decayCurveBox, "D");
    drawCurveLabel (releaseCurveBox, "R");

    g.setColour (juce::Colour (0xff9aa8b3));
    g.setFont (juce::Font (7.0f, juce::Font::bold));
    auto drawControlLabel = [&] (const juce::Slider& slider, const juce::String& label)
    {
        const auto r = slider.getBounds();
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
    auto area = getLocalBounds().reduced (5);
    area.removeFromTop (15);

    auto curveRow = area.removeFromBottom (18);
    muteButton.setBounds (curveRow.removeFromRight (18));
    curveRow.removeFromRight (2);

    constexpr int labelWidth = 7;
    constexpr int gap = 2;
    const int boxWidth = juce::jlimit (30, 39,
        (curveRow.getWidth() - 3 * (labelWidth + gap + 1)) / 3);

    auto placeCurve = [&] (juce::ComboBox& box)
    {
        curveRow.removeFromLeft (1);
        curveRow.removeFromLeft (labelWidth);
        box.setBounds (curveRow.removeFromLeft (boxWidth));
        curveRow.removeFromLeft (gap);
    };

    placeCurve (attackCurveBox);
    placeCurve (decayCurveBox);
    placeCurve (releaseCurveBox);

    area.removeFromBottom (3);
    auto filterRow = area.removeFromBottom (23);
    area.removeFromBottom (2);
    auto pitchRow = area.removeFromBottom (23);

    auto placePair = [] (juce::Rectangle<int> row, juce::Slider& first, juce::Slider& second)
    {
        constexpr int gapBetween = 4;
        const int cellWidth = (row.getWidth() - gapBetween) / 2;
        auto firstCell = row.removeFromLeft (cellWidth);
        row.removeFromLeft (gapBetween);
        auto secondCell = row;
        first.setBounds (firstCell.withTrimmedTop (7).withHeight (16));
        second.setBounds (secondCell.withTrimmedTop (7).withHeight (16));
    };

    placePair (pitchRow, panSlider, pitchSlider);
    placePair (filterRow, highPassSlider, lowPassSlider);
}

void LayerEnvelopeComponent::beginGesture (Handle handle)
{
    activeHandle = handle;
    juce::String parameterId;
    switch (handle)
    {
        case Handle::Attack: parameterId = ids::layerAttack (layer); break;
        case Handle::Hold: parameterId = ids::layerHold (layer); break;
        case Handle::Decay: parameterId = ids::layerDecay (layer); break;
        case Handle::Sustain: parameterId = ids::layerSustain (layer); break;
        case Handle::Release: parameterId = ids::layerRelease (layer); break;
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
        case Handle::Attack: parameterId = ids::layerAttack (layer); break;
        case Handle::Hold: parameterId = ids::layerHold (layer); break;
        case Handle::Decay: parameterId = ids::layerDecay (layer); break;
        case Handle::Sustain: parameterId = ids::layerSustain (layer); break;
        case Handle::Release: parameterId = ids::layerRelease (layer); break;
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
    const auto geometry = getGeometry (processor.getWaveformPreview (layer).durationMs > 0.0
                                           ? (float) processor.getWaveformPreview (layer).durationMs : 1000.0f);
    const float duration = juce::jmax (1.0f, geometry.durationMs);
    const float x = juce::jlimit (0.0f, 1.0f,
                                  (position.x - geometry.plot.getX())
                                      / juce::jmax (1.0f, geometry.plot.getWidth()));
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
            const float level = juce::jlimit (0.0f, 1.0f,
                (geometry.plot.getBottom() - position.y)
                    / juce::jmax (1.0f, geometry.plot.getHeight()));
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
    if (event.mods.isPopupMenu())
        return;

    endGesture();
    const auto preview = processor.getWaveformPreview (layer);
    const auto geometry = getGeometry (preview.loaded ? (float) preview.durationMs : 1000.0f);
    const juce::Point<float> handles[] = {
        geometry.attack, geometry.hold, geometry.decay, geometry.sustain, geometry.release
    };
    const Handle handleTypes[] = {
        Handle::Attack, Handle::Hold, Handle::Decay, Handle::Sustain, Handle::Release
    };

    float bestDistance = 17.0f;
    Handle bestHandle = Handle::None;
    for (int i = 0; i < 5; ++i)
    {
        const float distance = event.position.getDistanceFrom (handles[i]);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestHandle = handleTypes[i];
        }
    }

    if (bestHandle != Handle::None)
        beginGesture (bestHandle);
}

void LayerEnvelopeComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (activeHandle != Handle::None)
        applyDrag (event.position);
}

void LayerEnvelopeComponent::mouseUp (const juce::MouseEvent&)
{
    endGesture();
}
