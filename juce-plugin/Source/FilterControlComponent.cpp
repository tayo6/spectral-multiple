#include "FilterControlComponent.h"

#include <cmath>

namespace
{
    juce::String formatFreqShort (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + "k";
        return juce::String (juce::roundToInt (hz));
    }
}

FilterControlComponent::FilterControlComponent (WaveLensProcessor& processor,
                                                const juce::String& highPassParamId,
                                                const juce::String& lowPassParamId,
                                                juce::Colour accentColour,
                                                float minHpHz,
                                                float maxHpHz,
                                                float minLpHz,
                                                float maxLpHz)
    : proc (processor),
      hpId (highPassParamId),
      lpId (lowPassParamId),
      accent (accentColour),
      hpMin (minHpHz),
      hpMax (maxHpHz),
      lpMin (minLpHz),
      lpMax (maxLpHz)
{
    pHp = proc.apvts.getRawParameterValue (hpId);
    pLp = proc.apvts.getRawParameterValue (lpId);
    setTooltip ("HP / LP Filter: Drag left handle for High-Pass, right handle for Low-Pass. Double-click to reset.");
    setRepaintsOnMouseActivity (true);
    startTimerHz (15);
}

FilterControlComponent::~FilterControlComponent()
{
    stopTimer();
    if (activeHandle != DragHandle::None)
    {
        if (auto* p = proc.apvts.getParameter (activeHandle == DragHandle::HighPass ? hpId : lpId))
            p->endChangeGesture();
        activeHandle = DragHandle::None;
    }
}

void FilterControlComponent::timerCallback()
{
    const float hp = pHp != nullptr ? pHp->load() : hpMin;
    const float lp = pLp != nullptr ? pLp->load() : lpMax;
    if (std::abs (hp - lastPaintedHp) > 0.25f || std::abs (lp - lastPaintedLp) > 0.5f)
        repaint();
}

juce::Rectangle<float> FilterControlComponent::getTrackBounds() const noexcept
{
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft (44.0f);
    r.removeFromRight (44.0f);
    return r.reduced (4.0f, 0.0f);
}

float FilterControlComponent::hzToNorm (float hz) const noexcept
{
    const float lo = juce::jmax (1.0f, hpMin);
    const float hi = juce::jmax (lo + 1.0f, lpMax);
    const float clamped = juce::jlimit (lo, hi, hz);
    return (float) (std::log (clamped / lo) / std::log (hi / lo));
}

float FilterControlComponent::normToHz (float norm) const noexcept
{
    const float lo = juce::jmax (1.0f, hpMin);
    const float hi = juce::jmax (lo + 1.0f, lpMax);
    return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, norm));
}

void FilterControlComponent::applyValue (DragHandle handle, float hzValue)
{
    if (handle == DragHandle::None)
        return;

    const auto& id = (handle == DragHandle::HighPass) ? hpId : lpId;
    if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id)))
    {
        float clamped = hzValue;
        if (handle == DragHandle::HighPass)
        {
            const float currentLp = pLp != nullptr ? pLp->load() : lpMax;
            clamped = juce::jlimit (hpMin, juce::jmin (hpMax, currentLp * 0.8f), clamped);
        }
        else
        {
            const float currentHp = pHp != nullptr ? pHp->load() : hpMin;
            clamped = juce::jlimit (juce::jmax (lpMin, currentHp * 1.25f), lpMax, clamped);
        }
        param->setValueNotifyingHost (param->convertTo0to1 (clamped));
    }
    repaint();
}

void FilterControlComponent::paint (juce::Graphics& g)
{
    namespace ui = wavelens::ui;

    const float hp = pHp != nullptr ? pHp->load() : hpMin;
    const float lp = pLp != nullptr ? pLp->load() : lpMax;
    lastPaintedHp = hp;
    lastPaintedLp = lp;

    const auto full = getLocalBounds().toFloat();
    g.setColour (ui::kMainBg.withAlpha (0.85f));
    g.fillRoundedRectangle (full.reduced (0.5f), 3.0f);
    g.setColour (ui::kBorder);
    g.drawRoundedRectangle (full.reduced (0.5f), 3.0f, 1.0f);

    // HP and LP labels/values on the ends
    g.setFont (juce::Font (9.5f, juce::Font::bold));
    g.setColour (hp > hpMin + 0.5f ? accent : ui::kSecondaryText);
    g.drawText ("HP " + formatFreqShort (hp),
                full.withWidth (44.0f).reduced (3.0f, 0.0f),
                juce::Justification::centredLeft, false);

    g.setColour (lp < lpMax - 10.0f ? accent : ui::kSecondaryText);
    g.drawText (formatFreqShort (lp) + " LP",
                full.withLeft (full.getRight() - 44.0f).reduced (3.0f, 0.0f),
                juce::Justification::centredRight, false);

    const auto track = getTrackBounds();
    if (track.getWidth() < 12.0f)
        return;

    const float cy = track.getCentreY();
    const float barH = juce::jmax (10.0f, track.getHeight() - 8.0f);
    juce::Rectangle<float> groove (track.getX(), cy - barH * 0.5f, track.getWidth(), barH);
    g.setColour (ui::kControlBg.darker (0.25f));
    g.fillRoundedRectangle (groove, 2.5f);

    const float xHp = track.getX() + hzToNorm (hp) * track.getWidth();
    const float xLp = juce::jmax (xHp + 6.0f, track.getX() + hzToNorm (lp) * track.getWidth());

    // Active passband fill inside bar
    g.setColour (accent.withAlpha (0.24f));
    g.fillRoundedRectangle (xHp, groove.getY(), juce::jmax (2.0f, xLp - xHp), groove.getHeight(), 2.0f);
    g.setColour (accent.withAlpha (0.80f));
    g.fillRect (xHp, groove.getY(), juce::jmax (2.0f, xLp - xHp), 1.5f);

    auto drawHandle = [&] (float hx, bool highlighted)
    {
        const float w = highlighted ? 4.5f : 3.2f;
        const float h = groove.getHeight() + 4.0f;
        juce::Rectangle<float> tab (hx - w * 0.5f, cy - h * 0.5f, w, h);
        g.setColour (accent);
        g.fillRoundedRectangle (tab, 1.5f);
        g.setColour (ui::kMainText.withAlpha (highlighted ? 0.95f : 0.70f));
        g.drawRoundedRectangle (tab, 1.5f, 0.75f);
    };

    drawHandle (xHp, activeHandle == DragHandle::HighPass || hoveredHandle == DragHandle::HighPass);
    drawHandle (xLp, activeHandle == DragHandle::LowPass || hoveredHandle == DragHandle::LowPass);
}

void FilterControlComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto track = getTrackBounds();
    const float hp = pHp != nullptr ? pHp->load() : hpMin;
    const float lp = pLp != nullptr ? pLp->load() : lpMax;
    const float xHp = track.getX() + hzToNorm (hp) * track.getWidth();
    const float xLp = juce::jmax (xHp + 6.0f, track.getX() + hzToNorm (lp) * track.getWidth());

    const float dHp = std::abs (e.position.x - xHp);
    const float dLp = std::abs (e.position.x - xLp);
    DragHandle nextHover = DragHandle::None;
    if (track.expanded (8.0f, 6.0f).contains (e.position))
        nextHover = (dHp <= dLp && dHp <= 12.0f) ? DragHandle::HighPass : (dLp <= 12.0f ? DragHandle::LowPass : DragHandle::None);

    setMouseCursor (nextHover != DragHandle::None ? juce::MouseCursor::LeftRightResizeCursor
                                                  : juce::MouseCursor::NormalCursor);
    if (hoveredHandle != nextHover)
    {
        hoveredHandle = nextHover;
        repaint();
    }
}

void FilterControlComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto track = getTrackBounds();
    if (! track.expanded (10.0f, 6.0f).contains (e.position))
        return;

    const float hp = pHp != nullptr ? pHp->load() : hpMin;
    const float lp = pLp != nullptr ? pLp->load() : lpMax;
    const float xHp = track.getX() + hzToNorm (hp) * track.getWidth();
    const float xLp = juce::jmax (xHp + 6.0f, track.getX() + hzToNorm (lp) * track.getWidth());

    const float dHp = std::abs (e.position.x - xHp);
    const float dLp = std::abs (e.position.x - xLp);
    activeHandle = (dHp <= dLp) ? DragHandle::HighPass : DragHandle::LowPass;

    const auto& id = (activeHandle == DragHandle::HighPass) ? hpId : lpId;
    if (auto* p = proc.apvts.getParameter (id))
        p->beginChangeGesture();

    const float norm = juce::jlimit (0.0f, 1.0f, (e.position.x - track.getX()) / juce::jmax (1.0f, track.getWidth()));
    applyValue (activeHandle, normToHz (norm));
}

void FilterControlComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (activeHandle == DragHandle::None)
        return;
    const auto track = getTrackBounds();
    const float norm = juce::jlimit (0.0f, 1.0f, (e.position.x - track.getX()) / juce::jmax (1.0f, track.getWidth()));
    applyValue (activeHandle, normToHz (norm));
}

void FilterControlComponent::mouseUp (const juce::MouseEvent&)
{
    if (activeHandle == DragHandle::None)
        return;

    const auto& id = (activeHandle == DragHandle::HighPass) ? hpId : lpId;
    if (auto* p = proc.apvts.getParameter (id))
        p->endChangeGesture();
    activeHandle = DragHandle::None;
    repaint();
}

void FilterControlComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto track = getTrackBounds();
    const float hp = pHp != nullptr ? pHp->load() : hpMin;
    const float lp = pLp != nullptr ? pLp->load() : lpMax;
    const float xHp = track.getX() + hzToNorm (hp) * track.getWidth();
    const float xLp = juce::jmax (xHp + 6.0f, track.getX() + hzToNorm (lp) * track.getWidth());

    const DragHandle target = (std::abs (e.position.x - xHp) <= std::abs (e.position.x - xLp))
        ? DragHandle::HighPass
        : DragHandle::LowPass;

    const auto& id = (target == DragHandle::HighPass) ? hpId : lpId;
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (target == DragHandle::HighPass ? hpMin : lpMax));
        p->endChangeGesture();
    }
    repaint();
}
