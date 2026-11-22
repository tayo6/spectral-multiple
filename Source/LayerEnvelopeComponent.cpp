#include "LayerEnvelopeComponent.h"

namespace
{
    constexpr float kSustainHoldMs = 350.0f;
    constexpr float kHandleRadius = 5.0f;

    juce::String curveValueLabel (int curve)
    {
        static const char* labels[] = { "Lin", "Exp", "Log", "S", "Bez" };
        return labels[juce::jlimit (0, 4, curve)];
    }

    juce::String formatEnvelopeTime (float value)
    {
        if (value >= 1000.0f)
            return juce::String (value / 1000.0f, 1) + "s";
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
    pDecay = state.getRawParameterValue (ids::layerDecay (layer));
    pSustain = state.getRawParameterValue (ids::layerSustain (layer));
    pRelease = state.getRawParameterValue (ids::layerRelease (layer));
    pAttackCurve = state.getRawParameterValue (ids::layerAttackCurve (layer));
    pDecayCurve = state.getRawParameterValue (ids::layerDecayCurve (layer));
    pReleaseCurve = state.getRawParameterValue (ids::layerReleaseCurve (layer));

    const juce::StringArray curveNames { "Linear", "Exponential", "Logarithmic", "S-Curve", "Bezier" };
    const juce::StringArray shortNames { "Lin", "Exp", "Log", "S", "Bez" };

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

    juce::ignoreUnused (curveNames);
    prepareCurveBox (attackCurveBox, "Attack curve: Linear, Exponential, Logarithmic, S-Curve or Bezier");
    prepareCurveBox (decayCurveBox, "Decay curve: Linear, Exponential, Logarithmic, S-Curve or Bezier");
    prepareCurveBox (releaseCurveBox, "Release curve: Linear, Exponential, Logarithmic, S-Curve or Bezier");

    attackCurveAttachment = std::make_unique<ComboAtt> (state, ids::layerAttackCurve (layer), attackCurveBox);
    decayCurveAttachment = std::make_unique<ComboAtt> (state, ids::layerDecayCurve (layer), decayCurveBox);
    releaseCurveAttachment = std::make_unique<ComboAtt> (state, ids::layerReleaseCurve (layer), releaseCurveBox);

    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setOpaque (false);
    startTimerHz (30);
}

LayerEnvelopeComponent::~LayerEnvelopeComponent()
{
    stopTimer();
    endGesture();
}

float LayerEnvelopeComponent::curveValue (float progress, int curve) noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, progress);
    switch (juce::jlimit (0, 4, curve))
    {
        case 0: return t;
        case 1: return (std::exp (5.0f * t) - 1.0f) / (std::exp (5.0f) - 1.0f);
        case 2: return std::log1p (9.0f * t) / std::log (10.0f);
        case 3: return t * t * (3.0f - 2.0f * t);
        case 4: return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        default: return t;
    }
}

LayerEnvelopeComponent::Geometry LayerEnvelopeComponent::getGeometry() const
{
    Geometry result;
    auto bounds = getLocalBounds().toFloat().reduced (6.0f);
    bounds.removeFromBottom (22.0f);
    result.plot = bounds;

    const float attack = pAttack->load();
    const float decay = pDecay->load();
    const float sustain = pSustain->load();
    const float release = pRelease->load();
    const float total = juce::jmax (1.0f, attack + decay + kSustainHoldMs + release);

    const float xAttack = result.plot.getX() + result.plot.getWidth() * attack / total;
    const float xDecay = result.plot.getX() + result.plot.getWidth() * (attack + decay) / total;
    const float xRelease = result.plot.getX() + result.plot.getWidth()
                           * (attack + decay + kSustainHoldMs) / total;
    const float sustainY = result.plot.getBottom() - result.plot.getHeight() * sustain;

    result.attack = { xAttack, result.plot.getY() };
    result.decay = { xDecay, sustainY };
    result.sustain = { 0.5f * (xDecay + xRelease), sustainY };
    result.release = { xRelease, sustainY };
    return result;
}

void LayerEnvelopeComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff171d23));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (colour.withAlpha (0.28f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    const auto geometry = getGeometry();
    const auto plot = geometry.plot;
    if (plot.getWidth() < 24.0f || plot.getHeight() < 20.0f)
        return;

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    g.setColour (juce::Colours::white.withAlpha (0.055f));
    for (int i = 1; i < 4; ++i)
    {
        const float y = plot.getY() + plot.getHeight() * (float) i / 4.0f;
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    }
    for (int i = 1; i < 4; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * (float) i / 4.0f;
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
    }

    const int attackCurve = juce::roundToInt (pAttackCurve->load());
    const int decayCurve = juce::roundToInt (pDecayCurve->load());
    const int releaseCurve = juce::roundToInt (pReleaseCurve->load());
    const float sustainLevel = pSustain->load();

    juce::Path envelope;
    envelope.startNewSubPath (plot.getX(), plot.getBottom());

    constexpr int steps = 16;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / (float) steps;
        const float level = curveValue (t, attackCurve);
        const float x = plot.getX() + (geometry.attack.x - plot.getX()) * t;
        const float y = plot.getBottom() - plot.getHeight() * level;
        envelope.lineTo (x, y);
    }

    for (int i = 1; i <= steps; ++i)
    {
        const float t = (float) i / (float) steps;
        const float level = 1.0f + (sustainLevel - 1.0f) * curveValue (t, decayCurve);
        const float x = geometry.attack.x + (geometry.decay.x - geometry.attack.x) * t;
        const float y = plot.getBottom() - plot.getHeight() * level;
        envelope.lineTo (x, y);
    }

    envelope.lineTo (geometry.release.x, geometry.release.y);

    for (int i = 1; i <= steps; ++i)
    {
        const float t = (float) i / (float) steps;
        const float level = sustainLevel * (1.0f - curveValue (t, releaseCurve));
        const float x = geometry.release.x + (plot.getRight() - geometry.release.x) * t;
        const float y = plot.getBottom() - plot.getHeight() * level;
        envelope.lineTo (x, y);
    }

    auto fill = envelope;
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();
    g.setColour (colour.withAlpha (0.11f));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (envelope, juce::PathStrokeType (1.7f));

    auto drawHandle = [&] (juce::Point<float> point, const juce::String& label)
    {
        g.setColour (juce::Colour (0xff10151a));
        g.fillEllipse (point.x - kHandleRadius - 1.0f, point.y - kHandleRadius - 1.0f,
                       2.0f * (kHandleRadius + 1.0f), 2.0f * (kHandleRadius + 1.0f));
        g.setColour (colour);
        g.fillEllipse (point.x - kHandleRadius, point.y - kHandleRadius,
                       2.0f * kHandleRadius, 2.0f * kHandleRadius);
        g.setColour (juce::Colour (0xff10151a));
        g.setFont (juce::Font (8.0f, juce::Font::bold));
        g.drawText (label, juce::Rectangle<float> (point.x - 8.0f, point.y - 16.0f, 16.0f, 10.0f),
                    juce::Justification::centred);
    };

    drawHandle (geometry.attack, "A");
    drawHandle (geometry.decay, "D");
    drawHandle (geometry.sustain, "S");
    drawHandle (geometry.release, "R");

    g.restoreState();

    g.setColour (juce::Colour (0xff9aa8b3));
    g.setFont (juce::Font (8.5f));
    auto readout = getLocalBounds().removeFromTop (14).reduced (7, 0);
    g.drawText ("A " + formatEnvelopeTime (pAttack->load())
                + "   D " + formatEnvelopeTime (pDecay->load())
                + "   S " + juce::String (pSustain->load() * 100.0f, 0) + "%"
                + "   R " + formatEnvelopeTime (pRelease->load()),
                readout, juce::Justification::centredLeft, false);

    g.setColour (juce::Colour (0xffaeb8c0));
    g.setFont (juce::Font (8.0f));
    g.drawText ("A", attackCurveBox.getBounds().translated (0, 0).expanded (0, 0).withX (attackCurveBox.getX() - 8).withWidth (8),
                juce::Justification::centred);
    g.drawText ("D", decayCurveBox.getBounds().translated (0, 0).withX (decayCurveBox.getX() - 8).withWidth (8),
                juce::Justification::centred);
    g.drawText ("R", releaseCurveBox.getBounds().translated (0, 0).withX (releaseCurveBox.getX() - 8).withWidth (8),
                juce::Justification::centred);
}

void LayerEnvelopeComponent::resized()
{
    auto row = getLocalBounds().reduced (6);
    row.removeFromTop (getLocalBounds().getHeight() - 22);
    row.removeFromBottom (1);

    constexpr int labelWidth = 8;
    constexpr int boxWidth = 42;
    constexpr int gap = 3;

    auto place = [&] (juce::ComboBox& box)
    {
        row.removeFromLeft (2);
        row.removeFromLeft (labelWidth);
        box.setBounds (row.removeFromLeft (boxWidth));
        row.removeFromLeft (gap);
    };

    place (attackCurveBox);
    place (decayCurveBox);
    place (releaseCurveBox);
}

void LayerEnvelopeComponent::beginGesture (Handle handle)
{
    activeHandle = handle;
    const char* id = nullptr;
    switch (handle)
    {
        case Handle::Attack: id = "attack"; break;
        case Handle::Decay: id = "decay"; break;
        case Handle::Sustain: id = "sustain"; break;
        case Handle::Release: id = "release"; break;
        default: break;
    }

    if (id != nullptr)
    {
        juce::String parameterId;
        switch (handle)
        {
            case Handle::Attack: parameterId = ids::layerAttack (layer); break;
            case Handle::Decay: parameterId = ids::layerDecay (layer); break;
            case Handle::Sustain: parameterId = ids::layerSustain (layer); break;
            case Handle::Release: parameterId = ids::layerRelease (layer); break;
            default: break;
        }

        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.apvts.getParameter (parameterId)))
            parameter->beginChangeGesture();
    }
}

void LayerEnvelopeComponent::endGesture()
{
    if (activeHandle == Handle::None)
        return;

    juce::String parameterId;
    switch (activeHandle)
    {
        case Handle::Attack: parameterId = ids::layerAttack (layer); break;
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
    const auto geometry = getGeometry();
    const float width = juce::jmax (1.0f, geometry.plot.getWidth());
    const float x = juce::jlimit (0.015f, 0.985f,
                                  (position.x - geometry.plot.getX()) / width);

    const float attack = pAttack->load();
    const float decay = pDecay->load();
    const float release = pRelease->load();

    switch (activeHandle)
    {
        case Handle::Attack:
        {
            const float rest = decay + kSustainHoldMs + release;
            const float newAttack = (x / (1.0f - x)) * rest;
            setParameter (ids::layerAttack (layer), juce::jlimit (0.1f, 2000.0f, newAttack));
            break;
        }
        case Handle::Decay:
        {
            const float rest = kSustainHoldMs + release;
            const float cumulative = (x / (1.0f - x)) * rest;
            const float newDecay = cumulative - attack;
            setParameter (ids::layerDecay (layer), juce::jlimit (0.1f, 5000.0f, newDecay));
            break;
        }
        case Handle::Sustain:
        {
            const float level = juce::jlimit (0.0f, 1.0f,
                (geometry.plot.getBottom() - position.y) / juce::jmax (1.0f, geometry.plot.getHeight()));
            setParameter (ids::layerSustain (layer), level);
            break;
        }
        case Handle::Release:
        {
            const float elapsed = attack + decay + kSustainHoldMs;
            const float newRelease = elapsed * (1.0f - x) / x;
            setParameter (ids::layerRelease (layer), juce::jlimit (0.1f, 10000.0f, newRelease));
            break;
        }
        default: break;
    }

    repaint();
}

void LayerEnvelopeComponent::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
        return;

    const auto geometry = getGeometry();
    const auto point = event.position;
    const juce::Point<float> handles[] = {
        geometry.attack, geometry.decay, geometry.sustain, geometry.release
    };
    const Handle handleTypes[] = { Handle::Attack, Handle::Decay, Handle::Sustain, Handle::Release };

    float bestDistance = 16.0f;
    Handle bestHandle = Handle::None;

    for (int i = 0; i < 4; ++i)
    {
        const float distance = point.getDistanceFrom (handles[i]);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestHandle = handleTypes[i];
        }
    }

    if (bestHandle == Handle::None)
        return;

    mouseDownPosition = point;
    beginGesture (bestHandle);
    applyDrag (point);
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
