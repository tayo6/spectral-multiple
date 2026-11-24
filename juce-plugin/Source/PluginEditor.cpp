#include "PluginEditor.h"

namespace
{
    constexpr auto relaxed = std::memory_order_relaxed;

    const juce::Colour kRackGutter    { 0xff08080d };
    const juce::Colour kDeviceBg      { 0xff14141d };
    const juce::Colour kDeviceHeader  { 0xff1b1b29 };
    const juce::Colour kDeviceEdge    { 0xff343247 };
    const juce::Colour kPadIdle       { 0xff20202d };
    const juce::Colour kPadEdge       { 0xff45425c };
        const juce::Colour kTextLight     { 0xffeceaf7 };
    const juce::Colour kAccentPrimary { 0xff9c83f5 };
    const juce::Colour kCanvas        { 0xff07070c };
    const juce::Colour kCanvasText    { 0xffd9d4e8 };
    const juce::Colour kMeterHot      { 0xffff6a9f };
    const juce::Colour kMeterCool     { 0xff7b6cff };

    juce::String formatMs (double ms)
    {
        if (ms >= 1000.0)
            return juce::String (ms / 1000.0, 2) + " s";
        return juce::String (ms, ms < 10.0 ? 2 : 1) + " ms";
    }

    juce::String formatDb (float linear, float floorDb = -90.0f)
    {
        const float db = 20.0f * std::log10 (juce::jmax (linear, 1.0e-6f));
        return db <= floorDb ? juce::String ("-inf") : juce::String (db, 1) + " dB";
    }
}

//==============================================================================
WaveLensLookAndFeel::WaveLensLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, kRackGutter);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff171722));
    setColour (juce::ComboBox::textColourId, kTextLight);
    setColour (juce::ComboBox::outlineColourId, kPadEdge);
    setColour (juce::ComboBox::arrowColourId, juce::Colour (0xffb69cff));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff171722));
    setColour (juce::PopupMenu::textColourId, kTextLight);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccentPrimary);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff100d18));
    setColour (juce::TextButton::buttonColourId, kPadIdle);
    setColour (juce::TextButton::buttonOnColourId, kAccentPrimary);
    setColour (juce::TextButton::textColourOffId, kTextLight);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff100d18));
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
    g.strokePath (bgArc, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path valArc;
    valArc.addCentredArc (cx, cy, radius, radius, 0.0f, rotaryStartAngle, angle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (valArc, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float innerR = juce::jmax (3.0f, radius - 3.5f);
    g.setColour (juce::Colour (0xff1a1a26));
    g.fillEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f);
    g.setColour (juce::Colour (0xff514a6d));
    g.drawEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f, 1.0f);

    const float px = cx + (innerR - 1.0f) * std::sin (angle);
    const float py = cy - (innerR - 1.0f) * std::cos (angle);
    g.setColour (juce::Colour (0xff191919));
    g.drawLine (cx, cy, px, py, 2.0f);
}

//==============================================================================
LayerOverlayView::LayerOverlayView (WaveLensProcessor& p)
    : processor (p)
{
    setOpaque (true);
    startTimerHz (15);
}

void LayerOverlayView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll (kCanvas);
    g.setColour (kDeviceEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    auto header = bounds.reduced (14.0f, 8.0f).removeFromTop (22.0f);
    g.setColour (juce::Colour (0xfff2e8df));
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.drawText ("OVERLAP VIEW  /  SIX-LAYER TRANSIENT", header.removeFromLeft (250.0f),
                juce::Justification::centredLeft);

    auto plot = bounds.reduced (16.0f, 10.0f);
    plot.removeFromTop (28.0f);
    plot.removeFromBottom (26.0f);

    if (plot.getWidth() < 40.0f || plot.getHeight() < 40.0f)
        return;

    const float centre = plot.getCentreY();
    const float halfHeight = plot.getHeight() * 0.44f;

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    g.setColour (juce::Colours::white.withAlpha (0.05f));
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
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawHorizontalLine (juce::roundToInt (centre), plot.getX(), plot.getRight());

    int loadedCount = 0;
    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        const auto preview = processor.getWaveformPreview (layer);
        if (! preview.loaded)
            continue;

        ++loadedCount;
        const bool reversed = processor.apvts.getRawParameterValue (ids::layerReverse (layer))->load() > 0.5f;
        const float trimStart = juce::jlimit (0.0f, 0.98f,
            processor.apvts.getRawParameterValue (ids::layerTrimStart (layer))->load());
        const float trimEnd = juce::jlimit (trimStart + 0.01f, 1.0f,
            processor.apvts.getRawParameterValue (ids::layerTrimEnd (layer))->load());
        const float fadeInMs = processor.apvts.getRawParameterValue (ids::layerFadeIn (layer))->load();
        const float fadeOutMs = processor.apvts.getRawParameterValue (ids::layerFadeOut (layer))->load();
        const float gainScale = juce::jlimit (0.0f, 2.0f,
            processor.apvts.getRawParameterValue (ids::layerGain (layer))->load());
        const float trimmedMs = juce::jmax (1.0f, (float) preview.durationMs * (trimEnd - trimStart));

        auto sampleAtPoint = [&] (int point, float& outMax, float& outMin)
        {
            const float norm = (float) point / (float) (LayeredSampleEngine::kPreviewPoints - 1);
            const float posInWave = reversed
                ? (trimEnd - norm * (trimEnd - trimStart))
                : (trimStart + norm * (trimEnd - trimStart));
            const int idx = juce::jlimit (0, LayeredSampleEngine::kPreviewPoints - 1,
                juce::roundToInt (posInWave * (float) (LayeredSampleEngine::kPreviewPoints - 1)));

            const float elapsedMs = norm * trimmedMs;
            const float remainingMs = (1.0f - norm) * trimmedMs;
            const float fadeInG = fadeInMs > 0.5f ? juce::jlimit (0.0f, 1.0f, elapsedMs / fadeInMs) : 1.0f;
            const float fadeOutG = fadeOutMs > 0.5f ? juce::jlimit (0.0f, 1.0f, remainingMs / fadeOutMs) : 1.0f;
            const float env = gainScale * fadeInG * fadeOutG;

            outMax = juce::jlimit (-1.0f, 1.0f, preview.maximum[(size_t) idx] * env);
            outMin = juce::jlimit (-1.0f, 1.0f, preview.minimum[(size_t) idx] * env);
        };

        juce::Path area, upper, lower;

        for (int point = 0; point < LayeredSampleEngine::kPreviewPoints; ++point)
        {
            float maxV = 0.0f, minV = 0.0f;
            sampleAtPoint (point, maxV, minV);
            const float x = plot.getX()
                + ((float) point / (float) (LayeredSampleEngine::kPreviewPoints - 1)) * plot.getWidth();
            const float top = centre - maxV * halfHeight;
            const float bottom = centre - minV * halfHeight;

            if (point == 0)
            {
                area.startNewSubPath (x, top);
                upper.startNewSubPath (x, top);
                lower.startNewSubPath (x, bottom);
            }
            else
            {
                area.lineTo (x, top);
                upper.lineTo (x, top);
                lower.lineTo (x, bottom);
            }
        }

        for (int point = LayeredSampleEngine::kPreviewPoints - 1; point >= 0; --point)
        {
            float maxV = 0.0f, minV = 0.0f;
            sampleAtPoint (point, maxV, minV);
            const float x = plot.getX()
                + ((float) point / (float) (LayeredSampleEngine::kPreviewPoints - 1)) * plot.getWidth();
            const float bottom = centre - minV * halfHeight;
            area.lineTo (x, bottom);
        }
        area.closeSubPath();

        const auto colour = signalColour (layer);
        g.setColour (colour.withAlpha (0.16f));
        g.fillPath (area);
        g.setColour (colour.withAlpha (0.95f));
        g.strokePath (upper, juce::PathStrokeType (1.75f));
        g.setColour (colour.withAlpha (0.45f));
        g.strokePath (lower, juce::PathStrokeType (1.0f));
    }

    g.restoreState();

    auto legend = bounds.reduced (16.0f, 6.0f).removeFromBottom (18.0f);
    float x = legend.getX();
    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        const auto preview = processor.getWaveformPreview (layer);
        const float itemWidth = 92.0f;
        auto item = juce::Rectangle<float> (x, legend.getY(), itemWidth, legend.getHeight());
        g.setColour (signalColour (layer));
        g.fillRoundedRectangle (item.removeFromLeft (8.0f).withSizeKeepingCentre (7.0f, 7.0f), 2.0f);
        g.setColour (preview.loaded ? juce::Colour (0xfff2e8df) : juce::Colour (0xff908479));
        g.setFont (juce::Font (10.0f, juce::Font::bold));
        const auto label = layer < 3 ? "PRE " + juce::String (layer + 1)
                                     : "MAIN " + juce::String (layer - 2);
        g.drawText (label, item.withTrimmedLeft (4.0f), juce::Justification::centredLeft);
        x += itemWidth;
    }

    if (loadedCount == 0)
    {
        g.setColour (juce::Colour (0xffc4b8ad));
        g.setFont (juce::Font (13.0f));
        g.drawText ("Load PRE and MAIN samples to see all six layers overlap here",
                    plot.toNearestInt(), juce::Justification::centred);
    }
    else
    {
        g.setColour (juce::Colour (0xffc4b8ad));
        g.setFont (juce::Font (10.5f));
        g.drawText (juce::String (loadedCount) + " / 6 waves stacked",
                    header.withX (header.getRight() - 120.0f).withWidth (120.0f),
                    juce::Justification::centredRight);
    }
}

//==============================================================================
ScopeView::ScopeView (WaveLensProcessor& p)
    : proc (p)
{
    auto& a = proc.apvts;
    pView = a.getRawParameterValue (ids::view);
    pCapture  = a.getRawParameterValue (ids::capture);
    pBars     = a.getRawParameterValue (ids::bars);
    pTimeMs   = a.getRawParameterValue (ids::timeMs);
    pGain     = a.getRawParameterValue (ids::gain);
    pLayout   = a.getRawParameterValue (ids::layout);
    for (int n = 0; n < WaveLensProcessor::kMaxSignals; ++n)
        pShowSig[n] = a.getRawParameterValue (ids::showSig[n]);
    pFreeze   = a.getRawParameterValue (ids::freeze);
    pHold     = a.getRawParameterValue (ids::hold);

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

    if (count < 2)
        return;

    const float colW   = lane.getWidth() / (float) N;
    const float centre = lane.getCentreY();
    const float half   = lane.getHeight() * 0.5f * 0.94f;

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
        else        path.lineTo (xOf (j), y);
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

    const int mode = juce::roundToInt (pCapture->load());
    int divisions = 10;
    if (mode == 0)
    {
        const double bars = barsFromIndex (juce::roundToInt (pBars->load()));
        divisions = bars >= 1.0 ? (int) (bars * 4.0) : 4;
    }

    g.setColour (juce::Colours::white.withAlpha (0.05f));
    for (int i = 1; i < divisions; ++i)
    {
        const float x = lane.getX() + lane.getWidth() * (float) i / (float) divisions;
        g.drawVerticalLine (juce::roundToInt (x), lane.getY(), lane.getBottom());
    }

    const float centre = lane.getCentreY();
    const float half = lane.getHeight() * 0.5f * 0.94f;
    for (float level : { -1.0f, -0.5f, 0.5f, 1.0f })
        g.drawHorizontalLine (juce::roundToInt (centre - level * half), lane.getX(), lane.getRight());

    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.drawHorizontalLine (juce::roundToInt (centre), lane.getX(), lane.getRight());

    const float fillAlpha = traces.size() > 1 ? 0.10f : 0.30f;

    if (showHeld)
        for (auto it = traces.rbegin(); it != traces.rend(); ++it)
            drawTrace (g, lane, it->ch, it->colour, s, gain, true, fillAlpha);

    for (auto it = traces.rbegin(); it != traces.rend(); ++it)
        drawTrace (g, lane, it->ch, it->colour, s, gain, false, fillAlpha);

    if (mode != 2 && s.has)
    {
        const float x = lane.getX() + ((float) s.head + 0.5f) * lane.getWidth() / (float) WaveLensProcessor::kCols;
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawVerticalLine (juce::roundToInt (x), lane.getY(), lane.getBottom());
    }

    if (title.isNotEmpty())
    {
        g.setColour (kCanvasText);
        g.setFont (juce::Font (11.0f));
        g.drawText (title, lane.reduced (8.0f, 4.0f).removeFromTop (14.0f), juce::Justification::topLeft);
    }

    g.restoreState();
}

void ScopeView::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> lane, const std::vector<Trace>& traces, bool showHeld)
{
    auto plot = lane.reduced (38.0f, 18.0f); plot.removeFromBottom (20.0f);
    for (float db : { -96.0f, -72.0f, -48.0f, -24.0f, 0.0f }) { const float y = plot.getBottom() - (db + 96.0f) / 96.0f * plot.getHeight(); g.setColour (juce::Colours::white.withAlpha (0.1f)); g.drawHorizontalLine (juce::roundToInt(y), plot.getX(), plot.getRight()); g.setColour (kCanvasText); g.setFont (10.0f); g.drawText (juce::String((int)db), 2, (int)y-7, 32, 14, juce::Justification::centredRight); }
    const double lr = std::log (1000.0); for (double hz : {20.0,50.0,100.0,200.0,500.0,1000.0,2000.0,5000.0,10000.0,20000.0}) { const float x=plot.getX()+(float)(std::log(hz/20.0)/lr)*plot.getWidth(); g.setColour(juce::Colours::white.withAlpha(0.08f)); g.drawVerticalLine(juce::roundToInt(x),plot.getY(),plot.getBottom()); g.setColour(kCanvasText); g.setFont(10.0f); g.drawText(hz>=1000.0?juce::String(hz/1000.0,hz>=10000.0?0:1)+"k":juce::String((int)hz),(int)x-20,(int)plot.getBottom()+2,40,14,juce::Justification::centred); }
    auto draw=[&](const Trace& t,bool held){juce::Path p; for(int b=0;b<WaveLensProcessor::kSpectrumBins;++b){float db=(held?proc.heldSpectrumDb:proc.spectrumDb)[(size_t)t.ch][(size_t)b].load(relaxed); float n=juce::jlimit(0.0f,1.0f,(db+96.0f)/96.0f); float x=plot.getX()+(float)b/(float)(WaveLensProcessor::kSpectrumBins-1)*plot.getWidth(); float y=plot.getBottom()-n*plot.getHeight(); if(b==0)p.startNewSubPath(x,y);else p.lineTo(x,y);} g.setColour(t.colour.withAlpha(held?0.35f:0.98f));g.strokePath(p,juce::PathStrokeType(held?1.0f:1.5f));};
    if(showHeld)for(auto it=traces.rbegin();it!=traces.rend();++it)draw(*it,true); for(auto it=traces.rbegin();it!=traces.rend();++it)draw(*it,false);
}

void ScopeView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll (juce::Colour (0xff151a1f));
    g.setColour (kDeviceEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    Snapshot s;
    s.head      = proc.head.load();
    s.passStart = proc.passStartCol.load();
    s.filled    = proc.filled.load();
    s.has       = proc.hasData.load();
    s.heldFrom  = proc.heldFrom.load();
    s.heldTo    = proc.heldTo.load();
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
            else drawLane (g, lane, { { n, signalColour (n) } }, s, gain, showHeld, (n < 3 ? "PRE " : "MAIN ") + juce::String (n < 3 ? n + 1 : n - 2));
        }
    }
    else
    {
        std::vector<Trace> traces;
        for (int n : active)
            traces.push_back ({ n, signalColour (n) });
        if (spectrum) drawSpectrum (g, area, traces, showHeld);
        else drawLane (g, area, traces, s, gain, showHeld, {});
    }

    auto footer = bounds.reduced (10.0f, 3.0f).removeFromBottom (14.0f);
    g.setFont (juce::Font (11.0f));
    g.setColour (juce::Colour (0xffaeb8c0));
    g.drawText (windowLabel(), footer, juce::Justification::centredLeft);

    if (mask == 0)
        g.drawText ("Load a sample layer to start monitoring its signal",
                    footer, juce::Justification::centred);
    else
        g.drawText (juce::String (active.size()) + (active.size() == 1 ? " layer" : " layers"),
                    footer, juce::Justification::centred);

    if (pFreeze->load() > 0.5f)
    {
        g.setColour (kAccentPrimary);
        g.drawText ("FROZEN", footer, juce::Justification::centredRight);
    }
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
    g.fillAll (juce::Colour (0xff171722));
    g.setColour (kDeviceEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    const int mode = juce::roundToInt (pMeter->load());
    static const char* names[] = { "PEAK", "RMS", "CREST" };

    float now = 0.0f, hold = 0.0f, lo = -60.0f, hi = 0.0f;
    auto toDb = [] (float v) { return 20.0f * std::log10 (juce::jmax (v, 1.0e-6f)); };

    if (mode == 0)      { now = proc.peakNow.load (relaxed);  hold = proc.peakHold.load (relaxed);  }
    else if (mode == 1) { now = proc.rmsNow.load (relaxed);   hold = proc.rmsHold.load (relaxed);   }
    else                { now = proc.crestNow.load (relaxed); hold = proc.crestHold.load (relaxed); lo = 0.0f; hi = 24.0f; }

    const float nowDb = toDb (now);
    const float holdDb = toDb (hold);

    auto area = bounds.reduced (6.0f);
    g.setColour (kTextLight);
    g.setFont (juce::Font (11.0f, juce::Font::bold));
    g.drawText (names[juce::jlimit (0, 2, mode)], area.removeFromTop (16.0f), juce::Justification::centred);

    auto textArea = area.removeFromBottom (36.0f);
    auto bar = area.reduced (area.getWidth() * 0.28f, 4.0f);

    g.setColour (juce::Colour (0xff201b30));
    g.fillRoundedRectangle (bar, 2.0f);

    auto norm = [&] (float db) { return juce::jlimit (0.0f, 1.0f, (db - lo) / (hi - lo)); };
    const float h = bar.getHeight() * norm (nowDb);

    juce::ColourGradient grad (kMeterHot, 0.0f, bar.getY(), kMeterCool, 0.0f, bar.getBottom(), false);
    grad.addColour (0.25, juce::Colour (0xffffd24a));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (bar.withTrimmedTop (bar.getHeight() - h), 2.0f);

    if (hold > 0.0f)
    {
        const float y = bar.getBottom() - bar.getHeight() * norm (holdDb);
        g.setColour (juce::Colours::white);
        g.fillRect (bar.getX() - 3.0f, y - 1.0f, bar.getWidth() + 6.0f, 2.0f);
    }

    g.setColour (kTextLight);
    g.setFont (juce::Font (11.0f, juce::Font::bold));
    g.drawText (mode == 2 ? juce::String (nowDb, 1) : formatDb (now), textArea.removeFromTop (18.0f), juce::Justification::centred);

    g.setColour (juce::Colour (0xffaaa2c5));
    g.setFont (juce::Font (10.0f));
    g.drawText (mode == 2 ? juce::String (holdDb, 1) : formatDb (hold), textArea, juce::Justification::centred);
}

//==============================================================================
WaveLensEditor::WaveLensEditor (WaveLensProcessor& p)
    : AudioProcessorEditor (&p), proc (p), scope (p), overlay (p), meter (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (overlay);
    addAndMakeVisible (scope);
    addAndMakeVisible (meter);

    setupCombo (viewBox, ids::view, viewAtt);
    setupCombo (layoutBox, ids::layout, layoutAtt);
    setupToggle (freezeBtn, "FREEZE", ids::freeze, freezeAtt);
    setupToggle (holdBtn, "HOLD", ids::hold, holdAtt);

    resetBtn.setButtonText ("RESET");
    resetBtn.onClick = [this] { proc.requestReset(); };
    addAndMakeVisible (resetBtn);

    auto styleHeaderButton = [] (juce::TextButton& button, const juce::String& text,
                                 juce::Colour accent, const juce::String& tooltip)
    {
        button.setButtonText (text);
        button.setTooltip (tooltip);
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff20202d));
        button.setColour (juce::TextButton::buttonOnColourId, accent);
        button.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffd9d5e8));
        button.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff100d18));
    };

    styleHeaderButton (masterBtn, "WAVES", juce::Colour (0xffa78bfa), "Show all six sample waveforms");
    masterBtn.onClick = [this]
    {
        monitorMode = false;
        updateViewVisibility();
    };
    addAndMakeVisible (masterBtn);

    styleHeaderButton (monitorBtn, "MONITOR", juce::Colour (0xffcf83ff), "Switch the upper display to live waveform and FFT");
    monitorBtn.onClick = [this]
    {
        monitorMode = ! monitorMode;
        updateViewVisibility();
    };
    addAndMakeVisible (monitorBtn);

    styleHeaderButton (triggerBtn, "TRIGGER", juce::Colour (0xffb79aff), "Manually trigger all loaded PRE and MAIN layers");
    triggerBtn.onClick = [this] { proc.requestTrigger(); };
    addAndMakeVisible (triggerBtn);

    preLeadSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    preLeadSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
    preLeadSlider.setRange (0.0, 100.0, 0.1);
    preLeadSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff9a83f0));
    preLeadSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff292638));
    preLeadSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffe7dbff));
    preLeadSlider.setColour (juce::Slider::textBoxTextColourId, kTextLight);
    preLeadSlider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff171722));
    preLeadSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff48415f));
    preLeadSlider.setTextValueSuffix (" ms");
    preLeadSlider.setTooltip ("Global amount by which PRE layers lead the delayed dry transient");
    addAndMakeVisible (preLeadSlider);
    preLeadAtt = std::make_unique<SliderAtt> (proc.apvts, ids::preRollMs, preLeadSlider);

    for (int n = 0; n < WaveLensProcessor::kMaxSignals; ++n)
    {
        const auto accent = signalColour (n);
        const juce::String slot = (n < 3 ? "PRE " : "MAIN ")
                                + juce::String (n < 3 ? n + 1 : n - 2);
        layerSelectBtn[n].setButtonText (slot);
        layerSelectBtn[n].setClickingTogglesState (true);
        layerSelectBtn[n].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff20202d));
        layerSelectBtn[n].setColour (juce::TextButton::buttonOnColourId, accent);
        layerSelectBtn[n].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcbc6db));
        layerSelectBtn[n].setColour (juce::TextButton::textColourOnId, juce::Colour (0xff100d18));
        layerSelectBtn[n].setTooltip ("Select " + slot + " sample layer");
        addAndMakeVisible (layerSelectBtn[n]);
        layerSelectBtn[n].onClick = [this, n] { selectLayer (n); };

        setupToggle (mutePadBtn[n], "M", ids::layerMute (n), muteAtt[n]);
        mutePadBtn[n].setTooltip ("Mute this layer");
        mutePadBtn[n].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff20202d));
        mutePadBtn[n].setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb65d90));
        mutePadBtn[n].setColour (juce::TextButton::textColourOffId, kTextLight);
        mutePadBtn[n].setColour (juce::TextButton::textColourOnId, juce::Colours::white);

        auditionBtn[n].setButtonText (juce::String::fromUTF8 ("▶"));
        auditionBtn[n].setTooltip ("Audition this sample without the trigger timing offset");
        auditionBtn[n].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff20202d));
        auditionBtn[n].setColour (juce::TextButton::buttonOnColourId, accent);
        auditionBtn[n].setColour (juce::TextButton::textColourOffId, kTextLight);
        addAndMakeVisible (auditionBtn[n]);
        auditionBtn[n].onClick = [this, n] { proc.requestPreview (n); };

        setupToggle (soloPadBtn[n], "S", ids::layerSolo (n), soloAtt[n]);
        soloPadBtn[n].setTooltip ("Solo this layer");
        soloPadBtn[n].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff20202d));
        soloPadBtn[n].setColour (juce::TextButton::buttonOnColourId, accent);
        soloPadBtn[n].setColour (juce::TextButton::textColourOffId, kTextLight);
        soloPadBtn[n].setColour (juce::TextButton::textColourOnId, juce::Colour (0xff100d18));

        sigBtn[n].setButtonText (n < 3 ? ("P" + juce::String (n + 1))
                                       : ("M" + juce::String (n - 2)));
        sigBtn[n].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff20202d));
        sigBtn[n].setColour (juce::TextButton::buttonOnColourId, accent);
        sigBtn[n].setColour (juce::TextButton::textColourOffId, kTextLight);
        sigBtn[n].setColour (juce::TextButton::textColourOnId, juce::Colour (0xff100d18));
        sigBtn[n].setClickingTogglesState (true);
        addAndMakeVisible (sigBtn[n]);
        sigAtt[n] = std::make_unique<ButtonAtt> (proc.apvts, ids::showSig[n], sigBtn[n]);

        envelopeComponents[(size_t) n] = std::make_unique<LayerEnvelopeComponent> (proc, n, accent);
        addAndMakeVisible (*envelopeComponents[(size_t) n]);
    }

    for (int group = 0; group < 2; ++group)
    {
        loadSampleBtn[(size_t) group].setButtonText ("LOAD");
        loadSampleBtn[(size_t) group].setTooltip ("Load a sample into the selected panel layer");
        loadSampleBtn[(size_t) group].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff282638));
        loadSampleBtn[(size_t) group].setColour (juce::TextButton::textColourOffId, kTextLight);
        addAndMakeVisible (loadSampleBtn[(size_t) group]);
        loadSampleBtn[(size_t) group].onClick = [this, group] { chooseLoadSample (group); };

        clearSampleBtn[(size_t) group].setButtonText ("×");
        clearSampleBtn[(size_t) group].setTooltip ("Clear the selected panel layer sample");
        clearSampleBtn[(size_t) group].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff28202e));
        clearSampleBtn[(size_t) group].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffffa6cc));
        addAndMakeVisible (clearSampleBtn[(size_t) group]);
        clearSampleBtn[(size_t) group].onClick = [this, group]
        {
            const int layer = selectedLayerForGroup (group);
            proc.clearSample (layer);
            updateEnabledStates();
        };
    }

    setResizable (true, true);
    setResizeLimits (1024, 700, 1920, 1280);
    setSize (1280, 900);

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

void WaveLensEditor::setupToggle (juce::TextButton& button, const juce::String& text,
                                  const char* paramId, std::unique_ptr<ButtonAtt>& attachment)
{
    button.setButtonText (text);
    button.setClickingTogglesState (true);
    addAndMakeVisible (button);
    attachment = std::make_unique<ButtonAtt> (proc.apvts, paramId, button);
}

int WaveLensEditor::selectedLayerForGroup (int group) const noexcept
{
    return group == 0 ? selectedPreLayer : selectedMainLayer;
}

void WaveLensEditor::selectLayer (int layer)
{
    if (! juce::isPositiveAndBelow (layer, WaveLensProcessor::kMaxSignals))
        return;

    if (layer < 3)
        selectedPreLayer = layer;
    else
        selectedMainLayer = layer;

    lastSelectedLayer = layer;
    monitorMode = false;
    updateViewVisibility();
}

void WaveLensEditor::chooseLoadSample (int group)
{
    const int layer = selectedLayerForGroup (group);
    auto chooser = std::make_shared<juce::FileChooser> (
        "Load sample for " + juce::String (layer < 3 ? "PRE " : "MAIN ")
            + juce::String (layer < 3 ? layer + 1 : layer - 2),
        juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, layer, chooser] (const juce::FileChooser& fileChooser)
        {
            const auto file = fileChooser.getResult();
            if (file.existsAsFile())
                loadFileIntoLayer (layer, file);
        });
}

bool WaveLensEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& file : files)
        if (file.endsWithIgnoreCase (".wav") || file.endsWithIgnoreCase (".aif")
            || file.endsWithIgnoreCase (".aiff") || file.endsWithIgnoreCase (".flac")
            || file.endsWithIgnoreCase (".ogg") || file.endsWithIgnoreCase (".mp3"))
            return true;
    return false;
}

void WaveLensEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (files.isEmpty())
        return;

    int targetLayer = lastSelectedLayer;
    bool hitSlot = false;
    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
        if (padCellBounds[layer].contains (x, y))
        {
            targetLayer = layer;
            hitSlot = true;
            break;
        }

    // Dropping elsewhere within either device panel targets its active layer.
    if (! hitSlot && prePanelArea.contains (x, y))
        targetLayer = selectedPreLayer;
    else if (! hitSlot && mainPanelArea.contains (x, y))
        targetLayer = selectedMainLayer;

    selectLayer (targetLayer);
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
    proc.collectSampleGarbage();
    const int mask = proc.connectedMask.load();

    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        const bool loaded = (mask & (1 << layer)) != 0;
        const auto sampleName = proc.getSampleName (layer);
        if (sampleName != displayedSampleNames[(size_t) layer])
        {
            displayedSampleNames[(size_t) layer] = sampleName;
            const juce::String slot = (layer < 3 ? "PRE " : "MAIN ")
                                    + juce::String (layer < 3 ? layer + 1 : layer - 2);
            layerSelectBtn[layer].setButtonText (sampleName.isEmpty() ? slot : sampleName.substring (0, 10));
            layerSelectBtn[layer].setTooltip (sampleName.isEmpty() ? ("Empty " + slot) : sampleName);
        }
        auditionBtn[layer].setEnabled (loaded);
        sigBtn[layer].setEnabled (loaded);
    }

    for (int group = 0; group < 2; ++group)
    {
        const int layer = selectedLayerForGroup (group);
        const bool loaded = (mask & (1 << layer)) != 0;
        clearSampleBtn[(size_t) group].setEnabled (loaded);
        auditionBtn[layer].setEnabled (loaded);
    }
}

void WaveLensEditor::updateViewVisibility()
{
    overlay.setVisible (! monitorMode);
    scope.setVisible (monitorMode);
    meter.setVisible (monitorMode);

    masterBtn.setColour (juce::TextButton::buttonColourId,
                         monitorMode ? juce::Colour (0xff20202d) : juce::Colour (0xff65569b));
    monitorBtn.setColour (juce::TextButton::buttonColourId,
                          monitorMode ? juce::Colour (0xff8061ad) : juce::Colour (0xff20202d));

    viewBox.setVisible (monitorMode);
    layoutBox.setVisible (monitorMode);
    freezeBtn.setVisible (monitorMode);
    holdBtn.setVisible (monitorMode);
    resetBtn.setVisible (monitorMode);

    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        const bool selected = (layer == selectedPreLayer || layer == selectedMainLayer);
        layerSelectBtn[layer].setToggleState (selected, juce::dontSendNotification);
        mutePadBtn[layer].setVisible (selected);
        auditionBtn[layer].setVisible (selected);
        soloPadBtn[layer].setVisible (selected);
        sigBtn[layer].setVisible (monitorMode);
        envelopeComponents[(size_t) layer]->setVisible (selected);
    }

    updateEnabledStates();
    resized();
    repaint();
}

void WaveLensEditor::timerCallback()
{
    updateEnabledStates();
}

void WaveLensEditor::paint (juce::Graphics& g)
{
    g.fillAll (kRackGutter);

    // Header, master waveform viewport, trigger strip and the two sample devices.
    g.setColour (kDeviceBg);
    g.fillRoundedRectangle (topBarArea.toFloat(), 6.0f);
    g.setColour (kDeviceEdge);
    g.drawRoundedRectangle (topBarArea.toFloat().reduced (0.5f), 6.0f, 1.0f);
    g.setColour (juce::Colour (0xffb69cff));
    g.setFont (juce::Font (19.0f, juce::Font::bold));
    g.drawText ("SPECTRAL MULTIPLE", topBarArea.withTrimmedLeft (140).withTrimmedRight (monitorMode ? 520 : 90),
                juce::Justification::centred);
    g.setColour (juce::Colour (0xff817b99));
    g.setFont (juce::Font (9.5f, juce::Font::bold));
    g.drawText ("SIX-LAYER TRANSIENT DESIGNER", topBarArea.withTrimmedLeft (140).withTrimmedRight (monitorMode ? 520 : 90).translated (0, 12),
                juce::Justification::centred);

    g.setColour (kDeviceBg);
    g.fillRoundedRectangle (waveformArea.toFloat(), 6.0f);
    g.setColour (kDeviceEdge);
    g.drawRoundedRectangle (waveformArea.toFloat().reduced (0.5f), 6.0f, 1.0f);

    g.setColour (kDeviceHeader);
    g.fillRoundedRectangle (triggerBarArea.toFloat(), 5.0f);
    g.setColour (kDeviceEdge);
    g.drawRoundedRectangle (triggerBarArea.toFloat().reduced (0.5f), 5.0f, 1.0f);
    g.setColour (juce::Colour (0xffaaa2c5));
    g.setFont (juce::Font (9.5f, juce::Font::bold));
    g.drawText ("PRE LEAD", triggerBarArea.withX (triggerBarArea.getX() + 12).withWidth (60),
                juce::Justification::centredLeft);

    auto drawDevice = [&] (juce::Rectangle<int> panel, juce::Rectangle<int> header,
                           const juce::String& title, juce::Colour accent)
    {
        g.setColour (kDeviceBg);
        g.fillRoundedRectangle (panel.toFloat(), 6.0f);
        g.setColour (kDeviceEdge);
        g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 6.0f, 1.0f);
        g.setColour (juce::Colour (0xff1d1c2a));
        g.fillRoundedRectangle (header.toFloat(), 5.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) header.getX() + 7.0f,
                                                        (float) header.getY() + 7.0f,
                                                        3.0f, (float) juce::jmax (8, header.getHeight() - 14)), 1.5f);
        g.setColour (accent);
        g.setFont (juce::Font (12.5f, juce::Font::bold));
        g.drawText (title, header.withX (header.getX() + 16).withY (header.getY() + 2)
                                .withWidth (94).withHeight (17), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff7e7894));
        g.setFont (juce::Font (8.5f));
        g.drawText (title == "SNAPBACK" ? "PRE SAMPLE LAYERS" : "MAIN TRANSIENT LAYERS",
                    header.withX (header.getX() + 16).withY (header.getY() + 20).withWidth (100).withHeight (12),
                    juce::Justification::centredLeft);
    };

    drawDevice (prePanelArea, preHeaderArea, "SNAPBACK", juce::Colour (0xffa78bfa));
    drawDevice (mainPanelArea, mainHeaderArea, "TRANSIENT", juce::Colour (0xffd08aff));

    g.setColour (juce::Colour (0xff2e2a40));
    g.drawVerticalLine (prePanelArea.getRight() + 3, (float) prePanelArea.getY() + 6.0f,
                        (float) prePanelArea.getBottom() - 6.0f);
}

void WaveLensEditor::resized()
{
    auto area = getLocalBounds().reduced (10, 8);

    topBarArea = area.removeFromTop (38);
    auto header = topBarArea.reduced (7, 5);
    masterBtn.setBounds (header.removeFromLeft (58));
    header.removeFromLeft (5);
    monitorBtn.setBounds (header.removeFromLeft (72));

    auto rightControls = header.removeFromRight (monitorMode ? 520 : 0);
    if (monitorMode)
    {
        resetBtn.setBounds (rightControls.removeFromRight (48));
        rightControls.removeFromRight (4);
        holdBtn.setBounds (rightControls.removeFromRight (48));
        rightControls.removeFromRight (4);
        freezeBtn.setBounds (rightControls.removeFromRight (58));
        rightControls.removeFromRight (7);
        layoutBox.setBounds (rightControls.removeFromRight (78));
        rightControls.removeFromRight (4);
        viewBox.setBounds (rightControls.removeFromRight (86));
        rightControls.removeFromRight (6);
        for (int n = WaveLensProcessor::kMaxSignals - 1; n >= 0; --n)
        {
            sigBtn[n].setBounds (rightControls.removeFromRight (25));
            rightControls.removeFromRight (3);
        }
    }
    else
    {
        resetBtn.setBounds (0, 0, 0, 0);
        holdBtn.setBounds (0, 0, 0, 0);
        freezeBtn.setBounds (0, 0, 0, 0);
        viewBox.setBounds (0, 0, 0, 0);
        layoutBox.setBounds (0, 0, 0, 0);
        for (auto& button : sigBtn)
            button.setBounds (0, 0, 0, 0);
    }

    area.removeFromTop (7);
    const int overviewHeight = juce::jlimit (205, 440, juce::roundToInt ((float) area.getHeight() * 0.48f));
    waveformArea = area.removeFromTop (overviewHeight);
    auto display = waveformArea.reduced (4);
    if (monitorMode)
    {
        meter.setBounds (display.removeFromRight (58));
        display.removeFromRight (5);
        scope.setBounds (display);
    }
    else
    {
        overlay.setBounds (display);
        scope.setBounds (0, 0, 0, 0);
        meter.setBounds (0, 0, 0, 0);
    }

    area.removeFromTop (6);
    triggerBarArea = area.removeFromTop (34);
    auto triggerLayout = triggerBarArea.reduced (7, 3);
    triggerLayout.removeFromLeft (68);
    preLeadSlider.setBounds (triggerLayout.removeFromLeft (190));
    triggerBtn.setBounds (juce::Rectangle<int> (triggerBarArea.getCentreX() - 62,
                                                triggerBarArea.getY() + 3, 124,