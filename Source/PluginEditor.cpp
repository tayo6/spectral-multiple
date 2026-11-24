#include "PluginEditor.h"

namespace
{
    constexpr auto relaxed = std::memory_order_relaxed;

    const juce::Colour kRackGutter   { 0xff666666 };
    const juce::Colour kDeviceBg     { 0xff8e8e8e };
    const juce::Colour kDeviceHeader { 0xff868686 };
    const juce::Colour kDeviceEdge   { 0xff343434 };
    const juce::Colour kPadIdle      { 0xff8a8a8a };
    const juce::Colour kPadEdge      { 0xff424242 };
    const juce::Colour kTextDark     { 0xff151515 };
    const juce::Colour kTextLight    { 0xfff0f0f0 };
    const juce::Colour kAbletonYellow{ 0xffffd000 };
    const juce::Colour kAbletonOrange{ 0xffff9800 };
    const juce::Colour kAbletonCyan  { 0xff2cf0ff };
    const juce::Colour kCanvas       { 0xff251a12 };
    const juce::Colour kCanvasText   { 0xffe0e3e5 };
    const juce::Colour kMeterHot     { 0xffff4e42 };
    const juce::Colour kMeterCool    { 0xff58e058 };

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

    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff9c9c9c));
    setColour (juce::ComboBox::textColourId, kTextDark);
    setColour (juce::ComboBox::outlineColourId, kPadEdge);
    setColour (juce::ComboBox::arrowColourId, kTextDark);

    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff3a3a3a));
    setColour (juce::PopupMenu::textColourId, kTextLight);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAbletonOrange);
    setColour (juce::PopupMenu::highlightedTextColourId, kTextDark);

    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff7d7d7d));
    setColour (juce::TextButton::buttonOnColourId, kAbletonYellow);
    setColour (juce::TextButton::textColourOffId, kTextDark);
    setColour (juce::TextButton::textColourOnId, kTextDark);

    setColour (juce::Label::textColourId, kTextDark);
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
    g.setColour (juce::Colour (0xff7d7d7d));
    g.fillEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f);
    g.setColour (juce::Colour (0xff2f2f2f));
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
    g.drawText ("MASTER  /  ALL 6 WAVES STACKED", header.removeFromLeft (250.0f),
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
        g.drawText ("Drop samples onto the 6 Drum Rack pads to view all waves stacked here",
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
        g.drawText ("Load samples into the Drum Rack pads to view waveforms and spectra",
                    footer, juce::Justification::centred);
    else
        g.drawText (juce::String (active.size()) + (active.size() == 1 ? " layer" : " layers"),
                    footer, juce::Justification::centred);

    if (pFreeze->load() > 0.5f)
    {
        g.setColour (kAbletonOrange);
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
    g.fillAll (juce::Colour (0xff7b7b7b));
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
    g.setColour (kTextDark);
    g.setFont (juce::Font (11.0f, juce::Font::bold));
    g.drawText (names[juce::jlimit (0, 2, mode)], area.removeFromTop (16.0f), juce::Justification::centred);

    auto textArea = area.removeFromBottom (36.0f);
    auto bar = area.reduced (area.getWidth() * 0.28f, 4.0f);

    g.setColour (juce::Colour (0xff252525));
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

    g.setColour (kTextDark);
    g.setFont (juce::Font (11.0f, juce::Font::bold));
    g.drawText (mode == 2 ? juce::String (nowDb, 1) : formatDb (now), textArea.removeFromTop (18.0f), juce::Justification::centred);

    g.setColour (juce::Colour (0xff262626));
    g.setFont (juce::Font (10.0f));
    g.drawText (mode == 2 ? juce::String (holdDb, 1) : formatDb (hold), textArea, juce::Justification::centred);
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

    masterBtn.setButtonText ("Master");
    masterBtn.setTooltip ("Master view: stack all six loaded source waveforms");
    masterBtn.onClick = [this]
    {
        selectedLayer = -1;
        monitorMode = false;
        updateViewVisibility();
    };
    addAndMakeVisible (masterBtn);

    oneShotBtn.setButtonText ("1-Shot");
    oneShotBtn.setTooltip ("Edit selected layer sample, reverse, pogo, trim, fades, pitch & AHDSR");
    oneShotBtn.onClick = [this]
    {
        if (selectedLayer < 0) selectedLayer = 0;
        monitorMode = false;
        updateViewVisibility();
    };
    addAndMakeVisible (oneShotBtn);

    monitorBtn.setButtonText ("Monitor");
    monitorBtn.setTooltip ("Open live waveform & FFT spectrum monitor");
    monitorBtn.onClick = [this]
    {
        monitorMode = true;
        updateViewVisibility();
    };
    addAndMakeVisible (monitorBtn);

    padMasterBtn.setButtonText ("7 . MASTER  (ALL WAVES)");
    padMasterBtn.setTooltip ("Pad 7 - Master: view all six loaded waves stacked on each other");
    padMasterBtn.onClick = [this]
    {
        selectedLayer = -1;
        monitorMode = false;
        updateViewVisibility();
    };
    addAndMakeVisible (padMasterBtn);

    loadSampleBtn.setButtonText ("Load Sample...");
    loadSampleBtn.onClick = [this]
    {
        const int layer = juce::jlimit (0, WaveLensProcessor::kMaxSignals - 1, selectedLayer);
        auto chooser = std::make_shared<juce::FileChooser> (
            "Load sample for layer " + juce::String (layer + 1),
            juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, layer, chooser] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file.existsAsFile())
                    loadFileIntoLayer (layer, file);
            });
    };
    addAndMakeVisible (loadSampleBtn);

    clearSampleBtn.setButtonText ("x");
    clearSampleBtn.setTooltip ("Clear sample from selected pad");
    clearSampleBtn.onClick = [this]
    {
        if (selectedLayer >= 0)
        {
            proc.clearSample (selectedLayer);
            updateEnabledStates();
        }
    };
    addAndMakeVisible (clearSampleBtn);

    for (int n = 0; n < WaveLensProcessor::kMaxSignals; ++n)
    {
        setupToggle (sigBtn[n], n < 3 ? ("P" + juce::String (n + 1)) : ("M" + juce::String (n - 2)), ids::showSig[n], sigAtt[n]);
        sigBtn[n].setColour (juce::TextButton::buttonOnColourId, signalColour (n));
        sigBtn[n].setColour (juce::TextButton::textColourOnId, kTextDark);

        const juce::String slot = (n < 3 ? "PRE " : "MAIN ") + juce::String (n < 3 ? n + 1 : n - 2);
        layerSelectBtn[n].setButtonText (slot);
        layerSelectBtn[n].setColour (juce::TextButton::buttonColourId, kPadIdle);
        layerSelectBtn[n].setColour (juce::TextButton::buttonOnColourId, n < 3 ? kAbletonOrange : kAbletonCyan);
        layerSelectBtn[n].setColour (juce::TextButton::textColourOffId, kTextDark);
        layerSelectBtn[n].setColour (juce::TextButton::textColourOnId, kTextDark);
        addAndMakeVisible (layerSelectBtn[n]);
        layerSelectBtn[n].onClick = [this, n]
        {
            selectedLayer = n;
            monitorMode = false;
            updateViewVisibility();
        };

        setupToggle (mutePadBtn[n], "M", ids::layerMute (n), muteAtt[n]);
        mutePadBtn[n].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff7b7b7b));
        mutePadBtn[n].setColour (juce::TextButton::buttonOnColourId, kAbletonOrange);

        auditionBtn[n].setButtonText (">");
        auditionBtn[n].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff7b7b7b));
        auditionBtn[n].onClick = [this, n] { proc.requestPreview (n); };
        addAndMakeVisible (auditionBtn[n]);

        setupToggle (soloPadBtn[n], "S", ids::layerSolo (n), soloAtt[n]);
        soloPadBtn[n].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff7b7b7b));
        soloPadBtn[n].setColour (juce::TextButton::buttonOnColourId, kAbletonCyan);

        envelopeComponents[(size_t) n] = std::make_unique<LayerEnvelopeComponent> (proc, n, signalColour (n));
        addAndMakeVisible (*envelopeComponents[(size_t) n]);
    }

    setResizable (true, true);
    setResizeLimits (980, 580, 1800, 1100);
    setSize (1200, 660);

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

void WaveLensEditor::setupToggle (juce::TextButton& b, const juce::String& text, const char* paramId,
                                  std::unique_ptr<ButtonAtt>& att)
{
    b.setButtonText (text);
    b.setClickingTogglesState (true);
    addAndMakeVisible (b);
    att = std::make_unique<ButtonAtt> (proc.apvts, paramId, b);
}

bool WaveLensEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff")
            || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".ogg"))
            return true;
    return false;
}

void WaveLensEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (files.isEmpty())
        return;

    int targetLayer = selectedLayer >= 0 ? selectedLayer : 0;
    for (int i = 0; i < WaveLensProcessor::kMaxSignals; ++i)
    {
        if (padCellBounds[i].contains (x, y))
        {
            targetLayer = i;
            break;
        }
    }

    loadFileIntoLayer (targetLayer, juce::File (files[0]));
    selectedLayer = targetLayer;
    monitorMode = false;
    updateViewVisibility();
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

        const auto sampleName = proc.getSampleName (n);
        if (sampleName != displayedSampleNames[(size_t) n])
        {
            displayedSampleNames[(size_t) n] = sampleName;
            const juce::String slot = (n < 3 ? "PRE " : "MAIN ") + juce::String (n < 3 ? n + 1 : n - 2);
            layerSelectBtn[n].setButtonText (sampleName.isEmpty() ? slot : sampleName.substring (0, 14));
        }
    }
    layoutBox.setEnabled (loadedCount > 1);
    padMasterBtn.setButtonText ("7 . MASTER (" + juce::String (loadedCount) + "/6 WAVES)");
    clearSampleBtn.setEnabled (selectedLayer >= 0 && (mask & (1 << selectedLayer)) != 0);
}

void WaveLensEditor::updateViewVisibility()
{
    const bool showMaster = selectedLayer < 0 && ! monitorMode;
    const bool showLayer = selectedLayer >= 0 && ! monitorMode;

    masterBtn.setColour (juce::TextButton::buttonColourId, showMaster ? kAbletonOrange : juce::Colour (0xff4a4a4a));
    masterBtn.setColour (juce::TextButton::textColourOffId, showMaster ? kTextDark : kTextLight);
    oneShotBtn.setColour (juce::TextButton::buttonColourId, showLayer ? kAbletonOrange : juce::Colour (0xff4a4a4a));
    oneShotBtn.setColour (juce::TextButton::textColourOffId, showLayer ? kTextDark : kTextLight);
    monitorBtn.setColour (juce::TextButton::buttonColourId, monitorMode ? kAbletonCyan : juce::Colour (0xff4a4a4a));
    monitorBtn.setColour (juce::TextButton::textColourOffId, monitorMode ? kTextDark : kTextLight);
    padMasterBtn.setColour (juce::TextButton::buttonColourId, showMaster ? kAbletonYellow : kPadIdle);

    overlay.setVisible (showMaster);
    scope.setVisible (monitorMode);
    meter.setVisible (monitorMode);
    viewBox.setVisible (monitorMode);
    layoutBox.setVisible (monitorMode);
    loadSampleBtn.setVisible (showLayer);
    clearSampleBtn.setVisible (showLayer);

    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        sigBtn[layer].setVisible (monitorMode);
        const bool selected = showLayer && selectedLayer == layer;
        layerSelectBtn[layer].setToggleState (selected, juce::dontSendNotification);
        if (envelopeComponents[(size_t) layer] != nullptr)
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

    // Top bar title
    g.setColour (kTextLight);
    g.setFont (juce::Font (14.0f, juce::Font::bold));
    g.drawText ("WaveLens", 12, 5, 100, 22, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffdcdcdc));
    g.setFont (juce::Font (10.5f));
    g.drawText ("SIX-LAYER TRANSIENT SAMPLER", 96, 6, 240, 20, juce::Justification::centredLeft);

    auto drawDeviceFrame = [&] (juce::Rectangle<int> area, juce::Rectangle<int> hdr, const juce::String& title)
    {
        g.setColour (kDeviceBg);
        g.fillRoundedRectangle (area.toFloat(), 5.0f);
        g.setColour (kDeviceEdge);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 5.0f, 1.0f);

        g.setColour (kDeviceHeader);
        g.fillRect (hdr);
        g.setColour (juce::Colour (0xff4a4a4a));
        g.drawHorizontalLine (hdr.getBottom(), (float) hdr.getX(), (float) hdr.getRight());

        // Ableton yellow (I) power circle
        auto circle = juce::Rectangle<float> ((float) hdr.getX() + 7.0f, (float) hdr.getCentreY() - 6.5f, 13.0f, 13.0f);
        g.setColour (kAbletonYellow);
        g.fillEllipse (circle);
        g.setColour (kDeviceEdge);
        g.drawEllipse (circle, 1.0f);
        g.setColour (kTextDark);
        g.setFont (juce::Font (8.5f, juce::Font::bold));
        g.drawText ("I", circle, juce::Justification::centred);

        g.setFont (juce::Font (12.0f, juce::Font::bold | juce::Font::italic));
        g.drawText (title, hdr.withTrimmedLeft (25), juce::Justification::centredLeft);
    };

    drawDeviceFrame (drumRackDeviceArea, drumRackHeaderArea, "Drum Rack");

    juce::String rightTitle = "Master - All Source Layers";
    if (monitorMode)
        rightTitle = "Monitor / FFT Spectrum";
    else if (selectedLayer >= 0)
    {
        const auto name = proc.getSampleName (selectedLayer);
        const juce::String slot = (selectedLayer < 3 ? "PRE " : "MAIN ")
                                  + juce::String (selectedLayer < 3 ? selectedLayer + 1 : selectedLayer - 2);
        rightTitle = name.isEmpty() ? slot + " (Empty)" : slot + " - " + name;
    }
    drawDeviceFrame (simplerDeviceArea, simplerHeaderArea, rightTitle);

    // Pad grid background
    g.setColour (juce::Colour (0xff7d7d7d));
    g.fillRoundedRectangle (padGridArea.toFloat(), 3.0f);
    g.setColour (juce::Colour (0xff525252));
    g.drawRoundedRectangle (padGridArea.toFloat().reduced (0.5f), 3.0f, 1.0f);

    if (monitorMode)
    {
        g.setColour (juce::Colour (0xff7f7f7f));
        g.fillRoundedRectangle (monitorToolbarArea.toFloat(), 3.0f);
        g.setColour (juce::Colour (0xff4d4d4d));
        g.drawRoundedRectangle (monitorToolbarArea.toFloat().reduced (0.5f), 3.0f, 1.0f);
    }
}

void WaveLensEditor::resized()
{
    auto area = getLocalBounds().reduced (8, 6);

    auto topBar = area.removeFromTop (24);
    resetBtn.setBounds (topBar.removeFromRight (56));
    topBar.removeFromRight (4);
    holdBtn.setBounds (topBar.removeFromRight (56));
    topBar.removeFromRight (4);
    freezeBtn.setBounds (topBar.removeFromRight (56));

    area.removeFromTop (5);
    drumRackDeviceArea = area.removeFromLeft (juce::jmax (210, juce::roundToInt ((float) area.getWidth() * 0.25f)));
    area.removeFromLeft (6);
    simplerDeviceArea = area;

    auto leftInner = drumRackDeviceArea;
    drumRackHeaderArea = leftInner.removeFromTop (24);
    leftInner = leftInner.reduced (6);
    padGridArea = leftInner;

    // 7-Pad Drum Rack Grid: 2 cols x 4 rows (6 sample pads + full-width 7th Master pad)
    auto grid = padGridArea.reduced (4);
    const int cols = 2, rows = 4, gap = 5;
    const int cellW = (grid.getWidth() - gap * (cols - 1)) / cols;
    const int cellH = (grid.getHeight() - gap * (rows - 1)) / rows;

    auto cellAt = [&] (int row, int col)
    {
        return juce::Rectangle<int> (grid.getX() + col * (cellW + gap),
                                     grid.getY() + row * (cellH + gap),
                                     cellW, cellH);
    };

    auto layoutSamplePad = [&] (int layer, int row, int col)
    {
        auto cell = cellAt (row, col);
        padCellBounds[layer] = cell;
        auto bar = cell.removeFromBottom (18);
        layerSelectBtn[layer].setBounds (cell);
        const int bw = bar.getWidth() / 3;
        mutePadBtn[layer].setBounds (bar.removeFromLeft (bw));
        auditionBtn[layer].setBounds (bar.removeFromLeft (bw));
        soloPadBtn[layer].setBounds (bar);
    };

    // Rows 0..2: PRE 1..3 (left col) & MAIN 1..3 (right col)
    for (int r = 0; r < 3; ++r)
    {
        layoutSamplePad (r, r, 0);
        layoutSamplePad (r + 3, r, 1);
    }

    // Row 3: 7th Pad (MASTER) spanning full width
    padMasterBtn.setBounds (juce::Rectangle<int> (grid.getX(),
                                                  grid.getY() + 3 * (cellH + gap),
                                                  grid.getWidth(),
                                                  grid.getBottom() - (grid.getY() + 3 * (cellH + gap))));

    // Right Simpler / Monitor Device
    auto rightInner = simplerDeviceArea;
    simplerHeaderArea = rightInner.removeFromTop (24);

    auto hdrBtns = simplerHeaderArea.reduced (4, 3);
    clearSampleBtn.setBounds (hdrBtns.removeFromRight (22));
    hdrBtns.removeFromRight (4);
    loadSampleBtn.setBounds (hdrBtns.removeFromRight (100));

    auto body = rightInner.reduced (6);
    modeRailArea = body.removeFromLeft (54);
    body.removeFromLeft (6);

    const int modeH = modeRailArea.getHeight() / 3;
    auto rail = modeRailArea;
    masterBtn.setBounds (rail.removeFromTop (modeH));
    oneShotBtn.setBounds (rail.removeFromTop (modeH));
    monitorBtn.setBounds (rail);

    if (monitorMode)
    {
        monitorToolbarArea = body.removeFromTop (34);
        body.removeFromTop (6);
        auto tb = monitorToolbarArea.reduced (8, 5);
        viewBox.setBounds (tb.removeFromLeft (105));
        tb.removeFromLeft (8);
        layoutBox.setBounds (tb.removeFromLeft (95));
        tb.removeFromLeft (12);
        for (int n = 0; n < WaveLensProcessor::kMaxSignals; ++n)
        {
            sigBtn[n].setBounds (tb.removeFromLeft (32));
            tb.removeFromLeft (4);
        }

        meter.setBounds (body.removeFromRight (64));
        body.removeFromRight (6);
        scope.setBounds (body);
    }
    else
    {
        monitorToolbarArea = {};
        canvasArea = body;
        overlay.setBounds (canvasArea);
        for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
            if (envelopeComponents[(size_t) layer] != nullptr)
                envelopeComponents[(size_t) layer]->setBounds (canvasArea);
    }
}