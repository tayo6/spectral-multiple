#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr auto relaxed = std::memory_order_relaxed;

    inline float pickSample (float l, float r, InputMode m) noexcept
    {
        switch (m)
        {
            case InputMode::Peak: return std::abs (l) >= std::abs (r) ? l : r;   // louder channel, polarity kept
            case InputMode::L:    return l;
            case InputMode::R:    return r;
            case InputMode::M:    return 0.5f * (l + r);
            case InputMode::S:    return 0.5f * (l - r);
        }
        return l;
    }

    void copyRangeToHeld (WaveLensProcessor& p, int from, int to)
    {
        from = juce::jlimit (0, WaveLensProcessor::kCols - 1, from);
        to   = juce::jlimit (0, WaveLensProcessor::kCols - 1, to);
        if (to < from)
            return;

        for (int ch = 0; ch < WaveLensProcessor::kNumChan; ++ch)
            for (int c = from; c <= to; ++c)
            {
                const int i = WaveLensProcessor::idx (ch, c);
                p.heldMin[(size_t) i].store (p.colMin[(size_t) i].load (relaxed), relaxed);
                p.heldMax[(size_t) i].store (p.colMax[(size_t) i].load (relaxed), relaxed);
            }

        p.heldFrom.store (from);
        p.heldTo.store (to);
        p.heldValid.store (true);
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout WaveLensProcessor::createLayout()
{
    using namespace juce;
    using PID = ParameterID;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (PID { ids::view, 1 }, "Display", StringArray { "Waveform", "Spectrum" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (PID { ids::capture, 1 },  "Capture",
                                                        StringArray { "Tempo", "Live", "Scroll" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (PID { ids::bars, 1 },     "Window (bars)",
                                                        StringArray { "1/16", "1/8", "1/4", "1/2", "1", "2", "4" }, 4));

    NormalisableRange<float> timeRange (1.0f, 5000.0f);
    timeRange.setSkewForCentre (200.0f);
    layout.add (std::make_unique<AudioParameterFloat> (PID { ids::timeMs, 1 }, "Window (ms)", timeRange, 200.0f,
                                                       AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterChoice> (PID { ids::input, 1 },    "Input",
                                                        StringArray { "Peak", "L", "R", "M", "S" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (PID { ids::gain, 1 }, "Gain",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));

    NormalisableRange<float> freqRange (20.0f, 20000.0f);
    freqRange.setSkewForCentre (1000.0f);
    layout.add (std::make_unique<AudioParameterFloat> (PID { ids::rangeLo, 1 }, "Range low",  freqRange, 20.0f,
                                                       AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<AudioParameterFloat> (PID { ids::rangeHi, 1 }, "Range high", freqRange, 20000.0f,
                                                       AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterBool> (PID { ids::listen, 1 },   "Listen", false));
    layout.add (std::make_unique<AudioParameterChoice> (PID { ids::meter, 1 },  "Meter",
                                                        StringArray { "Peak", "RMS", "Crest" }, 0));

    layout.add (std::make_unique<AudioParameterChoice> (PID { ids::layout, 1 }, "Signal layout",
                                                        StringArray { "Overlay", "Split" }, 0));

    for (int i = 0; i < kMaxSignals; ++i)
        layout.add (std::make_unique<AudioParameterBool> (PID { ids::showSig[i], 1 },
                                                          "Show signal " + String (i + 1), true));

    layout.add (std::make_unique<AudioParameterBool> (PID { ids::freeze, 1 },   "Freeze", false));
    layout.add (std::make_unique<AudioParameterBool> (PID { ids::hold, 1 },     "Hold", false));

    return layout;
}

//==============================================================================
juce::AudioProcessor::BusesProperties WaveLensProcessor::makeBuses()
{
    // Signal 1 is the main in/out. Signals 2-6 are optional extra inputs (sidechain-style
    // buses) that the host routes to; they are analysed but never written to the output.
    auto props = BusesProperties()
                     .withInput  ("Signal 1", juce::AudioChannelSet::stereo(), true)
                     .withOutput ("Output",   juce::AudioChannelSet::stereo(), true);

    for (int i = 2; i <= kMaxSignals; ++i)
        props = props.withInput ("Signal " + juce::String (i), juce::AudioChannelSet::stereo(), false);

    return props;
}

WaveLensProcessor::WaveLensProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto* arr : { &colMin, &colMax, &heldMin, &heldMax })
        for (auto& a : *arr)
            a.store (0.0f, relaxed);

    for (auto& a : spectrumDb) for (auto& b : a) b.store (-120.0f, relaxed);
    for (auto& a : heldSpectrumDb) for (auto& b : a) b.store (-120.0f, relaxed);
    pView = apvts.getRawParameterValue (ids::view);
    pCapture  = apvts.getRawParameterValue (ids::capture);
    pBars     = apvts.getRawParameterValue (ids::bars);
    pTimeMs   = apvts.getRawParameterValue (ids::timeMs);
    pInput    = apvts.getRawParameterValue (ids::input);
    pRangeLo  = apvts.getRawParameterValue (ids::rangeLo);
    pRangeHi  = apvts.getRawParameterValue (ids::rangeHi);
    pListen   = apvts.getRawParameterValue (ids::listen);
    pFreeze   = apvts.getRawParameterValue (ids::freeze);
}

bool WaveLensProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    for (int b = 1; b < layouts.inputBuses.size(); ++b)
    {
        const auto extra = layouts.getChannelSet (true, b);
        if (! extra.isDisabled() && extra != juce::AudioChannelSet::mono() && extra != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void WaveLensProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 1 };

    for (auto* f : { &listenHp[0], &listenHp[1], &listenLp[0], &listenLp[1] })
    {
        f->prepare (spec);
        f->reset();
    }

    for (int s = 0; s < kMaxSignals; ++s)
    {
        rangeHp[s].prepare (spec);
        rangeHp[s].reset();
        rangeLp[s].prepare (spec);
        rangeLp[s].reset();
    }

    lastRangeLo = lastRangeHi = -1.0f;
    lastRangeActive = false;
    fftWindowSum = 0.0f;
    for (int n = 0; n < kFFTSize; ++n) { fftWindow[(size_t)n] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float)n / (float)(kFFTSize - 1)); fftWindowSum += fftWindow[(size_t)n]; }
    for (int b = 0; b < kSpectrumBins; ++b) { const float f = 20.0f * std::pow (1000.0f, (float)b / (float)(kSpectrumBins - 1)); const float x = juce::jlimit (0.0f, (float)(kFFTSize / 2 - 1), f * (float)sampleRate / (float)sampleRate * (float)kFFTSize / (float)sampleRate); spectrumBinLo[(size_t)b] = (int)std::floor(x); spectrumBinMix[(size_t)b] = x - (float)spectrumBinLo[(size_t)b]; }
    fftWritePos.fill (0); fftSamplesSinceUpdate = fftSamplesCollected = 0;

    peakDecay = (float) std::exp (-1.0 / (0.35 * sampleRate));
    msAlpha   = (float) (1.0 - std::exp (-1.0 / (0.30 * sampleRate)));
    peakEnv = meanSquare = 0.0f;

    lastModeIdx = -1;
    tempoRunning = false;
    wasFrozen = false;
    resetCapture (true);
}

//==============================================================================
// Capture engine
//==============================================================================
void WaveLensProcessor::resetCapture (bool clearHeld)
{
    needInit = true;
    havePass = false;
    filledCount = 0;
    colF = 0.0;
    curCol = 0;
    passStart = 0;

    head.store (0);
    passStartCol.store (0);
    filled.store (0);
    hasData.store (false);
    if (clearHeld) { heldValid.store (false); fftSamplesSinceUpdate = fftSamplesCollected = 0; fftWritePos.fill (0); for (auto& a : spectrumDb) for (auto& b : a) b.store (-120.0f, relaxed); for (auto& a : heldSpectrumDb) for (auto& b : a) b.store (-120.0f, relaxed); }
}

void WaveLensProcessor::copyPassToHeld()
{
    if (mode == CaptureMode::Scroll || ! havePass)
        return;

    if (curCol - passStart >= 8)           // ignore tiny fragments from stop/start
        copyRangeToHeld (*this, passStart, curCol);
}

void WaveLensProcessor::pushSpectrumSamples (const float* v) noexcept
{
    for (int s = 0; s < kMaxSignals; ++s) { fftRing[(size_t)s][(size_t)fftWritePos[(size_t)s]] = v[s]; fftWritePos[(size_t)s] = (fftWritePos[(size_t)s] + 1) % kFFTSize; }
    fftSamplesCollected = juce::jmin (kFFTSize, fftSamplesCollected + 1);
    if (++fftSamplesSinceUpdate >= kSpectrumHop) { fftSamplesSinceUpdate = 0; if (fftSamplesCollected == kFFTSize) processSpectrumFrame(); }
}

void WaveLensProcessor::processSpectrumFrame() noexcept
{
    const float scale = 2.0f / juce::jmax (fftWindowSum, 1.0f);
    for (int s = 0; s < kMaxSignals; ++s) {
        auto& d = fftScratch[(size_t)s]; const int oldest = fftWritePos[(size_t)s];
        for (int n = 0; n < kFFTSize; ++n) { d[(size_t)n] = fftRing[(size_t)s][(size_t)((oldest+n)%kFFTSize)] * fftWindow[(size_t)n]; d[(size_t)(kFFTSize+n)] = 0.0f; }
        spectrumFft.performFrequencyOnlyForwardTransform (d.data(), true);
        for (int b = 0; b < kSpectrumBins; ++b) { const int lo = spectrumBinLo[(size_t)b]; const float m = spectrumBinMix[(size_t)b]; const float mag = (d[(size_t)lo]*(1.0f-m)+d[(size_t)(lo+1)]*m)*scale; const float db = juce::Decibels::gainToDecibels (mag, -120.0f); const float old = spectrumDb[(size_t)s][(size_t)b].load(relaxed); spectrumDb[(size_t)s][(size_t)b].store (old + (db-old)*(db>old?0.68f:0.20f), relaxed); }
    }
}

void WaveLensProcessor::startNewPass (bool copyToHeld, double startColumn)
{
    if (copyToHeld)
        copyPassToHeld();

    needInit = true;
    havePass = false;
    filledCount = 0;

    colF = juce::jlimit (0.0, (double) kCols - 0.001, startColumn);
    curCol = (int) colF;
    passStart = curCol;

    head.store (curCol);
    passStartCol.store (passStart);
    filled.store (0);
    hasData.store (false);
}

void WaveLensProcessor::onWrap()
{
    if (mode != CaptureMode::Scroll && havePass)
        copyRangeToHeld (*this, passStart, kCols - 1);

    passStart = 0;
    passStartCol.store (0);
}

void WaveLensProcessor::initColumn (int col, const float* v) noexcept
{
    for (int ch = 0; ch < kNumChan; ++ch)
    {
        const auto i = (size_t) idx (ch, col);
        colMin[i].store (v[ch], relaxed);
        colMax[i].store (v[ch], relaxed);
    }
}

void WaveLensProcessor::updateColumn (int col, const float* v) noexcept
{
    for (int ch = 0; ch < kNumChan; ++ch)
    {
        const auto i = (size_t) idx (ch, col);
        if (v[ch] < colMin[i].load (relaxed)) colMin[i].store (v[ch], relaxed);
        if (v[ch] > colMax[i].load (relaxed)) colMax[i].store (v[ch], relaxed);
    }
}

void WaveLensProcessor::pushSample (const float* v) noexcept
{
    if (needInit)
    {
        initColumn (curCol, v);
        needInit = false;
        havePass = true;
        filledCount = 1;
        filled.store (1);
        hasData.store (true);
        head.store (curCol);
        return;
    }

    colF += colsPerSample;
    int target = (int) colF;

    if (target == curCol)
    {
        updateColumn (curCol, v);
        return;
    }

    // One or more new columns were entered. A sample shorter than a column is
    // held across all columns it covers, so very short windows stay continuous.
    for (int c = curCol + 1; c <= target; ++c)
    {
        int cc = c;
        if (cc >= kCols)
        {
            if (cc == kCols)
                onWrap();
            cc -= kCols;
        }

        initColumn (cc, v);
        if (filledCount < kCols)
            ++filledCount;
    }

    if (target >= kCols)
    {
        target -= kCols;
        colF   -= kCols;
    }

    curCol = target;
    head.store (curCol);
    filled.store (filledCount);
}

//==============================================================================
void WaveLensProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    auto mainBus = getBusBuffer (buffer, true, 0);
    const int mainCh = mainBus.getNumChannels();

    if (mainCh == 0 || numSamples == 0)
        return;

    float* chL = mainBus.getWritePointer (0);
    float* chR = mainCh > 1 ? mainBus.getWritePointer (1) : chL;

    // Signal sources: signal 1 is the main bus, signals 2-6 are the optional extra input buses.
    struct Source { const float* l = nullptr; const float* r = nullptr; };
    Source src[kMaxSignals];
    src[0] = { chL, chR };
    int mask = 1;

    for (int s = 1; s < kMaxSignals && s < getBusCount (true); ++s)
    {
        auto* bus = getBus (true, s);
        if (bus == nullptr || ! bus->isEnabled())
            continue;

        auto extra = getBusBuffer (buffer, true, s);
        if (extra.getNumChannels() > 0)
        {
            src[s] = { extra.getReadPointer (0), extra.getReadPointer (extra.getNumChannels() > 1 ? 1 : 0) };
            mask |= 1 << s;
        }
    }
    connectedMask.store (mask, relaxed);

    // Parameters ---------------------------------------------------------------
    const auto inputMode = (InputMode) juce::jlimit (0, 4, juce::roundToInt (pInput->load()));
    const int  modeIdx   = juce::jlimit (0, 2, juce::roundToInt (pCapture->load()));
    const int  barsIdx   = juce::jlimit (0, 6, juce::roundToInt (pBars->load()));
    const float timeMs   = pTimeMs->load();
    const bool frozen    = pFreeze->load() > 0.5f;
    const bool listen    = pListen->load() > 0.5f;

    float rangeLo = pRangeLo->load();
    float rangeHi = pRangeHi->load();
    rangeHi = juce::jmax (rangeHi, rangeLo * 1.05f);
    rangeHi = juce::jmin (rangeHi, 20000.0f, (float) (0.49 * sr));
    rangeLo = juce::jmin (rangeLo, rangeHi * 0.95f);

    const bool loOn = rangeLo > 20.5f;
    const bool hiOn = rangeHi < 19990.0f;
    const bool rangeActive = loOn || hiOn;

    if (rangeLo != lastRangeLo)
    {
        for (auto& f : rangeHp)
            f.setCutoffFrequency (rangeLo);
        for (auto* f : { &listenHp[0], &listenHp[1] })
            f->setCutoffFrequency (rangeLo);
        lastRangeLo = rangeLo;
    }
    if (rangeHi != lastRangeHi)
    {
        for (auto& f : rangeLp)
            f.setCutoffFrequency (rangeHi);
        for (auto* f : { &listenLp[0], &listenLp[1] })
            f->setCutoffFrequency (rangeHi);
        lastRangeHi = rangeHi;
    }
    if (rangeActive != lastRangeActive)
    {
        for (auto& f : rangeHp) f.reset();
        for (auto& f : rangeLp) f.reset();
        for (auto* f : { &listenHp[0], &listenHp[1], &listenLp[0], &listenLp[1] })
            f->reset();
        lastRangeActive = rangeActive;
    }

    // Capture configuration ------------------------------------------------------
    mode = (CaptureMode) modeIdx;
    {
        const bool tempoMode = mode == CaptureMode::Tempo;
        const bool changed = modeIdx != lastModeIdx
                             || (tempoMode ? barsIdx != lastBarsIdx : timeMs != lastTimeMs);
        lastModeIdx = modeIdx;
        lastBarsIdx = barsIdx;
        lastTimeMs  = timeMs;

        if (changed)
        {
            resetCapture (true);
            tempoRunning = false;
        }
    }

    if (resetRequest.exchange (false))
    {
        resetCapture (true);
        tempoRunning = false;
        peakHold.store (0.0f);
        rmsHold.store (0.0f);
        crestHold.store (1.0f);
    }

    if (frozen)
    {
        wasFrozen = true;
        tempoRunning = false;
    }
    else if (wasFrozen)
    {
        wasFrozen = false;
        if (mode != CaptureMode::Tempo)
            startNewPass (true, 0.0);          // tempo mode restarts through tempoRunning
    }

    bool capturing = false;

    if (! frozen)
    {
        if (mode != CaptureMode::Tempo)
        {
            capturing = true;
            const double windowSamples = juce::jmax (1.0, (double) timeMs * 0.001 * sr);
            colsPerSample = juce::jlimit (1.0e-6, kCols * 0.5, kCols / windowSamples);
        }
        else
        {
            bool ok = false;

            if (auto* ph = getPlayHead())
            {
                if (auto pos = ph->getPosition())
                {
                    const auto bpm = pos->getBpm();
                    const auto ppq = pos->getPpqPosition();

                    if (pos->getIsPlaying() && bpm.hasValue() && ppq.hasValue() && *bpm > 0.0 && *ppq >= 0.0)
                    {
                        const auto ts = pos->getTimeSignature();
                        const double barQuarters = (ts.hasValue() && ts->denominator > 0)
                                                       ? ts->numerator * 4.0 / ts->denominator : 4.0;
                        const double windowQuarters = barsFromIndex (barsIdx) * barQuarters;
                        const double startCol = std::fmod (*ppq, windowQuarters) / windowQuarters * kCols;

                        colsPerSample = juce::jlimit (1.0e-6, kCols * 0.5,
                                                      kCols * (*bpm / 60.0 / sr) / windowQuarters);
                        ok = true;
                        capturing = true;

                        if (! tempoRunning)
                        {
                            startNewPass (true, startCol);
                            tempoRunning = true;
                        }
                        else
                        {
                            double diff = startCol - colF;
                            if (diff < -kCols * 0.5) diff += kCols;
                            else if (diff > kCols * 0.5) diff -= kCols;

                            if (std::abs (diff) <= 4.0)
                                colF = juce::jmax (colF + diff, (double) curCol);   // follow the host clock
                            else
                                startNewPass (true, startCol);                      // seek or loop jump
                        }
                    }
                }
            }

            if (! ok)
                tempoRunning = false;
        }
    }

    // Per-sample work ------------------------------------------------------------
    const bool listenActive = listen && rangeActive;
    float v[kNumChan] {};
    float blockPeakMax = peakHold.load (relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = chL[i];
        const float r = chR[i];

        float lo_ = 0.0f, hi_ = 0.0f;

        // Signal 1: selected input, then the visible frequency range.
        float x = pickSample (l, r, inputMode);
        if (loOn) { rangeHp[0].processSample (0, x, lo_, hi_); x = hi_; }
        if (hiOn) { rangeLp[0].processSample (0, x, lo_, hi_); x = lo_; }

        // Meter (signal 1, before display gain, after range).
        peakEnv = juce::jmax (std::abs (x), peakEnv * peakDecay);
        meanSquare += msAlpha * (x * x - meanSquare);

        if (capturing)
        {
            v[Sig1] = x;

            // Signals 2-6 go through the same input mode and range as signal 1.
            for (int s = 1; s < kMaxSignals; ++s)
            {
                float y = 0.0f;
                if (src[s].l != nullptr)
                {
                    y = pickSample (src[s].l[i], src[s].r[i], inputMode);
                    if (loOn) { rangeHp[s].processSample (0, y, lo_, hi_); y = hi_; }
                    if (hiOn) { rangeLp[s].processSample (0, y, lo_, hi_); y = lo_; }
                }
                v[Sig1 + s] = y;
            }

            pushSpectrumSamples (v);
            pushSample (v);
        }

        // Band listening replaces the output; otherwise the audio is untouched.
        if (listenActive)
        {
            float a = l;
            if (loOn) { listenHp[0].processSample (0, a, lo_, hi_); a = hi_; }
            if (hiOn) { listenLp[0].processSample (0, a, lo_, hi_); a = lo_; }
            chL[i] = a;

            if (mainCh > 1)
            {
                float b = r;
                if (loOn) { listenHp[1].processSample (0, b, lo_, hi_); b = hi_; }
                if (hiOn) { listenLp[1].processSample (0, b, lo_, hi_); b = lo_; }
                chR[i] = b;
            }
        }
    }

    // Publish meter values ---------------------------------------------------------
    const float rms = std::sqrt (meanSquare);
    const float crest = juce::jlimit (1.0f, 1000.0f, peakEnv / juce::jmax (rms, 1.0e-6f));

    peakNow.store (peakEnv, relaxed);
    rmsNow.store (rms, relaxed);
    crestNow.store (crest, relaxed);

    if (peakEnv > blockPeakMax) peakHold.store (peakEnv, relaxed);
    if (rms > rmsHold.load (relaxed)) rmsHold.store (rms, relaxed);
    if (rms > 1.0e-4f && crest > crestHold.load (relaxed)) crestHold.store (crest, relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* WaveLensProcessor::createEditor()
{
    return new WaveLensEditor (*this);
}

void WaveLensProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void WaveLensProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WaveLensProcessor();
}
