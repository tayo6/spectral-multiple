#include "PluginEditor.h"

#include <cmath>

namespace
{
    constexpr auto relaxed = std::memory_order_relaxed;

    const juce::Colour kBgObsidian   { 0xff100f15 };
    const juce::Colour kHeaderBg     { 0xff15141d };
    const juce::Colour kDeckBg       { 0xff1a1923 };
    const juce::Colour kDeckHdrBg    { 0xff1f1e2a };
    const juce::Colour kDividerBg    { 0xff171620 };
    const juce::Colour kBorderDark   { 0xff2d2b3c };
    const juce::Colour kBtnDark      { 0xff1b1a26 };
    const juce::Colour kTextLight    { 0xffe6e8f2 };
    const juce::Colour kTextMuted    { 0xff9fa4c4 };
    const juce::Colour kSnapbackHdr  { 0xff8ea0ff };
    const juce::Colour kTransientHdr { 0xffcb8cff };
    const juce::Colour kMeterHot     { 0xffff4e42 };
    const juce::Colour kMeterCool    { 0xff58e058 };

    juce::String formatMs (double ms)
    {
        if (ms >= 1000.0)
            return juce::String (ms / 1000.0, 2) + " s";
        return juce::String (ms, ms < 10.0 ? 2 : 1) + " ms";
    }

    juce::String formatTimeTickLabel (double ms)
    {
        const double absMs = std::abs (ms);
        if (absMs < 0.05) return "0.00s";
        if (absMs >= 1000.0) return juce::String (ms / 1000.0, 2) + "s";
        if (absMs >= 100.0) return juce::String (ms / 1000.0, 3) + "s";
        if (absMs >= 10.0) return juce::String (ms, 1) + "ms";
        return juce::String (ms, 2) + "ms";
    }

    juce::String formatDb (float linear, float floorDb = -90.0f)
    {
        const float db = 20.0f * std::log10 (juce::jmax (linear, 1.0e-6f));
        return db <= floorDb ? juce::String ("-inf dB") : juce::String (db, 1) + " dB";
    }
}

//==============================================================================
WaveLensLookAndFeel::WaveLensLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, kBgObsidian);

    setColour (juce::ComboBox::backgroundColourId, kBtnDark);
    setColour (juce::ComboBox::textColourId, kTextLight);
    setColour (juce::ComboBox::outlineColourId, kBorderDark);
    setColour (juce::ComboBox::arrowColourId, kTextMuted);

    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1f1e2c));
    setColour (juce::PopupMenu::textColourId, kTextLight);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff312f48));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    setColour (juce::TextButton::buttonColourId, kBtnDark);
    setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2a2840));
    setColour (juce::TextButton::textColourOffId, kTextMuted);
    setColour (juce::TextButton::textColourOnId, kTextLight);

    setColour (juce::Label::textColourId, kTextLight);
}

void WaveLensLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float rotaryStartAngle,
                                            float rotaryEndAngle, juce::Slider& slider)
{
    const float diameter = (float) juce::jmin (width, height) - 4.0f;
    const float radius = diameter * 0.5f;
    const float cx = (float) x + (float) width * 0.5f;
    const float cy = (float) y + (float) height * 0.5f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    juce::Path bgArc;
    bgArc.addCentredArc (cx, cy, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (bgArc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path valArc;
    valArc.addCentredArc (cx, cy, radius, radius, 0.0f, rotaryStartAngle, angle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (valArc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float innerR = juce::jmax (3.0f, radius - 3.2f);
    g.setColour (juce::Colour (0xff1b1a24));
    g.fillEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f);
    g.setColour (juce::Colour (0xff353346));
    g.drawEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f, 1.0f);

    const float px = cx + (innerR - 1.5f) * std::sin (angle);
    const float py = cy - (innerR - 1.5f) * std::cos (angle);
    g.setColour (juce::Colour (0xffeceeff));
    g.drawLine (cx, cy, px, py, 1.75f);
}

void WaveLensLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                const juce::Colour& backgroundColour,
                                                bool shouldDrawButtonAsHighlighted,
                                                bool shouldDrawButtonAsDown)
{
    if (static_cast<bool> (button.getProperties().getWithDefault ("isCircle", false)))
    {
        auto b = button.getLocalBounds().toFloat().reduced (1.0f);
        const float d = juce::jmin (b.getWidth(), b.getHeight());
        auto circle = b.withSizeKeepingCentre (d, d);
        auto fill = backgroundColour;
        if (shouldDrawButtonAsDown) fill = fill.darker (0.18f);
        else if (shouldDrawButtonAsHighlighted) fill = fill.brighter (0.14f);
        g.setColour (fill);
        g.fillEllipse (circle);
        return;
    }

    juce::LookAndFeel_V4::drawButtonBackground (g, button, backgroundColour,
                                                shouldDrawButtonAsHighlighted,
                                                shouldDrawButtonAsDown);
}

//==============================================================================
LayerOverlayView::LayerOverlayView (WaveLensProcessor& p)
    : processor (p)
{
    setOpaque (true);
    startTimerHz (15);
}

void LayerOverlayView::clampViewport (float nextStart, float nextZoom) noexcept
{
    zoomFactor = juce::jlimit (1.0f, 32.0f, nextZoom);
    const float span = 1.0f / zoomFactor;
    viewStartNorm = zoomFactor <= 1.0001f ? 0.0f : juce::jlimit (0.0f, juce::jmax (0.0f, 1.0f - span), nextStart);
    if (zoomFactor <= 1.0001f) zoomFactor = 1.0f;
}

float LayerOverlayView::normToX (float norm, float plotX, float plotW) const noexcept
{
    const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
    return plotX + ((norm - viewStartNorm) / span) * plotW;
}

float LayerOverlayView::xToNorm (float x, float plotX, float plotW) const noexcept
{
    const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
    return juce::jlimit (0.0f, 1.0f, viewStartNorm + ((x - plotX) / juce::jmax (1.0f, plotW)) * span);
}

void LayerOverlayView::zoomBy (float factor, float anchorNorm) noexcept
{
    const float curSpan = 1.0f / juce::jmax (1.0f, zoomFactor);
    const float anchorRatio = juce::jlimit (0.0f, 1.0f, (anchorNorm - viewStartNorm) / curSpan);
    const float nextZoom = juce::jlimit (1.0f, 32.0f, zoomFactor * factor);
    const float nextSpan = 1.0f / nextZoom;
    clampViewport (anchorNorm - anchorRatio * nextSpan, nextZoom);
    repaint();
}

void LayerOverlayView::resetZoom() noexcept
{
    clampViewport (0.0f, 1.0f);
    repaint();
}

void LayerOverlayView::focusTrigger (float zoomIfUnzoomed) noexcept
{
    if (zoomFactor > 1.2f)
        resetZoom();
    else
    {
        const float nextZoom = juce::jlimit (1.0f, 32.0f, zoomIfUnzoomed);
        clampViewport (0.5f - 0.5f / nextZoom, nextZoom);
        repaint();
    }
}

void LayerOverlayView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (std::abs (wheel.deltaY) < 1.0e-4f) return;
    const float anchor = xToNorm (e.position.x, 0.0f, juce::jmax (1.0f, (float) getWidth()));
    zoomBy (wheel.deltaY > 0 ? 1.28f : 1.0f / 1.28f, anchor);
}

void LayerOverlayView::mouseDoubleClick (const juce::MouseEvent&)
{
    resetZoom();
}

void LayerOverlayView::mouseDown (const juce::MouseEvent& e)
{
    dragStartX = e.position.x;
    dragStartView = viewStartNorm;
}

void LayerOverlayView::mouseDrag (const juce::MouseEvent& e)
{
    if (zoomFactor <= 1.01f) return;
    const float dx = e.position.x - dragStartX;
    const float span = 1.0f / zoomFactor;
    clampViewport (dragStartView - (dx / juce::jmax (1.0f, (float) getWidth())) * span, zoomFactor);
    repaint();
}

void LayerOverlayView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient bgGrad (juce::Colour (0xff0f0e14), 0.0f, 0.0f,
                                 juce::Colour (0xff13121a), 0.0f, bounds.getBottom(), false);
    g.setGradientFill (bgGrad);
    g.fillRect (bounds);

    constexpr float kRulerH = 22.0f;
    auto ruler = bounds.removeFromTop (kRulerH);
    g.setColour (juce::Colour (0xff16151f));
    g.fillRect (ruler);
    g.setColour (juce::Colour (0x249ba0d2));
    g.drawHorizontalLine (juce::roundToInt (ruler.getBottom() - 0.5f), 0.0f, bounds.getRight());

    auto plot = bounds;
    const float plotX = plot.getX();
    const float plotY = plot.getY();
    const float plotW = plot.getWidth();
    const float plotH = plot.getHeight();
    if (plotW < 32.0f || plotH < 32.0f) return;

    const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
    const float viewEnd = juce::jmin (1.0f, viewStartNorm + span);
    const double barMs = (4.0 * 60000.0) / bpm;

    g.setFont (juce::Font (10.0f));
    if (gridMode)
    {
        int divisions = 8;
        int majorStep = 2;
        if (zoomFactor >= 12.0f) { divisions = 64; majorStep = 4; }
        else if (zoomFactor >= 4.5f) { divisions = 32; majorStep = 2; }
        else if (zoomFactor >= 1.8f) { divisions = 16; majorStep = 4; }

        const int firstIdx = juce::jmax (0, (int) std::floor (viewStartNorm * (float) divisions));
        const int lastIdx = juce::jmin (divisions, (int) std::ceil (viewEnd * (float) divisions));
        for (int i = firstIdx; i <= lastIdx; ++i)
        {
            const float norm = (float) i / (float) divisions;
            const float tx = normToX (norm, plotX, plotW);
            const bool major = (i % majorStep == 0);
            g.setColour (major ? juce::Colour (0x21a0a6e0) : juce::Colour (0x0ca0a6e0));
            g.drawVerticalLine (juce::roundToInt (tx), plotY, plot.getBottom());

            g.setColour (major ? juce::Colour (0x73c4caf0) : juce::Colour (0x2ec4caf0));
            g.drawVerticalLine (juce::roundToInt (tx), major ? ruler.getBottom() - 7.0f : ruler.getBottom() - 4.0f, ruler.getBottom());

            if (major)
            {
                juce::String lbl;
                if (i == divisions) lbl = "2.1";
                else
                {
                    const float beatFloat = norm * 4.0f;
                    const int beatNum = (int) std::floor (beatFloat) + 1;
                    const int sixteenths = juce::roundToInt ((beatFloat - std::floor (beatFloat)) * 4.0f) + 1;
                    lbl = divisions >= 32 ? ("1." + juce::String (beatNum) + "." + juce::String (sixteenths))
                                          : ("1." + juce::String (beatNum));
                }
                g.setColour (juce::Colour (0xb8c6cbe8));
                g.drawText (lbl, juce::roundToInt (tx - 24.0f), 2, 48, (int) kRulerH - 4, juce::Justification::centred);
            }
        }
    }
    else
    {
        const double visibleMs = barMs / (double) zoomFactor;
        static constexpr double candidates[] = { 1000.0, 500.0, 250.0, 100.0, 50.0, 25.0, 10.0, 5.0, 2.0, 1.0 };
        double stepMs = 250.0;
        for (double c : candidates)
        {
            const double count = visibleMs / c;
            if (count >= 4.0 && count <= 10.0) { stepMs = c; break; }
        }
        const double minorStep = stepMs * 0.5;
        const int firstIdx = juce::jmax (0, (int) std::floor ((viewStartNorm * barMs) / minorStep));
        const int lastIdx = juce::jmin ((int) std::ceil (barMs / minorStep), (int) std::ceil ((viewEnd * barMs) / minorStep));
        for (int i = firstIdx; i <= lastIdx; ++i)
        {
            const double ms = (double) i * minorStep;
            const float norm = juce::jlimit (0.0f, 1.0f, (float) (ms / barMs));
            const float tx = normToX (norm, plotX, plotW);
            const bool major = (i % 2 == 0);
            g.setColour (major ? juce::Colour (0x21a0a6e0) : juce::Colour (0x0ca0a6e0));
            g.drawVerticalLine (juce::roundToInt (tx), plotY, plot.getBottom());

            if (major)
            {
                g.setColour (juce::Colour (0xb8c6cbe8));
                g.drawText (formatTimeTickLabel (ms), juce::roundToInt (tx - 28.0f), 2, 56, (int) kRulerH - 4, juce::Justification::centred);
            }
        }
    }

    const float centre = plot.getCentreY();
    const float halfHeight = plotH * 0.43f;
    g.setColour (juce::Colour (0x29b4baeb));
    g.drawHorizontalLine (juce::roundToInt (centre), plotX, plot.getRight());

    const float trigX = normToX (0.5f, plotX, plotW);
    if (trigX >= plotX - 2.0f && trigX <= plot.getRight() + 2.0f)
    {
        g.setColour (juce::Colour (0x579aaeff));
        const float dash[] = { 4.0f, 3.0f };
        g.drawDashedLine ({ trigX, plotY, trigX, plot.getBottom() }, dash, 2, 1.0f);
    }

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    bool anySolo = false;
    for (int l = 0; l < WaveLensProcessor::kMaxSignals; ++l)
        if (processor.apvts.getRawParameterValue (ids::layerSolo (l))->load() > 0.5f)
            anySolo = true;

    int loadedCount = 0;
    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        const auto preview = processor.getWaveformPreview (layer);
        if (! preview.loaded) continue;
        ++loadedCount;

        const bool muted = processor.apvts.getRawParameterValue (ids::layerMute (layer))->load() > 0.5f;
        const bool solo = processor.apvts.getRawParameterValue (ids::layerSolo (layer))->load() > 0.5f;
        const bool audible = ! muted && (! anySolo || solo);
        const bool focused = (layer == selectedSnapback || layer == selectedTransient);
        const bool reversed = processor.apvts.getRawParameterValue (ids::layerReverse (layer))->load() > 0.5f;
        const float trimStart = juce::jlimit (0.0f, 0.99f, processor.apvts.getRawParameterValue (ids::layerTrimStart (layer))->load());
        const float trimEnd = juce::jlimit (trimStart + 0.005f, 1.0f, processor.apvts.getRawParameterValue (ids::layerTrimEnd (layer))->load());
        const float pitch = processor.apvts.getRawParameterValue (ids::layerPitch (layer))->load();
        const float shiftMs = processor.apvts.getRawParameterValue (ids::layerDelay (layer))->load();
        const float fadeInMs = processor.apvts.getRawParameterValue (ids::layerFadeIn (layer))->load();
        const float fadeOutMs = processor.apvts.getRawParameterValue (ids::layerFadeOut (layer))->load();
        const float gainScale = juce::jlimit (0.0f, 2.0f, processor.apvts.getRawParameterValue (ids::layerGain (layer))->load());
        const bool envOn = processor.apvts.getRawParameterValue (ids::layerEnvEnabled (layer))->load() > 0.5f;
        const float attMs = processor.apvts.getRawParameterValue (ids::layerAttack (layer))->load();
        const float holdMs = processor.apvts.getRawParameterValue (ids::layerHold (layer))->load();
        const float decMs = processor.apvts.getRawParameterValue (ids::layerDecay (layer))->load();
        const float sus = processor.apvts.getRawParameterValue (ids::layerSustain (layer))->load();
        const float relMs = processor.apvts.getRawParameterValue (ids::layerRelease (layer))->load();
        const int attCurve = juce::roundToInt (processor.apvts.getRawParameterValue (ids::layerAttackCurve (layer))->load());
        const int decCurve = juce::roundToInt (processor.apvts.getRawParameterValue (ids::layerDecayCurve (layer))->load());
        const int relCurve = juce::roundToInt (processor.apvts.getRawParameterValue (ids::layerReleaseCurve (layer))->load());
        const float attTen = processor.apvts.getRawParameterValue (ids::layerAttackTension (layer))->load();
        const float decTen = processor.apvts.getRawParameterValue (ids::layerDecayTension (layer))->load();
        const float relTen = processor.apvts.getRawParameterValue (ids::layerReleaseTension (layer))->load();

        const float preLeadMs = (layer < LayeredSampleEngine::kPreLayers)
            ? processor.apvts.getRawParameterValue (ids::preRollMs)->load()
            : 0.0f;
        const double effectiveOffsetMs = juce::jmax (-LayeredSampleEngine::kMaximumPreRollMilliseconds,
                                                     (double) shiftMs - (double) preLeadMs);
        const double trimmedMs = juce::jmax (1.0, preview.durationMs * (double) (trimEnd - trimStart));
        const double playbackMs = juce::jmax (1.0, trimmedMs / std::pow (2.0, (double) pitch / 12.0));
        const float startNorm = 0.5f + (float) (effectiveOffsetMs / barMs);
        const float spanNorm = (float) (playbackMs / barMs);
        const float x0 = normToX (startNorm, plotX, plotW);
        const float x1 = normToX (startNorm + spanNorm, plotX, plotW);
        if (x1 < plotX - 2.0f || x0 > plot.getRight() + 2.0f) continue;

        const float visX0 = juce::jmax (plotX, x0);
        const float visX1 = juce::jmin (plot.getRight(), x1);
        const int steps = juce::jlimit (48, 420, juce::roundToInt ((visX1 - visX0) * 1.5f));

        LayeredSampleEngine::LayerParameters lp;
        lp.gain = gainScale;
        lp.pitchSemitones = pitch;
        lp.attackMs = attMs;
        lp.holdMs = holdMs;
        lp.decayMs = decMs;
        lp.sustain = sus;
        lp.releaseMs = relMs;
        lp.attackCurve = attCurve;
        lp.decayCurve = decCurve;
        lp.releaseCurve = relCurve;
        lp.attackTension = attTen;
        lp.decayTension = decTen;
        lp.releaseTension = relTen;
        lp.envEnabled = envOn;
        lp.reverse = reversed;
        lp.trimStart = trimStart;
        lp.trimEnd = trimEnd;
        lp.fadeInMs = fadeInMs;
        lp.fadeOutMs = fadeOutMs;

        const float totalPlayMs = (float) playbackMs;
        const auto sched = LayeredSampleEngine::computeEnvelopeSchedule (lp, totalPlayMs);

        std::array<float, 422> lowerPx {};
        std::array<float, 422> lowerYs {};
        juce::Path area, upper, lower;
        for (int s = 0; s <= steps; ++s)
        {
            const float px = visX0 + ((float) s / (float) steps) * (visX1 - visX0);
            const float u = juce::jlimit (0.0f, 1.0f, (xToNorm (px, plotX, plotW) - startNorm) / juce::jmax (1.0e-6f, spanNorm));
            const float posInWave = reversed ? (trimEnd - u * (trimEnd - trimStart)) : (trimStart + u * (trimEnd - trimStart));
            const int idx = juce::jlimit (0, LayeredSampleEngine::kPreviewPoints - 1,
                juce::roundToInt (posInWave * (float) (LayeredSampleEngine::kPreviewPoints - 1)));

            const float elapsedMs = u * totalPlayMs;
            const float remainingMs = (1.0f - u) * totalPlayMs;
            const float fadeInG = fadeInMs > 0.5f ? juce::jlimit (0.0f, 1.0f, elapsedMs / fadeInMs) : 1.0f;
            const float fadeOutG = fadeOutMs > 0.5f ? juce::jlimit (0.0f, 1.0f, remainingMs / fadeOutMs) : 1.0f;
            const float flEnv = LayeredSampleEngine::evaluateCompleteEnvelopeAtMs (lp, sched, elapsedMs);
            const float env = gainScale * fadeInG * fadeOutG * flEnv;
            const float top = centre - juce::jlimit (-1.0f, 1.0f, preview.maximum[(size_t) idx] * env) * halfHeight;
            const float bot = centre - juce::jlimit (-1.0f, 1.0f, preview.minimum[(size_t) idx] * env) * halfHeight;
            lowerPx[(size_t) s] = px;
            lowerYs[(size_t) s] = bot;

            if (s == 0) { area.startNewSubPath (px, top); upper.startNewSubPath (px, top); lower.startNewSubPath (px, bot); }
            else        { area.lineTo (px, top); upper.lineTo (px, top); lower.lineTo (px, bot); }
        }
        for (int s = steps; s >= 0; --s)
            area.lineTo (lowerPx[(size_t) s], lowerYs[(size_t) s]);
        area.closeSubPath();

        const float alpha = ! audible ? 0.16f : focused ? 1.0f : 0.58f;
        const auto col = signalColour (layer);
        g.setColour (col.withAlpha (alpha * (focused ? 0.27f : 0.14f)));
        g.fillPath (area);
        g.setColour (col.withAlpha (alpha));
        g.strokePath (upper, juce::PathStrokeType (focused ? 1.6f : 1.1f));
        g.setColour (col.withAlpha (alpha * 0.67f));
        g.strokePath (lower, juce::PathStrokeType (focused ? 1.15f : 0.85f));
    }

    if (loadedCount == 0)
    {
        g.setColour (juce::Colour (0x8fcacee8));
        g.setFont (juce::Font (13.0f));
        g.drawText ("1 Bar Stacked Canvas (" + juce::String (barMs / 1000.0, 2) + "s at " + juce::String ((int) bpm)
                        + " BPM) - Load samples into SNAPBACK or TRANSIENT stacks below",
                    plot.toNearestInt(), juce::Justification::centred);
    }

    g.restoreState();
}

//==============================================================================
ScopeView::ScopeView (WaveLensProcessor& p)
    : proc (p)
{
    auto& a = proc.apvts;
    pView = a.getRawParameterValue (ids::view);
    pCapture = a.getRawParameterValue (ids::capture);
    pBars = a.getRawParameterValue (ids::bars);
    pTimeMs = a.getRawParameterValue (ids::timeMs);
    pGain = a.getRawParameterValue (ids::gain);
    pLayout = a.getRawParameterValue (ids::layout);
    for (int n = 0; n < WaveLensProcessor::kMaxSignals; ++n)
        pShowSig[n] = a.getRawParameterValue (ids::showSig[n]);
    pFreeze = a.getRawParameterValue (ids::freeze);
    pHold = a.getRawParameterValue (ids::hold);

    setOpaque (true);
    startTimerHz (30);
}

juce::String ScopeView::windowLabel() const
{
    const int mode = juce::roundToInt (pCapture->load());
    if (mode == 0)
    {
        static const char* names[] = { "1/16 bar", "1/8 bar", "1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars" };
        return juce::String ("Tempo  ") + names[juce::jlimit (0, 6, juce::roundToInt (pBars->load()))];
    }
    return juce::String (mode == 1 ? "Live  " : "Scroll  ") + formatMs (pTimeMs->load());
}

void ScopeView::drawTrace (juce::Graphics& g, juce::Rectangle<float> lane, int ch, juce::Colour colour,
                           const Snapshot& s, float gain, bool held, float fillAlpha)
{
    const bool scroll = juce::roundToInt (pCapture->load()) == 2;
    constexpr int N = WaveLensProcessor::kCols;

    int first = 0, count = 0;
    if (held)
    {
        if (scroll || ! s.heldValid) return;
        first = s.heldFrom;
        count = s.heldTo - s.heldFrom + 1;
    }
    else
    {
        if (! s.has) return;
        if (scroll)
        {
            count = juce::jlimit (0, N, s.filled);
            first = ((s.head - count + 1) % N + N) % N;
        }
        else
        {
            first = s.passStart;
            count = s.head - s.passStart + 1;
        }
    }

    if (count < 2) return;

    const float colW = lane.getWidth() / (float) N;
    const float centre = lane.getCentreY();
    const float half = lane.getHeight() * 0.5f * 0.94f;

    auto xOf = [&] (int j)
    {
        return scroll ? lane.getRight() - ((float) (count - j) - 0.5f) * colW
                      : lane.getX() + ((float) (first + j) + 0.5f) * colW;
    };
    auto yOf = [&] (float v) { return centre - juce::jlimit (-1.0f, 1.0f, v * gain) * half; };
    auto colOf = [&] (int j) { return (first + j) % N; };

    const auto& maxBuf = held ? proc.heldMax : proc.colMax;
    const auto& minBuf = held ? proc.heldMin : proc.colMin;

    juce::Path path;
    for (int j = 0; j < count; ++j)
    {
        const float y = yOf (maxBuf[(size_t) WaveLensProcessor::idx (ch, colOf (j))].load (relaxed));
        if (j == 0) path.startNewSubPath (xOf (j), y);
        else path.lineTo (xOf (j), y);
    }
    for (int j = count - 1; j >= 0; --j)
        path.lineTo (xOf (j), yOf (minBuf[(size_t) WaveLensProcessor::idx (ch, colOf (j))].load (relaxed)));
    path.closeSubPath();

    g.setColour (colour.withAlpha (held ? fillAlpha * 0.35f : fillAlpha));
    g.fillPath (path);
    g.setColour (colour.withAlpha (held ? 0.35f : 0.95f));
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

void ScopeView::drawLane (juce::Graphics& g, juce::Rectangle<float> lane, const std::vector<Trace>& traces,
                          const Snapshot& s, float gain, bool showHeld, const juce::String& title)
{
    g.saveState();
    g.reduceClipRegion (lane.toNearestInt());

    g.setColour (juce::Colours::white.withAlpha (0.05f));
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine (juce::roundToInt (lane.getX() + lane.getWidth() * (float) i / 8.0f), lane.getY(), lane.getBottom());

    const float centre = lane.getCentreY();
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.drawHorizontalLine (juce::roundToInt (centre), lane.getX(), lane.getRight());

    const float fillAlpha = traces.size() > 1 ? 0.12f : 0.28f;
    if (showHeld)
        for (auto it = traces.rbegin(); it != traces.rend(); ++it)
            drawTrace (g, lane, it->ch, it->colour, s, gain, true, fillAlpha);
    for (auto it = traces.rbegin(); it != traces.rend(); ++it)
        drawTrace (g, lane, it->ch, it->colour, s, gain, false, fillAlpha);

    if (title.isNotEmpty())
    {
        g.setColour (kTextLight);
        g.setFont (juce::Font (11.0f));
        g.drawText (title, lane.reduced (8.0f, 4.0f).removeFromTop (14.0f), juce::Justification::topLeft);
    }
    g.restoreState();
}

void ScopeView::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> lane, const std::vector<Trace>& traces, bool showHeld)
{
    auto plot = lane.reduced (38.0f, 18.0f); plot.removeFromBottom (20.0f);
    for (float db : { -96.0f, -72.0f, -48.0f, -24.0f, 0.0f })
    {
        const float y = plot.getBottom() - (db + 96.0f) / 96.0f * plot.getHeight();
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (kTextMuted); g.setFont (10.0f);
        g.drawText (juce::String ((int) db), 2, (int) y - 7, 32, 14, juce::Justification::centredRight);
    }
    const double lr = std::log (1000.0);
    for (double hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = plot.getX() + (float) (std::log (hz / 20.0) / lr) * plot.getWidth();
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (kTextMuted); g.setFont (10.0f);
        g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 0 : 1) + "k" : juce::String ((int) hz),
                    (int) x - 20, (int) plot.getBottom() + 2, 40, 14, juce::Justification::centred);
    }
    auto draw = [&] (const Trace& t, bool held)
    {
        juce::Path p;
        for (int b = 0; b < WaveLensProcessor::kSpectrumBins; ++b)
        {
            const float db = (held ? proc.heldSpectrumDb : proc.spectrumDb)[(size_t) t.ch][(size_t) b].load (relaxed);
            const float n = juce::jlimit (0.0f, 1.0f, (db + 96.0f) / 96.0f);
            const float x = plot.getX() + (float) b / (float) (WaveLensProcessor::kSpectrumBins - 1) * plot.getWidth();
            const float y = plot.getBottom() - n * plot.getHeight();
            if (b == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (t.colour.withAlpha (held ? 0.35f : 0.98f));
        g.strokePath (p, juce::PathStrokeType (held ? 1.0f : 1.5f));
    };
    if (showHeld) for (auto it = traces.rbegin(); it != traces.rend(); ++it) draw (*it, true);
    for (auto it = traces.rbegin(); it != traces.rend(); ++it) draw (*it, false);
}

void ScopeView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll (juce::Colour (0xff0f0e14));
    g.setColour (kBorderDark);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    Snapshot s;
    s.head = proc.head.load();
    s.passStart = proc.passStartCol.load();
    s.filled = proc.filled.load();
    s.has = proc.hasData.load();
    s.heldFrom = proc.heldFrom.load();
    s.heldTo = proc.heldTo.load();
    s.heldValid = proc.heldValid.load();

    const float gain = std::pow (10.0f, pGain->load() / 20.0f);
    const bool showHeld = pHold->load() > 0.5f;
    const int mask = proc.connectedMask.load (relaxed);

    std::vector<int> active;
    for (int n = 0; n < WaveLensProcessor::kMaxSignals; ++n)
        if ((mask & (1 << n)) != 0 && pShowSig[n]->load() > 0.5f)
            active.push_back (n);

    const bool split = active.size() > 1 && juce::roundToInt (pLayout->load()) == 1;
    auto area = bounds.reduced (6.0f);
    area.removeFromBottom (16.0f);

    const bool spectrum = juce::roundToInt (pView->load()) == 1;
    if (split)
    {
        const float gap = 4.0f;
        const float laneH = (area.getHeight() - gap * (float) (active.size() - 1)) / (float) active.size();
        for (int n : active)
        {
            auto lane = area.removeFromTop (laneH);
            area.removeFromTop (gap);
            if (spectrum) drawSpectrum (g, lane, { { n, signalColour (n) } }, showHeld);
            else drawLane (g, lane, { { n, signalColour (n) } }, s, gain, showHeld,
                           (n < 3 ? "SNAPBACK " : "TRANSIENT ") + juce::String (n < 3 ? n + 1 : n - 2));
        }
    }
    else
    {
        std::vector<Trace> traces;
        for (int n : active) traces.push_back ({ n, signalColour (n) });
        if (spectrum) drawSpectrum (g, area, traces, showHeld);
        else drawLane (g, area, traces, s, gain, showHeld, {});
    }

    auto footer = bounds.reduced (10.0f, 3.0f).removeFromBottom (14.0f);
    g.setFont (juce::Font (11.0f));
    g.setColour (kTextMuted);
    g.drawText (windowLabel(), footer, juce::Justification::centredLeft);
    g.drawText (juce::String (active.size()) + (active.size() == 1 ? " active trace" : " active traces"),
                footer, juce::Justification::centred);
}

//==============================================================================
MeterView::MeterView (WaveLensProcessor& p)
    : proc (p)
{
    pMeter = proc.apvts.getRawParameterValue (ids::meter);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setOpaque (true);
    startTimerHz (30);
}

void MeterView::mouseDown (const juce::MouseEvent&)
{
    if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (ids::meter)))
    {
        const int next = (juce::roundToInt (pMeter->load()) + 1) % 3;
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) next));
        param->endChangeGesture();
    }
}

void MeterView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll (kDeckBg);
    g.setColour (kBorderDark);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    const int mode = juce::roundToInt (pMeter->load());
    static const char* names[] = { "PEAK", "RMS", "CREST" };
    float now = 0.0f, hold = 0.0f, lo = -60.0f, hi = 0.0f;
    auto toDb = [] (float v) { return 20.0f * std::log10 (juce::jmax (v, 1.0e-6f)); };

    if (mode == 0) { now = proc.peakNow.load (relaxed); hold = proc.peakHold.load (relaxed); }
    else if (mode == 1) { now = proc.rmsNow.load (relaxed); hold = proc.rmsHold.load (relaxed); }
    else { now = proc.crestNow.load (relaxed); hold = proc.crestHold.load (relaxed); lo = 0.0f; hi = 24.0f; }

    auto area = bounds.reduced (6.0f);
    g.setColour (kTextLight);
    g.setFont (juce::Font (10.5f, juce::Font::bold));
    g.drawText (names[juce::jlimit (0, 2, mode)], area.removeFromTop (16.0f), juce::Justification::centred);

    auto textArea = area.removeFromBottom (32.0f);
    auto bar = area.reduced (area.getWidth() * 0.28f, 4.0f);
    g.setColour (juce::Colour (0xff100f15));
    g.fillRoundedRectangle (bar, 2.0f);

    auto norm = [&] (float db) { return juce::jlimit (0.0f, 1.0f, (db - lo) / (hi - lo)); };
    const float h = bar.getHeight() * norm (toDb (now));
    juce::ColourGradient grad (kMeterHot, 0.0f, bar.getY(), kMeterCool, 0.0f, bar.getBottom(), false);
    grad.addColour (0.25, juce::Colour (0xffffd24a));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (bar.withTrimmedTop (bar.getHeight() - h), 2.0f);

    g.setColour (kTextLight);
    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText (mode == 2 ? juce::String (toDb (now), 1) : formatDb (now), textArea.removeFromTop (16.0f), juce::Justification::centred);
    g.setColour (kTextMuted);
    g.setFont (juce::Font (9.5f));
    g.drawText (mode == 2 ? juce::String (toDb (hold), 1) : formatDb (hold), textArea, juce::Justification::centred);
}

//==============================================================================
WaveLensEditor::WaveLensEditor (WaveLensProcessor& p)
    : AudioProcessorEditor (&p), proc (p), scope (p), overlay (p), meter (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (scope);
    addAndMakeVisible (overlay);
    addAndMakeVisible (meter);

    setupCombo (viewBox, ids::view, viewAtt);
    setupCombo (layoutBox, ids::layout, layoutAtt);
    setupToggle (freezeBtn, "Freeze", ids::freeze, freezeAtt);
    setupToggle (holdBtn, "Hold", ids::hold, holdAtt);

    resetBtn.setButtonText ("Reset");
    resetBtn.onClick = [this] { proc.requestReset(); };
    addAndMakeVisible (resetBtn);

    bpmDownBtn.setButtonText ("-");
    bpmDownBtn.onClick = [this] { bpm = juce::jmax (40.0, bpm - 5.0); overlay.setBpm (bpm); repaint(); };
    addAndMakeVisible (bpmDownBtn);

    bpmUpBtn.setButtonText ("+");
    bpmUpBtn.onClick = [this] { bpm = juce::jmin (240.0, bpm + 5.0); overlay.setBpm (bpm); repaint(); };
    addAndMakeVisible (bpmUpBtn);

    triggerBtn.setButtonText ("TRIGGER");
    triggerBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff242233));
    triggerBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffeef0ff));
    triggerBtn.onClick = [this] { proc.requestTrigger(); };
    addAndMakeVisible (triggerBtn);

    preLeadSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    preLeadSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    preLeadSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff8b7cff));
    preLeadSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff262438));
    preLeadSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffd5cbff));
    preLeadSlider.setColour (juce::Slider::textBoxTextColourId, kTextLight);
    preLeadSlider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff12111a));
    preLeadSlider.setColour (juce::Slider::textBoxOutlineColourId, kBorderDark);
    preLeadSlider.setTooltip ("PRE lead offset before dry transient (0 ms to 100 ms)");
    preLeadSlider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " ms"; };
    addAndMakeVisible (preLeadSlider);
    preLeadAtt = std::make_unique<SliderAtt> (proc.apvts, ids::preRollMs, preLeadSlider);
    preLeadSlider.updateText();

    gridModeBtn.setButtonText ("Grid");
    gridModeBtn.onClick = [this] { overlay.setGridMode (true); updateViewVisibility(); };
    addAndMakeVisible (gridModeBtn);

    secondsModeBtn.setButtonText ("Seconds");
    secondsModeBtn.onClick = [this] { overlay.setGridMode (false); updateViewVisibility(); };
    addAndMakeVisible (secondsModeBtn);

    detailBtn.setButtonText ("Detail");
    detailBtn.onClick = [this] { overlay.focusTrigger (4.0f); updateEnabledStates(); };
    addAndMakeVisible (detailBtn);

    masterZoomOutBtn.setButtonText ("-");
    masterZoomOutBtn.onClick = [this] { overlay.zoomBy (1.0f / 1.6f); updateEnabledStates(); };
    addAndMakeVisible (masterZoomOutBtn);

    masterZoomResetBtn.setButtonText ("1.0x");
    masterZoomResetBtn.onClick = [this] { overlay.resetZoom(); updateEnabledStates(); };
    addAndMakeVisible (masterZoomResetBtn);

    masterZoomInBtn.setButtonText ("+");
    masterZoomInBtn.onClick = [this] { overlay.zoomBy (1.6f); updateEnabledStates(); };
    addAndMakeVisible (masterZoomInBtn);

    outputBtn.setButtonText ("Output");
    outputBtn.setTooltip ("Toggle live oscilloscope / FFT spectrum monitor");
    outputBtn.onClick = [this] { monitorMode = ! monitorMode; updateViewVisibility(); };
    addAndMakeVisible (outputBtn);

    auto initDeckBar = [this] (juce::TextButton& prevB, juce::TextButton& nextB,
                                 juce::TextButton& loadB, juce::TextButton& clrB,
                                 bool isSnapback)
    {
        prevB.setButtonText ("<");
        nextB.setButtonText (">");
        loadB.setButtonText ("Load...");
        clrB.setButtonText ("x");
        for (auto* b : { &prevB, &nextB, &loadB, &clrB }) addAndMakeVisible (*b);

        prevB.onClick = [this, isSnapback]
        {
            if (isSnapback) selectedSnapback = (selectedSnapback + 2) % 3;
            else selectedTransient = 3 + ((selectedTransient - 3 + 2) % 3);
            updateViewVisibility();
        };
        nextB.onClick = [this, isSnapback]
        {
            if (isSnapback) selectedSnapback = (selectedSnapback + 1) % 3;
            else selectedTransient = 3 + ((selectedTransient - 3 + 1) % 3);
            updateViewVisibility();
        };
        loadB.onClick = [this, isSnapback]
        {
            promptLoadLayer (isSnapback ? selectedSnapback : selectedTransient);
        };
        clrB.onClick = [this, isSnapback]
        {
            proc.clearSample (isSnapback ? selectedSnapback : selectedTransient);
            updateEnabledStates();
        };
    };
    initDeckBar (sbPrevBtn, sbNextBtn, sbLoadBtn, sbClearBtn, true);
    initDeckBar (trPrevBtn, trNextBtn, trLoadBtn, trClearBtn, false);

    for (int n = 0; n < WaveLensProcessor::kMaxSignals; ++n)
    {
        setupToggle (sigBtn[n], n < 3 ? ("SB" + juce::String (n + 1)) : ("TR" + juce::String (n - 2)), ids::showSig[n], sigAtt[n]);
        sigBtn[n].setColour (juce::TextButton::buttonOnColourId, signalColour (n));
        sigBtn[n].setColour (juce::TextButton::textColourOnId, juce::Colour (0xff111118));

        const auto col = signalColour (n);
        circleSelectBtn[n].setButtonText ("");
        circleSelectBtn[n].getProperties().set ("isCircle", true);
        circleSelectBtn[n].setColour (juce::TextButton::buttonColourId, col);
        circleSelectBtn[n].setColour (juce::TextButton::buttonOnColourId, col);
        circleSelectBtn[n].setTooltip ((n < 3 ? "SNAPBACK Stack " : "TRANSIENT Stack ") + juce::String (n < 3 ? n + 1 : n - 2));
        addAndMakeVisible (circleSelectBtn[n]);
        circleSelectBtn[n].onClick = [this, n]
        {
            if (n < 3) selectedSnapback = n;
            else selectedTransient = n;
            updateViewVisibility();
        };

        setupToggle (mutePadBtn[n], "M", ids::layerMute (n), muteAtt[n]);
        mutePadBtn[n].setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff7b00));
        mutePadBtn[n].setColour (juce::TextButton::textColourOnId, juce::Colour (0xff111118));

        setupToggle (soloPadBtn[n], "S", ids::layerSolo (n), soloAtt[n]);
        soloPadBtn[n].setColour (juce::TextButton::buttonOnColourId, col);
        soloPadBtn[n].setColour (juce::TextButton::textColourOnId, juce::Colour (0xff111118));

        auditionBtn[n].setButtonText (">");
        auditionBtn[n].onClick = [this, n] { proc.requestPreview (n); };
        addAndMakeVisible (auditionBtn[n]);

        envelopeComponents[(size_t) n] = std::make_unique<LayerEnvelopeComponent> (proc, n, col);
        addAndMakeVisible (*envelopeComponents[(size_t) n]);
    }

    setResizable (true, true);
    setResizeLimits (1020, 620, 1920, 1200);
    setSize (1280, 760);

    updateEnabledStates();
    updateViewVisibility();
    startTimerHz (10);
}

WaveLensEditor::~WaveLensEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void WaveLensEditor::setupCombo (juce::ComboBox& box, const char* paramId, std::unique_ptr<ComboAtt>& att)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (paramId)))
        box.addItemList (choice->choices, 1);
    addAndMakeVisible (box);
    att = std::make_unique<ComboAtt> (proc.apvts, paramId, box);
}

void WaveLensEditor::setupToggle (juce::TextButton& b, const juce::String& text, const juce::String& paramId,
                                  std::unique_ptr<ButtonAtt>& att)
{
    b.setButtonText (text);
    b.setClickingTogglesState (true);
    addAndMakeVisible (b);
    att = std::make_unique<ButtonAtt> (proc.apvts, paramId, b);
}

void WaveLensEditor::promptLoadLayer (int layer)
{
    const int target = juce::jlimit (0, WaveLensProcessor::kMaxSignals - 1, layer);
    auto chooser = std::make_shared<juce::FileChooser> (
        "Load sample for stack " + juce::String (target + 1),
        juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, target, chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file.existsAsFile())
                loadFileIntoLayer (target, file);
        });
}

bool WaveLensEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff")
            || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".ogg") || f.endsWithIgnoreCase (".mp3"))
            return true;
    return false;
}

void WaveLensEditor::filesDropped (const juce::StringArray& files, int x, int)
{
    if (files.isEmpty()) return;
    const int targetLayer = (x < getWidth() / 2) ? selectedSnapback : selectedTransient;
    loadFileIntoLayer (targetLayer, juce::File (files[0]));
}

void WaveLensEditor::loadFileIntoLayer (int layer, const juce::File& file)
{
    juce::String error;
    if (! proc.loadSampleFile (layer, file, error))
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Sample load failed", error);
    updateEnabledStates();
}

void WaveLensEditor::updateEnabledStates()
{
    const int mask = proc.connectedMask.load();
    int loadedCount = 0;
    for (int n = 0; n < WaveLensProcessor::kMaxSignals; ++n)
    {
        const bool loaded = (mask & (1 << n)) != 0;
        if (loaded) ++loadedCount;
        sigBtn[n].setEnabled (loaded);
        auditionBtn[n].setEnabled (loaded);
        displayedSampleNames[(size_t) n] = proc.getSampleName (n);
    }
    layoutBox.setEnabled (loadedCount > 1);
    sbClearBtn.setEnabled ((mask & (1 << selectedSnapback)) != 0);
    trClearBtn.setEnabled ((mask & (1 << selectedTransient)) != 0);
    sbLoadBtn.setButtonText (displayedSampleNames[(size_t) selectedSnapback].isEmpty() ? "Load..." : "Replace");
    trLoadBtn.setButtonText (displayedSampleNames[(size_t) selectedTransient].isEmpty() ? "Load..." : "Replace");

    const float z = overlay.getZoomFactor();
    masterZoomResetBtn.setButtonText (z <= 1.02f ? "1.0x" : (z >= 10.0f ? juce::String (juce::roundToInt (z)) + "x" : juce::String (z, 1) + "x"));
    masterZoomOutBtn.setEnabled (z > 1.01f);
    masterZoomInBtn.setEnabled (z < 31.99f);
}

void WaveLensEditor::updateViewVisibility()
{
    overlay.setSelectedPair (selectedSnapback, selectedTransient);
    overlay.setVisible (! monitorMode);
    scope.setVisible (monitorMode);
    meter.setVisible (monitorMode);
    viewBox.setVisible (monitorMode);
    layoutBox.setVisible (monitorMode);
    freezeBtn.setVisible (monitorMode);
    holdBtn.setVisible (monitorMode);

    gridModeBtn.setColour (juce::TextButton::buttonColourId, overlay.isGridMode() ? juce::Colour (0xff2a2840) : juce::Colour (0xff111018));
    gridModeBtn.setColour (juce::TextButton::textColourOffId, overlay.isGridMode() ? juce::Colour (0xffb4c0ff) : kTextMuted);
    secondsModeBtn.setColour (juce::TextButton::buttonColourId, ! overlay.isGridMode() ? juce::Colour (0xff2a2840) : juce::Colour (0xff111018));
    secondsModeBtn.setColour (juce::TextButton::textColourOffId, ! overlay.isGridMode() ? juce::Colour (0xffc79bff) : kTextMuted);
    outputBtn.setColour (juce::TextButton::buttonColourId, monitorMode ? juce::Colour (0xff2c2546) : juce::Colour (0xff1d1c28));

    for (int n = 0; n < WaveLensProcessor::kMaxSignals; ++n)
    {
        sigBtn[n].setVisible (monitorMode);
        const bool isActiveInDeck = (n == selectedSnapback || n == selectedTransient);
        mutePadBtn[n].setVisible (isActiveInDeck);
        soloPadBtn[n].setVisible (isActiveInDeck);
        auditionBtn[n].setVisible (isActiveInDeck);
        if (envelopeComponents[(size_t) n] != nullptr)
            envelopeComponents[(size_t) n]->setVisible (isActiveInDeck);
    }

    updateEnabledStates();
    resized();
    repaint();
}

void WaveLensEditor::timerCallback()
{
    proc.collectSampleGarbage();
    updateEnabledStates();
    repaint (dividerArea);
}

void WaveLensEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBgObsidian);

    // Top Header Bar
    g.setColour (kHeaderBg);
    g.fillRect (topHeaderArea);
    g.setColour (kBorderDark);
    g.drawHorizontalLine (topHeaderArea.getBottom(), 0.0f, (float) getWidth());

    // Stylized SNAPBACK centered title
    juce::ColourGradient titleGrad (juce::Colour (0xff7d92ff), (float) getWidth() * 0.42f, 0.0f,
                                    juce::Colour (0xffc77dff), (float) getWidth() * 0.58f, 0.0f, false);
    g.setGradientFill (titleGrad);
    g.setFont (juce::Font (18.0f, juce::Font::bold));
    g.drawText ("S N A P B A C K", topHeaderArea, juce::Justification::centred);

    g.setColour (kTextLight);
    g.setFont (juce::Font (11.0f));
    const int bpmX = bpmDownBtn.getRight() + 2;
    const int bpmW = juce::jmax (36, bpmUpBtn.getX() - bpmX - 2);
    g.drawText (juce::String ((int) bpm) + " BPM",
                bpmX, topHeaderArea.getY(), bpmW, topHeaderArea.getHeight(),
                juce::Justification::centred);

    // Center Divider Bar
    g.setColour (kDividerBg);
    g.fillRect (dividerArea);
    g.setColour (kBorderDark);
    g.drawHorizontalLine (dividerArea.getY(), 0.0f, (float) getWidth());
    g.drawHorizontalLine (dividerArea.getBottom(), 0.0f, (float) getWidth());

    g.setColour (kTextMuted);
    g.setFont (juce::Font (11.0f));
    g.drawText ("Limiter  " + formatDb (proc.peakNow.load (relaxed)),
                dividerArea.getX() + 14, dividerArea.getY(), 120, dividerArea.getHeight(),
                juce::Justification::centredLeft);

    g.setColour (kSnapbackHdr);
    g.setFont (juce::Font (10.5f, juce::Font::bold));
    g.drawText ("PRE", preLeadSlider.getX() - 30, dividerArea.getY(), 28, dividerArea.getHeight(),
                juce::Justification::centredRight);

    // Dual Deck Containers
    auto drawDeck = [&] (juce::Rectangle<int> area, juce::Rectangle<int> hdr,
                         const juce::String& title, juce::Colour titleCol, int selectedIdx,
                         const juce::TextButton& firstRightBtn)
    {
        g.setColour (kDeckBg);
        g.fillRoundedRectangle (area.toFloat(), 5.0f);
        g.setColour (kBorderDark);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 5.0f, 1.0f);

        g.setColour (kDeckHdrBg);
        g.fillRect (hdr);
        g.setColour (kBorderDark);
        g.drawHorizontalLine (hdr.getBottom(), (float) hdr.getX(), (float) hdr.getRight());

        g.setColour (titleCol);
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText (title, hdr.getX() + 12, hdr.getY(), 96, hdr.getHeight(), juce::Justification::centredLeft);

        // Ring around currently active circle
        const auto activeCircleBounds = circleSelectBtn[selectedIdx].getBounds().toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawEllipse (activeCircleBounds.expanded (2.5f), 1.5f);

        const auto sampleName = proc.getSampleName (selectedIdx);
        const int stackNum = (selectedIdx < 3) ? (selectedIdx + 1) : (selectedIdx - 2);
        const bool isMissing = proc.isLayerSampleMissing (selectedIdx);
        juce::String subtitle;
        if (isMissing)
            subtitle = "Missing: " + proc.getMissingSampleFileName (selectedIdx);
        else if (sampleName.isEmpty())
            subtitle = "Stack " + juce::String (stackNum) + " (Empty)";
        else
            subtitle = sampleName;

        const int subX = hdr.getX() + 176;
        const int subW = juce::jmax (30, firstRightBtn.getX() - subX - 6);
        g.setColour (isMissing ? juce::Colour (0xffffb86c) : kTextMuted);
        g.setFont (juce::Font (11.0f));
        g.drawText (subtitle, subX, hdr.getY(), subW, hdr.getHeight(),
                    juce::Justification::centredLeft, true);
    };

    drawDeck (snapbackDeckArea, snapbackHeaderArea, "SNAPBACK", kSnapbackHdr, selectedSnapback, sbPrevBtn);
    drawDeck (transientDeckArea, transientHeaderArea, "TRANSIENT", kTransientHdr, selectedTransient, trPrevBtn);
}

void WaveLensEditor::resized()
{
    auto area = getLocalBounds();
    topHeaderArea = area.removeFromTop (38);

    auto hdr = topHeaderArea.reduced (12, 7);
    resetBtn.setBounds (hdr.removeFromLeft (56));
    if (monitorMode)
    {
        hdr.removeFromLeft (6);
        freezeBtn.setBounds (hdr.removeFromLeft (56));
        hdr.removeFromLeft (4);
        holdBtn.setBounds (hdr.removeFromLeft (52));
    }
    bpmUpBtn.setBounds (hdr.removeFromRight (22));
    hdr.removeFromRight (54);
    bpmDownBtn.setBounds (hdr.removeFromRight (22));

    const int bodyH = area.getHeight();
    const int divH = 34;
    const int topH = juce::roundToInt ((float) (bodyH - divH) * 0.49f);

    topCanvasArea = area.removeFromTop (topH);
    dividerArea = area.removeFromTop (divH);

    if (monitorMode)
    {
        auto mon = topCanvasArea.reduced (8);
        monitorToolbarArea = mon.removeFromTop (30);
        auto tb = monitorToolbarArea.reduced (4, 3);
        viewBox.setBounds (tb.removeFromLeft (105));
        tb.removeFromLeft (6);
        layoutBox.setBounds (tb.removeFromLeft (95));
        tb.removeFromLeft (10);
        for (int n = 0; n < WaveLensProcessor::kMaxSignals; ++n)
        {
            sigBtn[n].setBounds (tb.removeFromLeft (36));
            tb.removeFromLeft (4);
        }
        mon.removeFromTop (6);
        meter.setBounds (mon.removeFromRight (64));
        mon.removeFromRight (6);
        scope.setBounds (mon);
    }
    else
    {
        overlay.setBounds (topCanvasArea);
    }

    // Divider bar controls
    const int trigX = dividerArea.getCentreX() - 58;
    triggerBtn.setBounds (trigX, dividerArea.getY() - 5, 116, 30);

    const int preX = dividerArea.getX() + 172;
    const int preMaxW = juce::jmax (110, trigX - preX - 10);
    preLeadSlider.setBounds (preX, dividerArea.getY() + 5, juce::jmin (164, preMaxW), 24);

    auto divRight = dividerArea.reduced (12, 6);
    outputBtn.setBounds (divRight.removeFromRight (58));
    divRight.removeFromRight (6);
    masterZoomInBtn.setBounds (divRight.removeFromRight (22));
    masterZoomResetBtn.setBounds (divRight.removeFromRight (42));
    masterZoomOutBtn.setBounds (divRight.removeFromRight (22));
    divRight.removeFromRight (6);
    detailBtn.setBounds (divRight.removeFromRight (52));
    divRight.removeFromRight (8);
    secondsModeBtn.setBounds (divRight.removeFromRight (58));
    gridModeBtn.setBounds (divRight.removeFromRight (46));

    // Bottom 2-Column Decks
    auto bottom = area.reduced (8);
    const int colW = (bottom.getWidth() - 8) / 2;
    snapbackDeckArea = bottom.removeFromLeft (colW);
    bottom.removeFromLeft (8);
    transientDeckArea = bottom;

    auto layoutDeck = [&] (juce::Rectangle<int> deck, juce::Rectangle<int>& hdrOut,
                           int baseOffset, int selectedIdx,
                           juce::TextButton& prevB, juce::TextButton& nextB,
                           juce::TextButton& loadB, juce::TextButton& clrB)
    {
        auto inner = deck;
        hdrOut = inner.removeFromTop (34);

        // 3 small colored circles immediately right after SNAPBACK / TRANSIENT title
        const int cy = hdrOut.getCentreY();
        for (int slot = 0; slot < 3; ++slot)
            circleSelectBtn[baseOffset + slot].setBounds (hdrOut.getX() + 112 + slot * 20, cy - 7, 14, 14);

        auto rightBtns = hdrOut.reduced (8, 7);
        clrB.setBounds (rightBtns.removeFromRight (20));
        rightBtns.removeFromRight (3);
        loadB.setBounds (rightBtns.removeFromRight (56));
        rightBtns.removeFromRight (3);
        auditionBtn[selectedIdx].setBounds (rightBtns.removeFromRight (22));
        rightBtns.removeFromRight (3);
        soloPadBtn[selectedIdx].setBounds (rightBtns.removeFromRight (22));
        rightBtns.removeFromRight (3);
        mutePadBtn[selectedIdx].setBounds (rightBtns.removeFromRight (22));
        rightBtns.removeFromRight (4);
        nextB.setBounds (rightBtns.removeFromRight (20));
        rightBtns.removeFromRight (2);
        prevB.setBounds (rightBtns.removeFromRight (20));

        auto body = inner.reduced (6);
        for (int slot = 0; slot < 3; ++slot)
        {
            const int idx = baseOffset + slot;
            if (envelopeComponents[(size_t) idx] != nullptr)
                envelopeComponents[(size_t) idx]->setBounds (body);
        }
    };

    layoutDeck (snapbackDeckArea, snapbackHeaderArea, 0, selectedSnapback,
                sbPrevBtn, sbNextBtn, sbLoadBtn, sbClearBtn);
    layoutDeck (transientDeckArea, transientHeaderArea, 3, selectedTransient,
                trPrevBtn, trNextBtn, trLoadBtn, trClearBtn);
}
