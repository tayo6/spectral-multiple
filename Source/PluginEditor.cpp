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
    : AudioProcessorEditor (&p), proc (p), scope (p), meter (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (scope);
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

    // Six sample slots: three amber PRE layers, followed by three cyan MAIN layers.
    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        const bool isPre = layer < 3;
        const juce::String slot = (isPre ? "PRE " : "MAIN ") + juce::String (isPre ? layer + 1 : layer - 2);
        layerHeader[layer].setText (slot + "  /  OUT " + juce::String (layer + 1),
                                    juce::dontSendNotification);
        layerHeader[layer].setFont (juce::Font (10.5f, juce::Font::bold));
        layerHeader[layer].setColour (juce::Label::textColourId, signalColour (layer));
        addAndMakeVisible (layerHeader[layer]);

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
    }

    setResizable (true, true);
    setResizeLimits (900, 620, 1920, 1200);
    setSize (1060, 700);

    updateEnabledStates();
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
    const int capture = juce::roundToInt (a.getRawParameterValue (ids::capture)->load());
    const int mask = proc.connectedMask.load();

    barsBox.setVisible (capture == 0);
    timeSlider.setVisible (capture != 0);

    // A signal's toggle is available once an input is routed to it in the host.
    for (int n = 0; n < WaveLensProcessor::kMaxSignals; ++n)
        sigBtn[n].setEnabled ((mask & (1 << n)) != 0);

    layoutBox.setEnabled ((mask & ~1) != 0);

    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        const auto sampleName = proc.getSampleName (layer);
        if (sampleName != displayedSampleNames[(size_t) layer])
        {
            displayedSampleNames[(size_t) layer] = sampleName;
            loadSampleBtn[layer].setButtonText (sampleName.isEmpty()
                                                    ? "Load sample..."
                                                    : sampleName.substring (0, 22));
        }
    }
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
    g.drawText ("WaveLens", 16, 8, 160, 28, juce::Justification::centredLeft);

    g.setColour (kText);
    g.setFont (juce::Font (12.0f));
    g.drawText ("six-layer transient sampler", 120, 12, 220, 22, juce::Justification::centredLeft);

    for (auto& grp : groups)
    {
        g.setColour (kPanel);
        g.fillRoundedRectangle (grp.area.toFloat(), 6.0f);
        g.setColour (kPanelEdge);
        g.drawRoundedRectangle (grp.area.toFloat().reduced (0.5f), 6.0f, 1.0f);

        g.setColour (grp.title.startsWith ("PRE") ? signalColour (0)
                                                   : (grp.title.startsWith ("MAIN") ? signalColour (3) : kAccent));
        g.setFont (juce::Font (10.5f, juce::Font::bold));
        g.drawText (grp.title.toUpperCase(), grp.area.withHeight (20).withTrimmedLeft (10), juce::Justification::centredLeft);
    }

    g.setColour (kText);
    g.setFont (juce::Font (11.5f));
    for (auto& c : captions)
        g.drawText (c.text, c.area, juce::Justification::centredLeft);
}

void WaveLensEditor::resized()
{
    captions.clear();
    groups.clear();

    auto area = getLocalBounds().reduced (12, 8);

    auto header = area.removeFromTop (30);
    resetBtn.setBounds  (header.removeFromRight (70));
    header.removeFromRight (6);
    holdBtn.setBounds   (header.removeFromRight (70));
    header.removeFromRight (6);
    freezeBtn.setBounds (header.removeFromRight (70));
    area.removeFromTop (6);

    constexpr int layerPanelH = 88;
    auto layerPanel = area.removeFromBottom (layerPanelH);
    area.removeFromBottom (8);

    constexpr int controlPanelH = 146;
    auto panel = area.removeFromBottom (controlPanelH);
    area.removeFromBottom (8);

    meter.setBounds (area.removeFromRight (84));
    area.removeFromRight (8);
    scope.setBounds (area);

    // Sample banks: three PRE layers and three MAIN layers.
    auto bankArea = layerPanel.reduced (2, 0);
    const int bankGap = 8;
    const int bankWidth = (bankArea.getWidth() - bankGap) / 2;
    auto preBank = bankArea.removeFromLeft (bankWidth);
    bankArea.removeFromLeft (bankGap);
    auto mainBank = bankArea;

    groups.push_back ({ "PRE LAYERS", preBank });
    groups.push_back ({ "MAIN LAYERS", mainBank });

    for (int layer = 0; layer < WaveLensProcessor::kMaxSignals; ++layer)
    {
        auto bank = layer < 3 ? preBank : mainBank;
        const int localIndex = layer < 3 ? layer : layer - 3;
        const int cellWidth = bank.getWidth() / 3;
        const int cellX = bank.getX() + localIndex * cellWidth;
        auto cell = juce::Rectangle<int> (cellX, bank.getY() + 20,
                                          localIndex == 2 ? bank.getRight() - cellX : cellWidth,
                                          bank.getHeight() - 22).reduced (3, 0);

        layerHeader[layer].setBounds (cell.removeFromTop (14));
        cell.removeFromTop (2);
        loadSampleBtn[layer].setBounds (cell.removeFromTop (24));
        cell.removeFromTop (2);
        layerGainSlider[layer].setBounds (cell.removeFromTop (24));
    }

    // Compact global controls.
    constexpr int numGroups = 4;
    const float weights[numGroups] = { 1.1f, 1.1f, 1.4f, 1.4f };
    const char* titles[numGroups] = { "Time", "Trigger", "Range", "Layers" };
    float total = 0.0f;
    for (float w : weights) total += w;

    juce::Rectangle<int> cells[numGroups];
    const int gap = 8;
    const int usable = panel.getWidth() - gap * (numGroups - 1);
    int x = panel.getX();
    for (int i = 0; i < numGroups; ++i)
    {
        const int w = i == numGroups - 1 ? panel.getRight() - x : juce::roundToInt (usable * weights[i] / total);
        cells[i] = { x, panel.getY(), w, panel.getHeight() };
        groups.push_back ({ titles[i], cells[i] });
        x += w + gap;
    }

    constexpr int rowH = 26, rowGap = 6, captionW = 52;

    auto rowOf = [=] (juce::Rectangle<int> cell, int row)
    {
        return juce::Rectangle<int> (cell.getX() + 10, cell.getY() + 24 + row * (rowH + rowGap),
                                     cell.getWidth() - 20, rowH);
    };

    auto labelled = [&] (juce::Rectangle<int> cell, int row, const juce::String& text, juce::Component& c)
    {
        auto r = rowOf (cell, row);
        captions.push_back ({ text, r.removeFromLeft (captionW) });
        c.setBounds (r);
    };

    labelled (cells[0], 0, "Capture", captureBox);
    {
        auto r = rowOf (cells[0], 1);
        captions.push_back ({ "Window", r.removeFromLeft (captionW) });
        barsBox.setBounds (r);
        timeSlider.setBounds (r);
    }

    labelled (cells[1], 0, "Input mode", inputBox);
    labelled (cells[1], 1, "Display gain", gainSlider);
    labelled (cells[1], 2, "Source", triggerModeBox);
    labelled (cells[1], 3, "Threshold", thresholdSlider);

    labelled (cells[2], 0, "Low cut", loSlider);
    labelled (cells[2], 1, "High cut", hiSlider);
    listenBtn.setBounds (rowOf (cells[2], 2));

    {
        auto r = rowOf (cells[3], 0);
        const int n = WaveLensProcessor::kMaxSignals;
        const int bw = (r.getWidth() - 3 * (n - 1)) / n;
        for (int i = 0; i < n; ++i)
        {
            sigBtn[i].setBounds (r.removeFromLeft (bw));
            r.removeFromLeft (3);
        }
    }
    labelled (cells[3], 1, "Layout", layoutBox);
    labelled (cells[3], 2, "View", viewBox);
}
