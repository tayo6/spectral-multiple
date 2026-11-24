#include "MasterWaveformComponent.h"

#include <cmath>

namespace ui = wavelens::ui;

namespace
{
    constexpr auto relaxed = std::memory_order_relaxed;

    juce::String formatTimeTickLabel (double ms)
    {
        const double absMs = std::abs (ms);
        if (absMs < 0.05) return "0.00s";
        if (absMs >= 1000.0) return juce::String (ms / 1000.0, 2) + "s";
        if (absMs >= 100.0) return juce::String (ms / 1000.0, 3) + "s";
        if (absMs >= 10.0) return juce::String (ms, 1) + "ms";
        return juce::String (ms, 2) + "ms";
    }
}

MasterWaveformComponent::MasterWaveformComponent (WaveLensProcessor& processor)
    : proc (processor)
{
    auto& a = proc.apvts;
    pView        = a.getRawParameterValue (ids::view);
    pCapture     = a.getRawParameterValue (ids::capture);
    pBars        = a.getRawParameterValue (ids::bars);
    pTimeMs      = a.getRawParameterValue (ids::timeMs);
    pGain        = a.getRawParameterValue (ids::gain);
    pHold        = a.getRawParameterValue (ids::hold);
    pThreshold   = a.getRawParameterValue (ids::threshold);
    pTriggerMode = a.getRawParameterValue (ids::triggerMode);
    pPreRollMs   = a.getRawParameterValue (ids::preRollMs);

    lastObservedTriggerCount = proc.getTriggerCount();

    masterBtn.setToggleState (true, juce::dontSendNotification);
    masterBtn.onClick = [this]
    {
        if (masterBtn.getToggleState())
            outputMonitorMode = false;
        if (onMasterToggled)
            onMasterToggled (masterBtn.getToggleState());
        repaint();
    };
    addAndMakeVisible (masterBtn);

    setOpaque (true);
    startTimerHz (30);
}

MasterWaveformComponent::~MasterWaveformComponent()
{
    stopTimer();
}

void MasterWaveformComponent::setSelectedPair (int snapbackLayer, int transientLayer) noexcept
{
    selectedSnapback  = juce::jlimit (0, 2, snapbackLayer);
    selectedTransient = juce::jlimit (3, 5, transientLayer);
    repaint();
}

void MasterWaveformComponent::setBpm (double newBpm) noexcept
{
    bpm = juce::jlimit (40.0, 240.0, newBpm);
    repaint();
}

void MasterWaveformComponent::setGridMode (bool useGrid) noexcept
{
    gridMode = useGrid;
    repaint();
}

void MasterWaveformComponent::setZoomBars (double barsCount) noexcept
{
    windowBars = juce::jlimit (0.125, 8.0, barsCount);
    repaint();
}

void MasterWaveformComponent::setMasterOverlayActive (bool active)
{
    masterBtn.setToggleState (active, juce::dontSendNotification);
    if (active)
        outputMonitorMode = false;
    repaint();
}

void MasterWaveformComponent::setOutputMonitorActive (bool active)
{
    outputMonitorMode = active;
    if (active)
        masterBtn.setToggleState (false, juce::dontSendNotification);
    repaint();
}

void MasterWaveformComponent::clampViewport (float nextStart, float nextZoom) noexcept
{
    zoomFactor = juce::jlimit (1.0f, 32.0f, nextZoom);
    const float span = 1.0f / zoomFactor;
    viewStartNorm = zoomFactor <= 1.0001f ? 0.0f : juce::jlimit (0.0f, juce::jmax (0.0f, 1.0f - span), nextStart);
    if (zoomFactor <= 1.0001f)
        zoomFactor = 1.0f;
    if (onZoomChanged)
        onZoomChanged (zoomFactor);
}

float MasterWaveformComponent::normToX (float norm, float plotX, float plotW) const noexcept
{
    const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
    return plotX + ((norm - viewStartNorm) / span) * plotW;
}

float MasterWaveformComponent::xToNorm (float x, float plotX, float plotW) const noexcept
{
    const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
    return juce::jlimit (0.0f, 1.0f, viewStartNorm + ((x - plotX) / juce::jmax (1.0f, plotW)) * span);
}

void MasterWaveformComponent::zoomBy (float factor, float anchorNorm) noexcept
{
    const float curSpan = 1.0f / juce::jmax (1.0f, zoomFactor);
    const float anchorRatio = juce::jlimit (0.0f, 1.0f, (anchorNorm - viewStartNorm) / curSpan);
    const float nextZoom = juce::jlimit (1.0f, 32.0f, zoomFactor * factor);
    const float nextSpan = 1.0f / nextZoom;
    clampViewport (anchorNorm - anchorRatio * nextSpan, nextZoom);
    repaint();
}

void MasterWaveformComponent::resetZoom() noexcept
{
    clampViewport (0.0f, 1.0f);
    repaint();
}

void MasterWaveformComponent::focusTrigger (float zoomIfUnzoomed) noexcept
{
    if (zoomFactor > 1.2f)
    {
        resetZoom();
    }
    else
    {
        const float nextZoom = juce::jlimit (1.0f, 32.0f, zoomIfUnzoomed);
        clampViewport (0.5f - 0.5f / nextZoom, nextZoom);
        repaint();
    }
}

void MasterWaveformComponent::timerCallback()
{
    const auto trig = proc.getTriggerCount();
    if (trig != lastObservedTriggerCount)
    {
        lastObservedTriggerCount = trig;
        triggerFlashAlpha = 1.0f;
    }
    else if (triggerFlashAlpha > 0.0f)
    {
        triggerFlashAlpha = juce::jmax (0.0f, triggerFlashAlpha - 0.08f);
    }
    repaint();
}

void MasterWaveformComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (outputMonitorMode || std::abs (wheel.deltaY) < 1.0e-4f)
        return;
    const float anchor = xToNorm (e.position.x, 0.0f, juce::jmax (1.0f, (float) getWidth()));
    zoomBy (wheel.deltaY > 0 ? 1.28f : 1.0f / 1.28f, anchor);
}

void MasterWaveformComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    if (! outputMonitorMode)
        resetZoom();
}

void MasterWaveformComponent::mouseDown (const juce::MouseEvent& e)
{
    dragStartX = e.position.x;
    dragStartView = viewStartNorm;
}

void MasterWaveformComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (outputMonitorMode || zoomFactor <= 1.01f)
        return;
    const float dx = e.position.x - dragStartX;
    const float span = 1.0f / zoomFactor;
    clampViewport (dragStartView - (dx / juce::jmax (1.0f, (float) getWidth())) * span, zoomFactor);
    repaint();
}

void MasterWaveformComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (ui::kMainBg);
    g.fillRoundedRectangle (bounds, 4.0f);

    if (outputMonitorMode)
    {
        const bool spectrum = pView != nullptr && juce::roundToInt (pView->load()) == 1;
        if (spectrum)
            paintLiveSpectrum (g, bounds.reduced (6.0f));
        else
            paintLiveOutputMonitor (g, bounds.reduced (6.0f));
    }
    else
    {
        constexpr float kRulerH = 22.0f;
        auto inner = bounds.reduced (1.0f);
        auto ruler = inner.removeFromTop (kRulerH);
        paintTimelineWaveforms (g, inner, ruler);
    }

    g.setColour (ui::kBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
}

void MasterWaveformComponent::paintTimelineWaveforms (juce::Graphics& g,
                                                      juce::Rectangle<float> plot,
                                                      juce::Rectangle<float> ruler)
{
    g.setColour (ui::kHeaderBg);
    g.fillRect (ruler);
    g.setColour (ui::kBorder);
    g.drawHorizontalLine (juce::roundToInt (ruler.getBottom() - 0.5f), ruler.getX(), ruler.getRight());

    const float plotX = plot.getX();
    const float plotY = plot.getY();
    const float plotW = plot.getWidth();
    const float plotH = plot.getHeight();
    if (plotW < 32.0f || plotH < 32.0f)
        return;

    const float span = 1.0f / juce::jmax (1.0f, zoomFactor);
    const float viewEnd = juce::jmin (1.0f, viewStartNorm + span);
    const double windowMs = windowBars * (4.0 * 60000.0) / bpm;

    g.setFont (juce::Font (10.0f));
    if (gridMode)
    {
        int divisions = 8;
        int majorStep = 2;
        if (zoomFactor >= 12.0f) { divisions = 64; majorStep = 4; }
        else if (zoomFactor >= 4.5f) { divisions = 32; majorStep = 2; }
        else if (zoomFactor >= 1.8f) { divisions = 16; majorStep = 4; }

        const int firstIdx = juce::jmax (0, (int) std::floor (viewStartNorm * (float) divisions));
        const int lastIdx  = juce::jmin (divisions, (int) std::ceil (viewEnd * (float) divisions));
        for (int i = firstIdx; i <= lastIdx; ++i)
        {
            const float norm = (float) i / (float) divisions;
            const float tx = normToX (norm, plotX, plotW);
            const bool major = (i % majorStep == 0);
            g.setColour (major ? ui::kBorder.withAlpha (0.85f) : ui::kBorder.withAlpha (0.35f));
            g.drawVerticalLine (juce::roundToInt (tx), plotY, plot.getBottom());

            if (major && tx > (float) masterBtn.getRight() + 14.0f)
            {
                const double beatPos = norm * windowBars * 4.0;
                const int barNum = (int) std::floor (beatPos / 4.0) + 1;
                const int beatInBar = ((int) std::floor (beatPos)) % 4 + 1;
                const juce::String lbl = juce::String (barNum) + "." + juce::String (beatInBar);
                g.setColour (ui::kSecondaryText);
                g.drawText (lbl, juce::roundToInt (tx - 24.0f), juce::roundToInt (ruler.getY()),
                            48, juce::roundToInt (ruler.getHeight()), juce::Justification::centred);
            }
        }
    }
    else
    {
        const double visibleMs = windowMs / (double) zoomFactor;
        static constexpr double candidates[] = { 2000.0, 1000.0, 500.0, 250.0, 100.0, 50.0, 25.0, 10.0, 5.0, 2.0, 1.0 };
        double stepMs = 250.0;
        for (double c : candidates)
        {
            const double count = visibleMs / c;
            if (count >= 4.0 && count <= 10.0) { stepMs = c; break; }
        }
        const double minorStep = stepMs * 0.5;
        const int firstIdx = juce::jmax (0, (int) std::floor ((viewStartNorm * windowMs) / minorStep));
        const int lastIdx  = juce::jmin ((int) std::ceil (windowMs / minorStep), (int) std::ceil ((viewEnd * windowMs) / minorStep));
        for (int i = firstIdx; i <= lastIdx; ++i)
        {
            const double ms = (double) i * minorStep;
            const float norm = juce::jlimit (0.0f, 1.0f, (float) (ms / windowMs));
            const float tx = normToX (norm, plotX, plotW);
            const bool major = (i % 2 == 0);
            g.setColour (major ? ui::kBorder.withAlpha (0.85f) : ui::kBorder.withAlpha (0.35f));
            g.drawVerticalLine (juce::roundToInt (tx), plotY, plot.getBottom());

            if (major && tx > (float) masterBtn.getRight() + 14.0f)
            {
                g.setColour (ui::kSecondaryText);
                g.drawText (formatTimeTickLabel (ms), juce::roundToInt (tx - 28.0f), juce::roundToInt (ruler.getY()),
                            56, juce::roundToInt (ruler.getHeight()), juce::Justification::centred);
            }
        }
    }

    // Subtle horizontal reference lines & central waveform baseline
    const float centre = plot.getCentreY();
    const float halfHeight = plotH * 0.43f;
    g.setColour (ui::kBorder.withAlpha (0.35f));
    g.drawHorizontalLine (juce::roundToInt (centre - halfHeight * 0.5f), plotX, plot.getRight());
    g.drawHorizontalLine (juce::roundToInt (centre + halfHeight * 0.5f), plotX, plot.getRight());
    g.setColour (ui::kSecondaryText.withAlpha (0.32f));
    g.drawHorizontalLine (juce::roundToInt (centre), plotX, plot.getRight());

    // Threshold horizontal guide when Audio / Both trigger mode is active
    const int trigMode = pTriggerMode != nullptr ? juce::roundToInt (pTriggerMode->load()) : 2;
    if (trigMode == 0 || trigMode == 2)
    {
        const float thresh = pThreshold != nullptr ? pThreshold->load() : 0.03f;
        const float thYTop = centre - juce::jlimit (0.01f, 1.0f, thresh) * halfHeight;
        const float thYBot = centre + juce::jlimit (0.01f, 1.0f, thresh) * halfHeight;
        g.setColour (ui::kInputWave.withAlpha (0.28f));
        const float dash[] = { 3.0f, 4.0f };
        g.drawDashedLine ({ plotX, thYTop, plot.getRight(), thYTop }, dash, 2, 1.0f);
        g.drawDashedLine ({ plotX, thYBot, plot.getRight(), thYBot }, dash, 2, 1.0f);
    }

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    // Live input audio waveform ghost in grey (#A2A0AA) if audio has arrived
    if (proc.hasData.load())
    {
        const int filledCols = juce::jlimit (0, WaveLensProcessor::kCols, proc.filled.load());
        if (filledCols > 2)
        {
            juce::Path inPath;
            const int step = juce::jmax (1, filledCols / 256);
            for (int c = 0; c < filledCols; c += step)
            {
                const float px = plotX + ((float) c / (float) WaveLensProcessor::kCols) * plotW;
                const float pk = proc.colMax[(size_t) WaveLensProcessor::idx (0, c)].load (relaxed);
                const float py = centre - juce::jlimit (-1.0f, 1.0f, pk) * halfHeight * 0.65f;
                if (c == 0) inPath.startNewSubPath (px, py);
                else inPath.lineTo (px, py);
            }
            g.setColour (ui::kInputWave.withAlpha (0.28f));
            g.strokePath (inPath, juce::PathStrokeType (1.0f));
        }
    }

    bool anySolo = false;
    for (int l = 0; l < WaveLensProcessor::kMaxSignals; ++l)
        if (proc.apvts.getRawParameterValue (ids::layerSolo (l))->load() > 0.5f)
            anySolo = true;

    const bool masterOverlay = masterBtn.getToggleState();
    int loadedCount = 0;
    const float preLeadMs = pPreRollMs != nullptr ? pPreRollMs->load() : 50.0f;

    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        const auto preview = proc.getWaveformPreview (layer);
        if (! preview.loaded)
            continue;
        ++loadedCount;

        const bool isSelected = (layer == selectedSnapback || layer == selectedTransient);
        if (! masterOverlay && ! isSelected)
            continue;

        const bool muted   = proc.apvts.getRawParameterValue (ids::layerMute (layer))->load() > 0.5f;
        const bool solo    = proc.apvts.getRawParameterValue (ids::layerSolo (layer))->load() > 0.5f;
        const bool audible = ! muted && (! anySolo || solo);

        const bool reversed  = proc.apvts.getRawParameterValue (ids::layerReverse (layer))->load() > 0.5f;
        const float trimStart = juce::jlimit (0.0f, 0.99f, proc.apvts.getRawParameterValue (ids::layerTrimStart (layer))->load());
        const float trimEnd   = juce::jlimit (trimStart + 0.005f, 1.0f, proc.apvts.getRawParameterValue (ids::layerTrimEnd (layer))->load());
        const float pitch     = proc.apvts.getRawParameterValue (ids::layerPitch (layer))->load();
        const float shiftMs   = proc.apvts.getRawParameterValue (ids::layerDelay (layer))->load();
        const float fadeInMs  = proc.apvts.getRawParameterValue (ids::layerFadeIn (layer))->load();
        const float fadeOutMs = proc.apvts.getRawParameterValue (ids::layerFadeOut (layer))->load();
        const float gainScale = juce::jlimit (0.0f, 2.0f, proc.apvts.getRawParameterValue (ids::layerGain (layer))->load());
        const bool envOn      = proc.apvts.getRawParameterValue (ids::layerEnvEnabled (layer))->load() > 0.5f;

        const float preOffsetMs = (layer < LayeredSampleEngine::kPreLayers) ? preLeadMs : 0.0f;
        const double effectiveOffsetMs = juce::jmax (-LayeredSampleEngine::kMaximumPreRollMilliseconds,
                                                     (double) shiftMs - (double) preOffsetMs);
        const double trimmedMs  = juce::jmax (1.0, preview.durationMs * (double) (trimEnd - trimStart));
        const double playbackMs = juce::jmax (1.0, trimmedMs / std::pow (2.0, (double) pitch / 12.0));
        const float startNorm   = 0.5f + (float) (effectiveOffsetMs / windowMs);
        const float spanNorm    = (float) (playbackMs / windowMs);
        const float x0 = normToX (startNorm, plotX, plotW);
        const float x1 = normToX (startNorm + spanNorm, plotX, plotW);
        if (x1 < plotX - 2.0f || x0 > plot.getRight() + 2.0f)
            continue;

        const float visX0 = juce::jmax (plotX, x0);
        const float visX1 = juce::jmin (plot.getRight(), x1);
        const int steps = juce::jlimit (48, 420, juce::roundToInt ((visX1 - visX0) * 1.5f));

        LayeredSampleEngine::LayerParameters lp;
        lp.gain           = gainScale;
        lp.pitchSemitones = pitch;
        lp.attackMs       = proc.apvts.getRawParameterValue (ids::layerAttack (layer))->load();
        lp.holdMs         = proc.apvts.getRawParameterValue (ids::layerHold (layer))->load();
        lp.decayMs        = proc.apvts.getRawParameterValue (ids::layerDecay (layer))->load();
        lp.sustain        = proc.apvts.getRawParameterValue (ids::layerSustain (layer))->load();
        lp.releaseMs      = proc.apvts.getRawParameterValue (ids::layerRelease (layer))->load();
        lp.attackCurve    = juce::roundToInt (proc.apvts.getRawParameterValue (ids::layerAttackCurve (layer))->load());
        lp.decayCurve     = juce::roundToInt (proc.apvts.getRawParameterValue (ids::layerDecayCurve (layer))->load());
        lp.releaseCurve   = juce::roundToInt (proc.apvts.getRawParameterValue (ids::layerReleaseCurve (layer))->load());
        lp.attackTension  = proc.apvts.getRawParameterValue (ids::layerAttackTension (layer))->load();
        lp.decayTension   = proc.apvts.getRawParameterValue (ids::layerDecayTension (layer))->load();
        lp.releaseTension = proc.apvts.getRawParameterValue (ids::layerReleaseTension (layer))->load();
        lp.envEnabled     = envOn;
        lp.reverse        = reversed;
        lp.trimStart      = trimStart;
        lp.trimEnd        = trimEnd;
        lp.fadeInMs       = fadeInMs;
        lp.fadeOutMs      = fadeOutMs;

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

            const float elapsedMs   = u * totalPlayMs;
            const float remainingMs = (1.0f - u) * totalPlayMs;
            const float fadeInG  = fadeInMs > 0.5f ? juce::jlimit (0.0f, 1.0f, elapsedMs / fadeInMs) : 1.0f;
            const float fadeOutG = fadeOutMs > 0.5f ? juce::jlimit (0.0f, 1.0f, remainingMs / fadeOutMs) : 1.0f;
            const float flEnv    = LayeredSampleEngine::evaluateCompleteEnvelopeAtMs (lp, sched, elapsedMs);
            const float env      = gainScale * fadeInG * fadeOutG * flEnv;

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

        const float alpha = ! audible ? 0.16f : (masterOverlay ? (isSelected ? 0.96f : 0.72f) : 1.0f);
        const auto col = ui::layerColour (layer);
        g.setColour (col.withAlpha (alpha * (isSelected ? 0.22f : 0.12f)));
        g.fillPath (area);
        g.setColour (col.withAlpha (alpha));
        g.strokePath (upper, juce::PathStrokeType (isSelected ? 1.7f : 1.15f));
        g.setColour (col.withAlpha (alpha * 0.68f));
        g.strokePath (lower, juce::PathStrokeType (isSelected ? 1.2f : 0.85f));
    }

    // Trigger line + cyan diamond marker at the top
    const float trigX = normToX (0.5f, plotX, plotW);
    if (trigX >= plotX - 4.0f && trigX <= plot.getRight() + 4.0f)
    {
        if (triggerFlashAlpha > 0.01f)
        {
            g.setColour (ui::kCyanAccent.withAlpha (0.18f * triggerFlashAlpha));
            g.fillRect (trigX - 5.0f, plotY, 10.0f, plotH);
        }

        g.setColour (ui::kCyanAccent.withAlpha (0.85f + 0.15f * triggerFlashAlpha));
        g.drawLine (trigX, plotY + 6.0f, trigX, plot.getBottom(), 1.35f);

        juce::Path diamond;
        diamond.startNewSubPath (trigX, plotY + 2.0f);
        diamond.lineTo (trigX + 5.0f, plotY + 7.0f);
        diamond.lineTo (trigX, plotY + 12.0f);
        diamond.lineTo (trigX - 5.0f, plotY + 7.0f);
        diamond.closeSubPath();
        g.fillPath (diamond);
    }

    if (loadedCount == 0)
    {
        g.setColour (ui::kSecondaryText);
        g.setFont (juce::Font (12.5f));
        g.drawText ((masterOverlay ? "MASTER OVERLAY  -  " : "")
                        + juce::String ("Load samples into SNAPBACK (left) or TRANSIENT (right) below"),
                    plot.toNearestInt(), juce::Justification::centred);
    }

    // Top-right badge showing active mode + zoom
    g.setFont (juce::Font (10.0f, juce::Font::bold));
    const juce::String badgeText = (masterOverlay ? "MASTER ALL 6" : "SELECTED VIEW")
        + (zoomFactor > 1.02f ? ("  |  " + juce::String (zoomFactor, 1) + "x") : juce::String());
    g.setColour (ui::kSecondaryText);
    g.drawText (badgeText, plot.reduced (10.0f, 6.0f).removeFromTop (16.0f), juce::Justification::topRight);

    g.restoreState();
}

void MasterWaveformComponent::paintLiveOutputMonitor (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.saveState();
    g.reduceClipRegion (area.toNearestInt());

    const float gain = std::pow (10.0f, (pGain != nullptr ? pGain->load() : 0.0f) / 20.0f);
    const int captureMode = pCapture != nullptr ? juce::roundToInt (pCapture->load()) : 0;
    const bool scroll = captureMode == 2;
    const int head = proc.head.load();
    const int passStart = proc.passStartCol.load();
    const int filled = proc.filled.load();
    const bool has = proc.hasData.load();

    // Subtle vertical grid + baseline
    g.setColour (ui::kBorder.withAlpha (0.45f));
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine (juce::roundToInt (area.getX() + area.getWidth() * (float) i / 8.0f),
                            area.getY(), area.getBottom());

    const float centre = area.getCentreY();
    const float half = area.getHeight() * 0.44f;
    g.setColour (ui::kSecondaryText.withAlpha (0.35f));
    g.drawHorizontalLine (juce::roundToInt (centre), area.getX(), area.getRight());

    // Draw summed Master Output across all active signals in designated Master Output colour (#48D6D0)
    constexpr int N = WaveLensProcessor::kCols;
    const int first = scroll ? ((head - juce::jlimit (0, N, filled) + 1) % N + N) % N : passStart;
    const int count = scroll ? juce::jlimit (0, N, filled) : (head - passStart + 1);
    const int mask = proc.connectedMask.load (relaxed);

    if (has && count >= 2)
    {
        const float colW = area.getWidth() / (float) N;
        auto xOf = [&] (int j)
        {
            return scroll ? area.getRight() - ((float) (count - j) - 0.5f) * colW
                          : area.getX() + ((float) (first + j) + 0.5f) * colW;
        };
        auto yOf = [&] (float v) { return centre - juce::jlimit (-1.0f, 1.0f, v * gain) * half; };

        juce::Path masterPath;
        for (int j = 0; j < count; ++j)
        {
            const int col = (first + j) % N;
            float hiSum = 0.0f;
            for (int ch = 0; ch < WaveLensProcessor::kNumChan; ++ch)
                if ((mask & (1 << ch)) != 0)
                    hiSum += proc.colMax[(size_t) WaveLensProcessor::idx (ch, col)].load (relaxed);
            const float y = yOf (hiSum);
            if (j == 0) masterPath.startNewSubPath (xOf (j), y);
            else masterPath.lineTo (xOf (j), y);
        }
        for (int j = count - 1; j >= 0; --j)
        {
            const int col = (first + j) % N;
            float loSum = 0.0f;
            for (int ch = 0; ch < WaveLensProcessor::kNumChan; ++ch)
                if ((mask & (1 << ch)) != 0)
                    loSum += proc.colMin[(size_t) WaveLensProcessor::idx (ch, col)].load (relaxed);
            masterPath.lineTo (xOf (j), yOf (loSum));
        }
        masterPath.closeSubPath();

        g.setColour (ui::kMasterWave.withAlpha (0.24f));
        g.fillPath (masterPath);
        g.setColour (ui::kMasterWave);
        g.strokePath (masterPath, juce::PathStrokeType (1.4f));
    }
    else
    {
        g.setColour (ui::kSecondaryText);
        g.setFont (juce::Font (12.0f));
        g.drawText ("OUTPUT MONITOR  -  Trigger audio or MIDI to inspect summed Master output",
                    area.toNearestInt(), juce::Justification::centred);
    }

    g.setColour (ui::kMasterWave);
    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText ("MASTER OUTPUT", area.reduced (10.0f, 4.0f).removeFromTop (16.0f), juce::Justification::topRight);

    g.restoreState();
}

void MasterWaveformComponent::paintLiveSpectrum (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto plot = area.reduced (38.0f, 18.0f);
    plot.removeFromBottom (16.0f);

    for (float db : { -96.0f, -72.0f, -48.0f, -24.0f, 0.0f })
    {
        const float y = plot.getBottom() - (db + 96.0f) / 96.0f * plot.getHeight();
        g.setColour (ui::kBorder.withAlpha (0.5f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (ui::kSecondaryText);
        g.setFont (10.0f);
        g.drawText (juce::String ((int) db), (int) area.getX(), (int) y - 7, 32, 14, juce::Justification::centredRight);
    }

    const int mask = proc.connectedMask.load (relaxed);
    for (int ch = 0; ch < WaveLensProcessor::kMaxSignals; ++ch)
    {
        if ((mask & (1 << ch)) == 0)
            continue;
        juce::Path p;
        for (int b = 0; b < WaveLensProcessor::kSpectrumBins; ++b)
        {
            const float db = proc.spectrumDb[(size_t) ch][(size_t) b].load (relaxed);
            const float n  = juce::jlimit (0.0f, 1.0f, (db + 96.0f) / 96.0f);
            const float x  = plot.getX() + (float) b / (float) (WaveLensProcessor::kSpectrumBins - 1) * plot.getWidth();
            const float y  = plot.getBottom() - n * plot.getHeight();
            if (b == 0) p.startNewSubPath (x, y);
            else p.lineTo (x, y);
        }
        g.setColour (ui::layerColour (ch).withAlpha (0.92f));
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }

    g.setColour (ui::kMasterWave);
    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText ("OUTPUT SPECTRUM", area.reduced (10.0f, 4.0f).removeFromTop (16.0f), juce::Justification::topRight);
}

void MasterWaveformComponent::resized()
{
    masterBtn.setBounds (10, 28, 38, 26);
}
