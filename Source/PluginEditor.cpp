#include "PluginEditor.h"

namespace
{
    constexpr auto relaxed = std::memory_order_relaxed;

    const juce::Colour kBackground { 0xff202428 };
    const juce::Colour kPanel { 0xff292f35 }, kPanelEdge { 0xff424b54 };
    const juce::Colour kText { 0xffaeb8c0 }, kTextBright { 0xffedf2f5 };
    const juce::Colour kAccent { 0xffffb04a }, kCanvas { 0xff151a1f }, kCanvasText { 0xffe0e3e5 };
    const juce::Colour kMeterHot   { 0xffff6b5e };
    const juce::Colour kMeterCool  { 0xff7be07a };

    juce::String formatHz (double hz)
    {
        if (hz >= 1000.0)
            return juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz";
        return juce::String (juce::roundToInt (hz)) + " Hz";
    }

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
    setColour (juce::ResizableWindow::backgroundColourId, kBackground);

    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1b2229));
    setColour (juce::ComboBox::textColourId, kTextBright);
    setColour (juce::ComboBox::outlineColourId, kPanelEdge);
    setColour (juce::ComboBox::arrowColourId, kText);

    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1b2128));
    setColour (juce::PopupMenu::textColourId, kTextBright);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    setColour (juce::Slider::trackColourId, kAccent);
    setColour (juce::Slider::backgroundColourId, juce::Colour (0xff303942));
    setColour (juce::Slider::thumbColourId, kTextBright);
    setColour (juce::Slider::textBoxTextColourId, kTextBright);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff1b2128));
    setColour (juce::Slider::textBoxOutlineColourId, kPanelEdge);
    setColour (juce::Slider::textBoxHighlightColourId, kAccent.withAlpha (0.4f));

    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1b2128));
    setColour (juce::TextButton::buttonOnColourId, kAccent.withAlpha (0.85f));
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff04121a));

    setColour (juce::Label::textColourId, kText);
}

void WaveLensLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                const juce::Colour& backgroundColour,
                                                bool isMouseOverButton, bool isButtonDown)
{
    const auto componentId = button.getComponentID();

    if (componentId == "rack-master")
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        const bool active = button.getToggleState();
        const auto fill = active ? kAccent : juce::Colour (0xff20272e);
        g.setColour (isButtonDown ? fill.brighter (0.12f) : fill);
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (active ? kAccent : kPanelEdge);
        g.drawRoundedRectangle (bounds, 6.0f, active ? 1.6f : 1.0f);
        return;
    }

    if (componentId != "rack-pad")
    {
        LookAndFeel_V4::drawButtonBackground (g, button, backgroundColour,
                                              isMouseOverButton, isButtonDown);
        return;
    }

    const auto accent = button.findColour (juce::TextButton::textColourOffId);
    const bool selected = button.getToggleState();
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    auto fill = selected ? accent.withAlpha (0.19f) : juce::Colour (0xff20272e);

    if (! button.isEnabled())
        fill = juce::Colour (0xff1a2026);
    else if (isButtonDown)
        fill = fill.brighter (0.14f);
    else if (isMouseOverButton)
        fill = fill.brighter (0.07f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (accent.withAlpha (selected ? 0.95f : 0.42f));
    g.drawRoundedRectangle (bounds, 6.0f, selected ? 1.8f : 1.0f);

    auto marker = bounds.removeFromLeft (3.0f).reduced (0.0f, 8.0f);
    g.setColour (accent.withAlpha (selected ? 1.0f : 0.72f));
    g.fillRoundedRectangle (marker, 1.5f);
}

void WaveLensLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                          bool isMouseOverButton, bool isButtonDown)
{
    const auto componentId = button.getComponentID();

    if (componentId == "rack-master")
    {
        g.setColour (button.getToggleState() ? juce::Colour (0xff10151a) : kTextBright);
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (2),
                          juce::Justification::centred, 1);
        return;
    }

    if (componentId != "rack-pad")
    {
        LookAndFeel_V4::drawButtonText (g, button, isMouseOverButton, isButtonDown);
        return;
    }

    const auto text = button.getButtonText();
    const int separator = text.indexOfChar ('\\n');
    const auto title = separator >= 0 ? text.substring (0, separator) : text;
    const auto detail = separator >= 0 ? text.substring (separator + 1) : juce::String();
    const auto bounds = button.getLocalBounds().reduced (9, 6);
    const auto accent = button.findColour (juce::TextButton::textColourOffId);

    g.setColour (button.isEnabled() ? kTextBright : kText.withAlpha (0.42f));
    g.setFont (juce::Font (11.5f, juce::Font::bold));
    g.drawFittedText (title, bounds.removeFromTop (18), juce::Justification::centredLeft, 1);

    auto detailArea = button.getLocalBounds().reduced (9, 6);
    detailArea.removeFromTop (30);
    detailArea = detailArea.withTrimmedRight (8);

    const bool loaded = detail.isNotEmpty() && detail != "EMPTY SLOT";
    g.setColour (loaded ? kText : kText.withAlpha (0.55f));
    g.setFont (juce::Font (8.5f));
    g.drawFittedText (detail, detailArea, juce::Justification::centredLeft, 1);

    auto dot = juce::Rectangle<float> ((float) button.getWidth() - 13.0f, 8.0f, 5.0f, 5.0f);
    g.setColour (loaded ? kMeterCool : accent.withAlpha (0.62f));
    g.fillEllipse (dot);
}

//==============================================================================
// LayerOverlayView
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
    g.setColour (kPanelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    auto header = bounds.reduced (18.0f, 12.0f).removeFromTop (24.0f);
    g.setColour (kTextBright);
    g.setFont (juce::Font (13.0f, juce::Font::bold));
    g.drawText ("MASTER  /  ALL SOURCE LAYERS", header.removeFromLeft (260.0f),
                juce::Justification::centredLeft);

    auto plot = bounds.reduced (22.0f, 14.0f);
    plot.removeFromTop (38.0f);
    plot.removeFromBottom (32.0f);

    if (plot.getWidth() < 40.0f || plot.getHeight() < 40.0f)
        return;

    const float centre = plot.getCentreY();
    const float halfHeight = plot.getHeight() * 0.44f;

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    g.setColour (juce::Colours::white.withAlpha (0.045f));
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
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (juce::roundToInt (centre), plot.getX(), plot.getRight());

    int loadedCount = 0;
    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        const auto preview = processor.getWaveformPreview (layer);
        if (! preview.loaded)
            continue;

        ++loadedCount;
        juce::Path area, upper, lower;

        for (int point = 0; point < LayeredSampleEngine::kPreviewPoints; ++point)
        {
            const float x = plot.getX()
                + ((float) point / (float) (LayeredSampleEngine::kPreviewPoints - 1)) * plot.getWidth();
            const float top = centre - preview.maximum[(size_t) point] * halfHeight;
            const float bottom = centre - preview.minimum[(size_t) point] * halfHeight;

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
            const float x = plot.getX()
                + ((float) point / (float) (LayeredSampleEngine::kPreviewPoints - 1)) * plot.getWidth();
            const float bottom = centre - preview.minimum[(size_t) point] * halfHeight;
            area.lineTo (x, bottom);
        }
        area.closeSubPath();

        const auto colour = signalColour (layer);
        g.setColour (colour.withAlpha (0.10f));
        g.fillPath (area);
        g.setColour (colour.withAlpha (0.95f));
        g.strokePath (upper, juce::PathStrokeType (1.7f));
        g.setColour (colour.withAlpha (0.42f));
        g.strokePath (lower, juce::PathStrokeType (0.9f));
    }

    g.restoreState();

    auto legend = bounds.reduced (18.0f, 8.0f).removeFromBottom (20.0f);
    float x = legend.getX();
    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        const auto preview = processor.getWaveformPreview (layer);
        const float itemWidth = 106.0f;
        auto item = juce::Rectangle<float> (x, legend.getY(), itemWidth, legend.getHeight());
        g.setColour (signalColour (layer));
        g.fillRoundedRectangle (item.removeFromLeft (8.0f).withSizeKeepingCentre (7.0f, 7.0f), 3.0f);
        g.setColour (preview.loaded ? kTextBright : kText.withAlpha (0.45f));
        g.setFont (juce::Font (10.0f, juce::Font::bold));
        const auto label = layer < 3 ? "PRE " + juce::String (layer + 1)
                                     : "MAIN " + juce::String (layer - 2);
        g.drawText (label, item.withTrimmedLeft (4.0f), juce::Justification::centredLeft);
        x += itemWidth;
    }

    if (loadedCount == 0)
    {
        g.setColour (kText);
        g.setFont (juce::Font (15.0f));
        g.drawText ("Load samples from the left rack to build your six-layer sound",
                    plot.toNearestInt(), juce::Justification::centred);
    }
    else
    {
        g.setColour (kText);
        g.setFont (juce::Font (10.5f));
        g.drawText (juce::String (loadedCount) + " / 6 source layers loaded",
                    header.withY (header.getY() + 2.0f).withX (header.getRight() - 150.0f).withWidth (150.0f),
                    juce::Justification::centredRight);
    }
}

//==============================================================================
// ScopeView
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
        if (scroll || ! s.heldValid)
            return;
        first = s.heldFrom;
        count = s.heldTo - s.heldFrom + 1;
    }
    else
    {
        if (! s.has)
            return;

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

    // The previous pass lives in its own pair of buffers.
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

    // Grid
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

    // With several overlapping traces the fills are lighter so every trace stays readable.
    const float fillAlpha = traces.size() > 1 ? 0.10f : 0.30f;

    // Previous pass first, so it sits behind the current one. Traces are drawn from the last
    // to the first, which keeps signal 1 on top of the stack.
    if (showHeld)
        for (auto it = traces.rbegin(); it != traces.rend(); ++it)
            drawTrace (g, lane, it->ch, it->colour, s, gain, true, fillAlpha);

    for (auto it = traces.rbegin(); it != traces.rend(); ++it)
        drawTrace (g, lane, it->ch, it->colour, s, gain, false, fillAlpha);

    // Write cursor for the fixed-window modes.
    if (mode != 2 && s.has)
    {
        const float x = lane.getX() + ((float) s.head + 0.5f) * lane.getWidth() / (float) WaveLensProcessor::kCols;
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawVerticalLine (juce::roundToInt (x), lane.getY(), lane.getBottom());
    }

    if (title.isNotEmpty())
    {
        g.setColour (kText);
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
    g.fillAll (kCanvas);
    g.setColour (kCanvas);
    g.fillRoundedRectangle (bounds.reduced (0.5f), 6.0f);
    g.setColour (kPanelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

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

    // Signals that are routed in the host and not hidden with their toggle.
    std::vector<int> active;
    for (int n = 0; n < WaveLensProcessor::kMaxSignals; ++n)
        if ((mask & (1 << n)) != 0 && pShowSig[n]->load() > 0.5f)
            active.push_back (n);

    const bool split = active.size() > 1 && juce::roundToInt (pLayout->load()) == 1;

    auto area = bounds.reduced (6.0f);
    area.removeFromBottom (16.0f);          // room for the footer text

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

        // Colour key, so the overlapping traces can be told apart.
        if (traces.size() > 1)
        {
            g.setFont (juce::Font (11.0f, juce::Font::bold));
            float x = area.getRight() - 8.0f;
            for (int k = (int) active.size() - 1; k >= 0; --k)
            {
                const int n = active[(size_t) k];
                auto chip = juce::Rectangle<float> (x - 24.0f, area.getY() + 6.0f, 24.0f, 14.0f);
                g.setColour (signalColour (n).withAlpha (0.9f));
                g.fillRoundedRectangle (chip, 3.0f);
                g.setColour (juce::Colour (0xff04121a));
                g.drawText (n < 3 ? ("P" + juce::String (n + 1)) : ("M" + juce::String (n - 2)),
                            chip, juce::Justification::centred);
                x -= 28.0f;
            }
        }
    }

    // Footer
    auto footer = bounds.reduced (10.0f, 3.0f).removeFromBottom (14.0f);
    g.setFont (juce::Font (11.0f));
    g.setColour (kText);
    g.drawText (windowLabel(), footer, juce::Justification::centredLeft);

    if (mask == 0)
        g.drawText ("Load samples into the PRE and MAIN slots below to view their waveforms and spectra",
                    footer, juce::Justification::centred);
    else
        g.drawText (juce::String (active.size()) + (active.size() == 1 ? " layer" : " layers"),
                    footer, juce::Justification::centred);

    if (pFreeze->load() > 0.5f)
    {
        g.setColour (kAccent);
        g.drawText ("FROZEN", footer, juce::Justification::centredRight);
    }
}

//==============================================================================
// MeterView
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
    g.fillAll (kBackground);
    g.setColour (kPanel);
    g.fillRoundedRectangle (bounds.reduced (0.5f), 6.0f);
    g.setColour (kPanelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    const int mode = juce::roundToInt (pMeter->load());
    static const char* names[] = { "PEAK", "RMS", "CREST" };

    float now = 0.0f, hold = 0.0f, lo = -60.0f, hi = 0.0f;
    float nowDb = 0.0f, holdDb = 0.0f;

    auto toDb = [] (float v) { return 20.0f * std::log10 (juce::jmax (v, 1.0e-6f)); };

    if (mode == 0)      { now = proc.peakNow.load (relaxed);  hold = proc.peakHold.load (relaxed);  }
    else if (mode == 1) { now = proc.rmsNow.load (relaxed);   hold = proc.rmsHold.load (relaxed);   }
    else                { now = proc.crestNow.load (relaxed); hold = proc.crestHold.load (relaxed); lo = 0.0f; hi = 24.0f; }

    nowDb = toDb (now);
    holdDb = toDb (hold);

    auto area = bounds.reduced (8.0f);
    g.setColour (kAccent);
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.drawText (names[juce::jlimit (0, 2, mode)], area.removeFromTop (16.0f), juce::Justification::centred);

    auto textArea = area.removeFromBottom (40.0f);
    auto bar = area.reduced (area.getWidth() * 0.3f, 4.0f);

    g.setColour (juce::Colour (0xff1b2128));
    g.fillRoundedRectangle (bar, 3.0f);

    auto norm = [&] (float db) { return juce::jlimit (0.0f, 1.0f, (db - lo) / (hi - lo)); };

    const float h = bar.getHeight() * norm (nowDb);
    juce::ColourGradient grad (kMeterHot, 0.0f, bar.getY(), kMeterCool, 0.0f, bar.getBottom(), false);
    grad.addColour (0.25, juce::Colour (0xffffd24a));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (bar.withTrimmedTop (bar.getHeight() - h), 3.0f);

    if (hold > 0.0f)
    {
        const float y = bar.getBottom() - bar.getHeight() * norm (holdDb);
        g.setColour (kTextBright);
        g.fillRect (bar.getX() - 3.0f, y - 1.0f, bar.getWidth() + 6.0f, 2.0f);
    }

    g.setColour (kTextBright);
    g.setFont (juce::Font (13.0f, juce::Font::bold));
    juce::String nowText = mode == 2 ? juce::String (nowDb, 1) + " dB"
                                     : formatDb (now);
    g.drawText (nowText, textArea.removeFromTop (20.0f), juce::Justification::centred);

    g.setColour (kText);
    g.setFont (juce::Font (11.0f));
    juce::String holdText = mode == 2 ? juce::String (holdDb, 1) + " dB" : formatDb (hold);
    g.drawText ("hold " + holdText, textArea, juce::Justification::centred);
}

//==============================================================================
// Editor
//==============================================================================
WaveLensEditor::WaveLensEditor (WaveLensProcessor& p)
    : AudioProcessorEditor (&p), proc (p), scope (p), overlay (p), meter (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (scope);
    addAndMakeVisible (overlay);
    addAndMakeVisible (meter);

    setupCombo (viewBox,     ids::view,     viewAtt);
    setupCombo (captureBox,  ids::capture,  captureAtt);
    setupCombo (barsBox,     ids::bars,     barsAtt);
    setupCombo (inputBox,    ids::input,    inputAtt);
    setupCombo (layoutBox,   ids::layout,   layoutAtt);
    setupCombo (triggerModeBox, ids::triggerMode, triggerModeAtt);

    setupToggle (listenBtn, "Listen",    ids::listen,   listenAtt);
    setupToggle (freezeBtn, "Freeze",    ids::freeze,   freezeAtt);
    setupToggle (holdBtn,   "Hold",      ids::hold,     holdAtt);

    masterBtn.setButtonText ("M");
    masterBtn.setComponentID ("rack-master");
    masterBtn.setTooltip ("Master view: overlay all six loaded source waveforms");
    masterBtn.setClickingTogglesState (false);
    masterBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1b2229));
    masterBtn.setColour (juce::TextButton::buttonOnColourId, kAccent);
    masterBtn.setColour (juce::TextButton::textColourOffId, kTextBright);
    masterBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff10151a));
    addAndMakeVisible (masterBtn);
    masterBtn.onClick = [this]
    {
        selectedLayer = -1;
        monitorMode = false;
        updateViewVisibility();
    };

    monitorBtn.setButtonText ("MONITOR / FFT");
    monitorBtn.setTooltip ("Open the live waveform and multi-signal spectrum monitor");
    monitorBtn.setClickingTogglesState (false);
    monitorBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1b2229));
    monitorBtn.setColour (juce::TextButton::buttonOnColourId, kAccent.withAlpha (0.85f));
    monitorBtn.setColour (juce::TextButton::textColourOffId, kTextBright);
    monitorBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff10151a));
    addAndMakeVisible (monitorBtn);
    monitorBtn.onClick = [this]
    {
        monitorMode = true;
        updateViewVisibility();
    };

    // One toggle per signal, in that signal's trace colour.
    for (int n = 0; n < WaveLensProcessor::kMaxSignals; ++n)
    {
        setupToggle (sigBtn[n], n < 3 ? ("P" + juce::String (n + 1)) : ("M" + juce::String (n - 2)), ids::showSig[n], sigAtt[n]);
        sigBtn[n].setColour (juce::TextButton::buttonOnColourId, signalColour (n));
        sigBtn[n].setColour (juce::TextButton::textColourOnId, juce::Colour (0xff04121a));
    }

    resetBtn.setButtonText ("Reset");
    resetBtn.onClick = [this] { proc.requestReset(); };
    addAndMakeVisible (resetBtn);

    auto setupSlider = [this] (juce::Slider& s, const char* id, std::unique_ptr<SliderAtt>& att,
                               std::function<juce::String (double)> fmt)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20);
        addAndMakeVisible (s);
        att = std::make_unique<SliderAtt> (proc.apvts, id, s);

        // The attachment installs its own text conversion, so ours goes in afterwards.
        s.textFromValueFunction = std::move (fmt);
        s.updateText();
    };

    setupSlider (timeSlider, ids::timeMs,  timeAtt, [] (double v) { return formatMs (v); });
    setupSlider (gainSlider, ids::gain,    gainAtt, [] (double v) { return juce::String (v, 1) + " dB"; });
    setupSlider (loSlider,   ids::rangeLo, loAtt,   [] (double v) { return formatHz (v); });
    setupSlider (hiSlider,   ids::rangeHi, hiAtt,   [] (double v) { return formatHz (v); });
    setupSlider (thresholdSlider, ids::threshold, thresholdAtt,
                 [] (double v) { return juce::String (v * 100.0, 0) + "%"; });
    setupSlider (preRollSlider, ids::preRollMs, preRollAtt,
                 [] (double v) { return formatMs (v); });

    // Typing a value like "2k" or "250" should work in the Hz boxes.
    auto hzFromText = [] (const juce::String& t)
    {
        auto s = t.trim().toLowerCase();
        double mult = 1.0;
        if (s.endsWithChar ('k') || s.contains ("khz")) mult = 1000.0;
        return s.getDoubleValue() * mult;
    };
    loSlider.valueFromTextFunction = hzFromText;
    hiSlider.valueFromTextFunction = hzFromText;

    // The old global Time / Trigger / Range controls are intentionally removed from the UI.
    // Trigger detection remains active internally with a low threshold and Audio + MIDI support.
    captureBox.setVisible (false);
    barsBox.setVisible (false);
    inputBox.setVisible (false);
    triggerModeBox.setVisible (false);
    listenBtn.setVisible (false);
    timeSlider.setVisible (false);
    gainSlider.setVisible (false);
    loSlider.setVisible (false);
    hiSlider.setVisible (false);
    thresholdSlider.setVisible (false);
    preRollSlider.setVisible (false);

    // Six sample slots: three amber PRE layers, followed by three cyan MAIN layers.
    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        const bool isPre = layer < 3;
        const juce::String slot = (isPre ? "PRE " : "MAIN ") + juce::String (isPre ? layer + 1 : layer - 2);
        layerHeader[layer].setText (slot + "  /  OUT " + juce::String (layer + 1),
                                    juce::dontSendNotification);

        layerSelectBtn[layer].setButtonText (slot + "\\nEMPTY SLOT");
        layerSelectBtn[layer].setComponentID ("rack-pad");
        layerSelectBtn[layer].setClickingTogglesState (false);
        layerSelectBtn[layer].setTooltip ("Select " + slot + " sample and its envelope controls");
        layerSelectBtn[layer].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff20272e));
        layerSelectBtn[layer].setColour (juce::TextButton::buttonOnColourId, signalColour (layer).withAlpha (0.88f));
        layerSelectBtn[layer].setColour (juce::TextButton::textColourOffId, signalColour (layer));
        layerSelectBtn[layer].setColour (juce::TextButton::textColourOnId, juce::Colour (0xff10151a));
        addAndMakeVisible (layerSelectBtn[layer]);
        layerSelectBtn[layer].onClick = [this, layer]
        {
            selectedLayer = layer;
            monitorMode = false;
            updateViewVisibility();
        };
        layerHeader[layer].setFont (juce::Font (10.5f, juce::Font::bold));
        layerHeader[layer].setColour (juce::Label::textColourId, signalColour (layer));
        addAndMakeVisible (layerHeader[layer]);

        envelopeComponents[(size_t) layer] = std::make_unique<LayerEnvelopeComponent> (proc, layer, signalColour (layer));
        addAndMakeVisible (*envelopeComponents[(size_t) layer]);

        loadSampleBtn[layer].setButtonText ("Load sample...");
        loadSampleBtn[layer].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1b2229));
        loadSampleBtn[layer].setColour (juce::TextButton::buttonOnColourId, signalColour (layer).withAlpha (0.85f));
        loadSampleBtn[layer].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe7edf2));
        addAndMakeVisible (loadSampleBtn[layer]);

        loadSampleBtn[layer].onClick = [this, layer]
        {
            auto chooser = std::make_shared<juce::FileChooser> (
                "Load sample for " + layerHeader[layer].getText(),
                juce::File(),
                "*.wav;*.aif;*.aiff;*.flac;*.ogg");

            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                [this, layer, chooser] (const juce::FileChooser& fc)
                {
                    const auto file = fc.getResult();
                    if (file.existsAsFile())
                    {
                        juce::String error;
                        if (! proc.loadSampleFile (layer, file, error))
                            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                    "Sample load failed", error);
                    }
                    updateEnabledStates();
                });
        };

        auditionBtn[layer].setButtonText (">");
        auditionBtn[layer].setTooltip ("Audition this sample layer");
        auditionBtn[layer].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1b2229));
        auditionBtn[layer].setColour (juce::TextButton::buttonOnColourId, signalColour (layer).withAlpha (0.9f));
        auditionBtn[layer].setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe7edf2));
        addAndMakeVisible (auditionBtn[layer]);
        auditionBtn[layer].onClick = [this, layer] { proc.requestPreview (layer); };

        layerGainSlider[layer].setSliderStyle (juce::Slider::LinearHorizontal);
        layerGainSlider[layer].setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 16);
        layerGainSlider[layer].setColour (juce::Slider::trackColourId, signalColour (layer));
        addAndMakeVisible (layerGainSlider[layer]);
        layerGainAtt[layer] = std::make_unique<SliderAtt> (proc.apvts, ids::layerGain (layer), layerGainSlider[layer]);
        layerGainSlider[layer].textFromValueFunction = [] (double value)
        {
            return juce::String (value * 100.0, 0) + "%";
        };
        layerGainSlider[layer].updateText();

        layerDelaySlider[layer].setSliderStyle (juce::Slider::LinearHorizontal);
        layerDelaySlider[layer].setTextBoxStyle (juce::Slider::TextBoxRight, false, 39, 16);
        layerDelaySlider[layer].setColour (juce::Slider::trackColourId, signalColour (layer));
        layerDelaySlider[layer].setTooltip ("Layer timing shift: negative advances the sample; positive delays it");
        addAndMakeVisible (layerDelaySlider[layer]);
        layerDelayAtt[layer] = std::make_unique<SliderAtt> (proc.apvts, ids::layerDelay (layer), layerDelaySlider[layer]);
        layerDelaySlider[layer].textFromValueFunction = [] (double value)
        {
            return juce::String (value > 0.0 ? "+" : "") + juce::String (value, 1) + "ms";
        };
        layerDelaySlider[layer].updateText();
    }

    setResizable (true, true);
    setResizeLimits (1000, 620, 1800, 1200);
    setSize (1180, 720);

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

void WaveLensEditor::updateEnabledStates()
{
    const auto& a = proc.apvts;
    const int mask = proc.connectedMask.load();

    // A layer's visibility toggle is enabled once its sample has been loaded.
    for (int n = 0; n < WaveLensProcessor::kMaxSignals; ++n)
    {
        const bool loaded = (mask & (1 << n)) != 0;
        sigBtn[n].setEnabled (loaded);
        auditionBtn[n].setEnabled (loaded);
    }

    int loadedLayerCount = 0;
    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
        if ((mask & (1 << layer)) != 0)
            ++loadedLayerCount;
    layoutBox.setEnabled (loadedLayerCount > 1);

    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        const auto sampleName = proc.getSampleName (layer);
        if (sampleName != displayedSampleNames[(size_t) layer])
        {
            displayedSampleNames[(size_t) layer] = sampleName;
            loadSampleBtn[layer].setButtonText (sampleName.isEmpty()
                                                    ? "Load sample..."
                                                    : sampleName.substring (0, 22));

            const auto slot = layer < 3 ? "PRE " + juce::String (layer + 1)
                                        : "MAIN " + juce::String (layer - 2);
            const auto padName = sampleName.isEmpty() ? juce::String ("EMPTY SLOT")
                                                       : sampleName.substring (0, 15).toUpperCase();
            layerSelectBtn[layer].setButtonText (slot + "\\n" + padName);
        }
    }
}

void WaveLensEditor::updateViewVisibility()
{
    const bool showMaster = selectedLayer < 0 && ! monitorMode;
    const bool showLayer = selectedLayer >= 0 && ! monitorMode;

    masterBtn.setToggleState (showMaster, juce::dontSendNotification);
    monitorBtn.setToggleState (monitorMode, juce::dontSendNotification);

    overlay.setVisible (showMaster);
    scope.setVisible (monitorMode);
    meter.setVisible (monitorMode);

    viewBox.setVisible (monitorMode);
    layoutBox.setVisible (monitorMode);
    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        sigBtn[layer].setVisible (monitorMode);
        const bool selected = showLayer && selectedLayer == layer;
        layerSelectBtn[layer].setToggleState (selected, juce::dontSendNotification);
        layerHeader[layer].setVisible (selected);
        loadSampleBtn[layer].setVisible (selected);
        auditionBtn[layer].setVisible (selected);
        layerGainSlider[layer].setVisible (selected);
        layerDelaySlider[layer].setVisible (selected);
        if (envelopeComponents[(size_t) layer] != nullptr)
            envelopeComponents[(size_t) layer]->setVisible (selected);
    }

    resized();
    repaint();
}

void WaveLensEditor::timerCallback()
{
    updateEnabledStates();
}

void WaveLensEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    g.setColour (kTextBright);
    g.setFont (juce::Font (20.0f, juce::Font::bold));
    g.drawText ("WaveLens", 14, 7, 150, 28, juce::Justification::centredLeft);

    g.setColour (kText);
    g.setFont (juce::Font (11.5f));
    g.drawText ("SIX-LAYER TRANSIENT SAMPLER", 164, 11, 260, 20,
                juce::Justification::centredLeft);

    g.setColour (kPanel);
    g.fillRoundedRectangle (rackArea.toFloat(), 7.0f);
    g.setColour (kPanelEdge);
    g.drawRoundedRectangle (rackArea.toFloat().reduced (0.5f), 7.0f, 1.0f);

    g.setColour (kPanel);
    g.fillRoundedRectangle (workspaceArea.toFloat(), 7.0f);
    g.setColour (kPanelEdge);
    g.drawRoundedRectangle (workspaceArea.toFloat().reduced (0.5f), 7.0f, 1.0f);

    g.setColour (kPanel.withBrightness (1.05f));
    g.fillRoundedRectangle (workspaceToolbarArea.toFloat().reduced (1.0f), 5.0f);

    g.setColour (kTextBright);
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.drawText ("DRUM RACK", rackArea.getX() + 12, rackArea.getY() + 8,
                rackArea.getWidth() - 58, 20, juce::Justification::centredLeft);

    g.setColour (signalColour (0));
    g.setFont (juce::Font (9.5f, juce::Font::bold));
    g.drawText ("PRE-SAMPLE BANK", rackArea.getX() + 12, layerSelectBtn[0].getY() - 18,
                rackArea.getWidth() - 24, 14, juce::Justification::centredLeft);

    g.setColour (signalColour (3));
    g.drawText ("MAIN-SAMPLE BANK", rackArea.getX() + 12, layerSelectBtn[3].getY() - 18,
                rackArea.getWidth() - 24, 14, juce::Justification::centredLeft);

    if (monitorMode)
    {
        g.setColour (kText);
        g.setFont (juce::Font (9.0f, juce::Font::bold));
        g.drawText ("DISPLAY", viewBox.getX(), workspaceToolbarArea.getY() + 4,
                    viewBox.getWidth(), 12, juce::Justification::centredLeft);
        g.drawText ("LAYOUT", layoutBox.getX(), workspaceToolbarArea.getY() + 4,
                    layoutBox.getWidth(), 12, juce::Justification::centredLeft);
        if (sigBtn[0].isVisible())
            g.drawText ("VISIBLE TRACES", sigBtn[0].getX(), workspaceToolbarArea.getY() + 4,
                        sigBtn[5].getRight() - sigBtn[0].getX(), 12,
                        juce::Justification::centredLeft);
    }
    else if (selectedLayer >= 0)
    {
        const int layer = juce::jlimit (0, WaveLensProcessor::kMaxSignals - 1, selectedLayer);
        g.setColour (signalColour (layer));
        g.setFont (juce::Font (9.5f, juce::Font::bold));
        g.drawText ("SAMPLE", loadSampleBtn[layer].getX(), workspaceToolbarArea.getY() + 5,
                    loadSampleBtn[layer].getWidth(), 12, juce::Justification::centredLeft);
        g.drawText ("LEVEL", layerGainSlider[layer].getX(), workspaceToolbarArea.getY() + 5,
                    layerGainSlider[layer].getWidth(), 12, juce::Justification::centredLeft);
        g.drawText ("TIMING SHIFT", layerDelaySlider[layer].getX(), workspaceToolbarArea.getY() + 5,
                    layerDelaySlider[layer].getWidth(), 12, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (kText);
        g.setFont (juce::Font (10.0f, juce::Font::bold));
        g.drawText ("MASTER WAVEFORM OVERLAY  /  ALL LOADED SAMPLES",
                    workspaceToolbarArea.reduced (12, 0), juce::Justification::centredLeft);
    }
}

void WaveLensEditor::resized()
{
    auto area = getLocalBounds().reduced (12, 7);

    auto header = area.removeFromTop (32);
    resetBtn.setBounds (header.removeFromRight (62).withHeight (24).withY (header.getY() + 3));
    header.removeFromRight (5);
    holdBtn.setBounds (header.removeFromRight (62).withHeight (24).withY (header.getY() + 3));
    header.removeFromRight (5);
    freezeBtn.setBounds (header.removeFromRight (62).withHeight (24).withY (header.getY() + 3));

    area.removeFromTop (7);
    rackArea = area.removeFromLeft (236);
    area.removeFromLeft (10);
    workspaceArea = area;

    auto rack = rackArea.reduced (10, 9);
    auto rackHeader = rack.removeFromTop (30);
    masterBtn.setBounds (rackHeader.removeFromRight (34).withHeight (28)
                                      .withY (rackHeader.getY() + 1));

    rack.removeFromTop (12);
    rack.removeFromTop (16);
    rack.removeFromTop (5);

    constexpr int padGap = 6;
    constexpr int padHeight = 76;
    const int padWidth = (rack.getWidth() - padGap * 2) / 3;
    auto preRow = rack.removeFromTop (padHeight);
    for (int column = 0; column < 3; ++column)
    {
        const int x = rack.getX() + column * (padWidth + padGap);
        const int width = column == 2 ? rack.getRight() - x : padWidth;
        layerSelectBtn[column].setBounds (x, preRow.getY(), width, preRow.getHeight());
    }

    rack.removeFromTop (15);
    rack.removeFromTop (16);
    rack.removeFromTop (5);
    auto mainRow = rack.removeFromTop (padHeight);
    for (int column = 0; column < 3; ++column)
    {
        const int x = rack.getX() + column * (padWidth + padGap);
        const int width = column == 2 ? rack.getRight() - x : padWidth;
        layerSelectBtn[column + 3].setBounds (x, mainRow.getY(), width, mainRow.getHeight());
    }

    monitorBtn.setBounds (rack.removeFromBottom (36));

    auto workspace = workspaceArea.reduced (8, 8);
    workspaceToolbarArea = workspace.removeFromTop (62);
    workspace.removeFromTop (8);
    canvasArea = workspace;

    if (monitorMode)
    {
        auto monitorArea = canvasArea.reduced (2);
        meter.setBounds (monitorArea.removeFromRight (68));
        monitorArea.removeFromRight (7);
        scope.setBounds (monitorArea);
    }
    else
    {
        scope.setBounds (canvasArea.reduced (2));
        meter.setBounds (juce::Rectangle<int>());
    }

    overlay.setBounds (canvasArea.reduced (2));
    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
        envelopeComponents[(size_t) layer]->setBounds (canvasArea.reduced (2));

    if (monitorMode)
    {
        auto controls = workspaceToolbarArea.reduced (10, 0);
        controls.removeFromTop (18);
        viewBox.setBounds (controls.removeFromLeft (110).withHeight (25));
        controls.removeFromLeft (8);
        layoutBox.setBounds (controls.removeFromLeft (100).withHeight (25));
        controls.removeFromLeft (18);
        const int gap = 5;
        const int toggleWidth = juce::jlimit (34, 48,
            (controls.getWidth() - gap * 5) / 6);
        for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
        {
            sigBtn[layer].setBounds (controls.removeFromLeft (toggleWidth).withHeight (25));
            if (layer != WaveLensProcessor::kMaxSignals - 1)
                controls.removeFromLeft (gap);
        }
    }
    else if (selectedLayer >= 0)
    {
        const int layer = juce::jlimit (0, WaveLensProcessor::kMaxSignals - 1, selectedLayer);
        auto controls = workspaceToolbarArea.reduced (10, 0);
        controls.removeFromTop (19);

        const int available = controls.getWidth();
        const int headerWidth = juce::jlimit (88, 108, (int) (available * 0.14f));
        const int sampleWidth = juce::jlimit (170, 225, (int) (available * 0.29f));
        const int levelWidth = juce::jlimit (145, 180, (int) (available * 0.22f));
        constexpr int gap = 8;

        layerHeader[layer].setBounds (controls.removeFromLeft (headerWidth).withHeight (25));
        controls.removeFromLeft (gap);

        auto sampleArea = controls.removeFromLeft (sampleWidth);
        auditionBtn[layer].setBounds (sampleArea.removeFromRight (28).withHeight (25));
        sampleArea.removeFromRight (4);
        loadSampleBtn[layer].setBounds (sampleArea.withHeight (25));
        controls.removeFromLeft (gap);

        auto levelArea = controls.removeFromLeft (levelWidth);
        layerGainSlider[layer].setBounds (levelArea.withHeight (24));
        controls.removeFromLeft (gap);

        layerDelaySlider[layer].setBounds (controls.withHeight (24));
    }
    else
    {
        viewBox.setBounds (juce::Rectangle<int>());
        layoutBox.setBounds (juce::Rectangle<int>());
        for (auto& button : sigBtn)
            button.setBounds (juce::Rectangle<int>());
    }
}
