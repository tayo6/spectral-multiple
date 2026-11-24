#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr auto relaxed = std::memory_order_relaxed;

    inline float pickSample (float l, float r, InputMode m) noexcept
    {
        switch (m)
        {
            case InputMode::Peak: return std::abs (l) >= std::abs (r) ? l : r;
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

    layout.add (std::make_unique<AudioParameterChoice> (PID { ids::triggerMode, 1 },
                                                        "Trigger mode", StringArray { "Audio", "MIDI", "Both" }, 2));
    layout.add (std::make_unique<AudioParameterFloat> (PID { ids::threshold, 1 }, "Trigger threshold",
                                                       NormalisableRange<float> (0.001f, 1.0f, 0.001f), 0.03f));
    layout.add (std::make_unique<AudioParameterFloat> (PID { ids::preRollMs, 1 }, "PRE lead",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
                                                       AudioParameterFloatAttributes().withLabel ("ms")));

    for (int i = 0; i < kMaxSignals; ++i)
    {
        const auto suffix = String (i + 1);
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerGain (i), 1 }, "Layer " + suffix + " gain",
                                                           NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerPan (i), 1 }, "Layer " + suffix + " pan",
                                                           NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerPitch (i), 1 }, "Layer " + suffix + " pitch",
                                                           NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerDelay (i), 1 }, "Layer " + suffix + " timing shift",
                                                           NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerHighPass (i), 1 }, "Layer " + suffix + " high-pass",
                                                           NormalisableRange<float> (20.0f, 1000.0f, 1.0f, 0.35f), 20.0f,
                                                           AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerLowPass (i), 1 }, "Layer " + suffix + " low-pass",
                                                           NormalisableRange<float> (1000.0f, 20000.0f, 1.0f, 0.35f), 20000.0f,
                                                           AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerAttack (i), 1 }, "Layer " + suffix + " attack",
                                                           NormalisableRange<float> (0.1f, 2000.0f, 0.1f), 210.0f,
                                                           AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerHold (i), 1 }, "Layer " + suffix + " hold",
                                                           NormalisableRange<float> (0.0f, 2000.0f, 0.1f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerDecay (i), 1 }, "Layer " + suffix + " decay",
                                                           NormalisableRange<float> (0.1f, 5000.0f, 0.1f), 310.0f,
                                                           AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerSustain (i), 1 }, "Layer " + suffix + " sustain",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.76f));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerRelease (i), 1 }, "Layer " + suffix + " release",
                                                           NormalisableRange<float> (0.1f, 10000.0f, 0.1f), 480.0f,
                                                           AudioParameterFloatAttributes().withLabel ("ms")));
        const StringArray curveNames { "Hold", "Smooth", "Single curve", "Half sine", "Stairs", "Smooth stairs", "Pulse", "Wave" };
        layout.add (std::make_unique<AudioParameterChoice> (PID { ids::layerAttackCurve (i), 1 },
                                                           "Layer " + suffix + " attack curve", curveNames, 2));
        layout.add (std::make_unique<AudioParameterChoice> (PID { ids::layerDecayCurve (i), 1 },
                                                           "Layer " + suffix + " decay curve", curveNames, 2));
        layout.add (std::make_unique<AudioParameterChoice> (PID { ids::layerReleaseCurve (i), 1 },
                                                           "Layer " + suffix + " release curve", curveNames, 2));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerAttackTension (i), 1 }, "Layer " + suffix + " attack tension",
                                                           NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.35f));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerDecayTension (i), 1 }, "Layer " + suffix + " decay tension",
                                                           NormalisableRange<float> (-1.0f, 1.0f, 0.01f), -0.35f));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerReleaseTension (i), 1 }, "Layer " + suffix + " release tension",
                                                           NormalisableRange<float> (-1.0f, 1.0f, 0.01f), -0.45f));
        layout.add (std::make_unique<AudioParameterBool> (PID { ids::layerEnvEnabled (i), 1 }, "Layer " + suffix + " env enabled", false));
        layout.add (std::make_unique<AudioParameterBool> (PID { ids::layerMute (i), 1 }, "Layer " + suffix + " mute", false));
        layout.add (std::make_unique<AudioParameterBool> (PID { ids::layerSolo (i), 1 }, "Layer " + suffix + " solo", false));
        layout.add (std::make_unique<AudioParameterBool> (PID { ids::layerReverse (i), 1 }, "Layer " + suffix + " reverse", false));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerPogo (i), 1 }, "Layer " + suffix + " pogo",
                                                           NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerTrimStart (i), 1 }, "Layer " + suffix + " front trim",
                                                           NormalisableRange<float> (0.0f, 0.99f, 0.001f), 0.0f));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerTrimEnd (i), 1 }, "Layer " + suffix + " back trim",
                                                           NormalisableRange<float> (0.005f, 1.0f, 0.001f), 1.0f));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerFadeIn (i), 1 }, "Layer " + suffix + " fade in",
                                                           NormalisableRange<float> (0.0f, 2000.0f, 1.0f, 0.4f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<AudioParameterFloat> (PID { ids::layerFadeOut (i), 1 }, "Layer " + suffix + " fade out",
                                                           NormalisableRange<float> (0.0f, 5000.0f, 1.0f, 0.4f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("ms")));
    }

    layout.add (std::make_unique<AudioParameterBool> (PID { ids::freeze, 1 },   "Freeze", false));
    layout.add (std::make_unique<AudioParameterBool> (PID { ids::hold, 1 },     "Hold", false));

    return layout;
}

//==============================================================================
juce::AudioProcessor::BusesProperties WaveLensProcessor::makeBuses()
{
    auto props = BusesProperties()
                     .withInput  ("Trigger Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput ("Master",       juce::AudioChannelSet::stereo(), true);

    static const char* names[kMaxSignals] = {
        "Pre 1", "Pre 2", "Pre 3", "Main 1", "Main 2", "Main 3"
    };

    for (int i = 0; i < kMaxSignals; ++i)
        props = props.withOutput (names[i], juce::AudioChannelSet::stereo(), false);

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
    pTriggerMode = apvts.getRawParameterValue (ids::triggerMode);
    pThreshold = apvts.getRawParameterValue (ids::threshold);
    pPreRollMs = apvts.getRawParameterValue (ids::preRollMs);

    for (int i = 0; i < kMaxSignals; ++i)
    {
        pLayerGain[(size_t) i] = apvts.getRawParameterValue (ids::layerGain (i));
        pLayerPan[(size_t) i] = apvts.getRawParameterValue (ids::layerPan (i));
        pLayerPitch[(size_t) i] = apvts.getRawParameterValue (ids::layerPitch (i));
        pLayerDelay[(size_t) i] = apvts.getRawParameterValue (ids::layerDelay (i));
        pLayerHighPass[(size_t) i] = apvts.getRawParameterValue (ids::layerHighPass (i));
        pLayerLowPass[(size_t) i] = apvts.getRawParameterValue (ids::layerLowPass (i));
        pLayerAttack[(size_t) i] = apvts.getRawParameterValue (ids::layerAttack (i));
        pLayerDecay[(size_t) i] = apvts.getRawParameterValue (ids::layerDecay (i));
        pLayerHold[(size_t) i] = apvts.getRawParameterValue (ids::layerHold (i));
        pLayerSustain[(size_t) i] = apvts.getRawParameterValue (ids::layerSustain (i));
        pLayerRelease[(size_t) i] = apvts.getRawParameterValue (ids::layerRelease (i));
        pLayerAttackCurve[(size_t) i] = apvts.getRawParameterValue (ids::layerAttackCurve (i));
        pLayerDecayCurve[(size_t) i] = apvts.getRawParameterValue (ids::layerDecayCurve (i));
        pLayerReleaseCurve[(size_t) i] = apvts.getRawParameterValue (ids::layerReleaseCurve (i));
        pLayerAttackTension[(size_t) i] = apvts.getRawParameterValue (ids::layerAttackTension (i));
        pLayerDecayTension[(size_t) i] = apvts.getRawParameterValue (ids::layerDecayTension (i));
        pLayerReleaseTension[(size_t) i] = apvts.getRawParameterValue (ids::layerReleaseTension (i));
        pLayerEnvEnabled[(size_t) i] = apvts.getRawParameterValue (ids::layerEnvEnabled (i));
        pLayerMute[(size_t) i] = apvts.getRawParameterValue (ids::layerMute (i));
        pLayerSolo[(size_t) i] = apvts.getRawParameterValue (ids::layerSolo (i));
        pLayerReverse[(size_t) i] = apvts.getRawParameterValue (ids::layerReverse (i));
        pLayerPogo[(size_t) i] = apvts.getRawParameterValue (ids::layerPogo (i));
        pLayerTrimStart[(size_t) i] = apvts.getRawParameterValue (ids::layerTrimStart (i));
        pLayerTrimEnd[(size_t) i] = apvts.getRawParameterValue (ids::layerTrimEnd (i));
        pLayerFadeIn[(size_t) i] = apvts.getRawParameterValue (ids::layerFadeIn (i));
        pLayerFadeOut[(size_t) i] = apvts.getRawParameterValue (ids::layerFadeOut (i));
    }
}

bool WaveLensProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    for (int b = 1; b < layouts.outputBuses.size(); ++b)
    {
        const auto extra = layouts.getChannelSet (false, b);
        if (! extra.isDisabled() && extra != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void WaveLensProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    const int safeBlockSize = juce::jmax (1, samplesPerBlock);
    layerEngine.prepareToPlay (sampleRate, safeBlockSize);
    layerEngine.setPreRollMilliseconds (pPreRollMs->load (relaxed));
    triggerBuffer.setSize (2, safeBlockSize, false, true, false);
    sliceMidiBuffer.ensureSize (2048);
    setLatencySamples (juce::jmax (1, juce::roundToInt (sampleRate * LayeredSampleEngine::kMaximumPreRollMilliseconds * 0.001)));

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
    for (int b = 0; b < kSpectrumBins; ++b) { const float f = 20.0f * std::pow (1000.0f, (float)b / (float)(kSpectrumBins - 1)); const float x = juce::jlimit (0.0f, (float)(kFFTSize / 2 - 1), f * (float)kFFTSize / (float)sampleRate); spectrumBinLo[(size_t)b] = (int)std::floor(x); spectrumBinMix[(size_t)b] = x - (float)spectrumBinLo[(size_t)b]; }
    fftWritePos.fill (0); fftSamplesSinceUpdate = fftSamplesCollected = 0;

    peakDecay = (float) std::exp (-1.0 / (0.35 * sampleRate));
    msAlpha   = (float) (1.0 - std::exp (-1.0 / (0.30 * sampleRate)));
    peakEnv = meanSquare = 0.0f;

    lastModeIdx = -1;
    tempoRunning = false;
    wasFrozen = false;
    resetCapture (true);
}

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

    if (curCol - passStart >= 8)
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
void WaveLensProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    auto mainInput = getBusBuffer (buffer, true, 0);
    auto mainOutput = getBusBuffer (buffer, false, 0);
    const int inCh = mainInput.getNumChannels();
    const int outCh = mainOutput.getNumChannels();

    if (inCh > 0 && outCh > 0)
    {
        for (int ch = 0; ch < outCh; ++ch)
        {
            const int srcCh = juce::jmin (ch, inCh - 1);
            if (mainOutput.getWritePointer (ch) != mainInput.getReadPointer (srcCh))
                mainOutput.copyFrom (ch, 0, mainInput, srcCh, 0, numSamples);
        }
    }
    else
    {
        mainOutput.clear (0, numSamples);
    }

    for (int layer = 0; layer < kMaxSignals; ++layer)
    {
        const int outputBusIndex = layer + 1;
        if (outputBusIndex < getBusCount (false)
            && getChannelIndexInProcessBlockBuffer (false, outputBusIndex, 0) >= 0)
        {
            auto* bus = getBus (false, outputBusIndex);
            if (bus != nullptr && bus->isEnabled())
                getBusBuffer (buffer, false, outputBusIndex).clear (0, numSamples);
        }
    }
}

void WaveLensProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalSamples = buffer.getNumSamples();
    auto mainInput = getBusBuffer (buffer, true, 0);
    auto mainOutput = getBusBuffer (buffer, false, 0);
    const int inputChannels = mainInput.getNumChannels();

    if (inputChannels == 0 || totalSamples == 0)
        return;

    std::array<juce::AudioBuffer<float>, kMaxSignals> layerOutputBuffers;
    std::array<bool, kMaxSignals> layerOutputActive {};

    for (int layer = 0; layer < kMaxSignals; ++layer)
    {
        const int outputBusIndex = layer + 1;
        if (outputBusIndex < getBusCount (false)
            && getChannelIndexInProcessBlockBuffer (false, outputBusIndex, 0) >= 0)
        {
            auto* bus = getBus (false, outputBusIndex);
            if (bus != nullptr && bus->isEnabled())
            {
                layerOutputBuffers[(size_t) layer] = getBusBuffer (buffer, false, outputBusIndex);
                layerOutputActive[(size_t) layer] = layerOutputBuffers[(size_t) layer].getNumChannels() > 0;
            }
        }

        LayeredSampleEngine::LayerParameters params;
        params.gain = pLayerGain[(size_t) layer]->load (relaxed);
        params.pan = pLayerPan[(size_t) layer]->load (relaxed);
        params.pitchSemitones = pLayerPitch[(size_t) layer]->load (relaxed);
        params.delayShiftMs = pLayerDelay[(size_t) layer]->load (relaxed);
        params.highPassHz = pLayerHighPass[(size_t) layer]->load (relaxed);
        params.lowPassHz = pLayerLowPass[(size_t) layer]->load (relaxed);
        params.attackMs = pLayerAttack[(size_t) layer]->load (relaxed);
        params.holdMs = pLayerHold[(size_t) layer]->load (relaxed);
        params.decayMs = pLayerDecay[(size_t) layer]->load (relaxed);
        params.sustain = pLayerSustain[(size_t) layer]->load (relaxed);
        params.releaseMs = pLayerRelease[(size_t) layer]->load (relaxed);
        params.attackCurve = juce::roundToInt (pLayerAttackCurve[(size_t) layer]->load (relaxed));
        params.decayCurve = juce::roundToInt (pLayerDecayCurve[(size_t) layer]->load (relaxed));
        params.releaseCurve = juce::roundToInt (pLayerReleaseCurve[(size_t) layer]->load (relaxed));
        params.attackTension = pLayerAttackTension[(size_t) layer]->load (relaxed);
        params.decayTension = pLayerDecayTension[(size_t) layer]->load (relaxed);
        params.releaseTension = pLayerReleaseTension[(size_t) layer]->load (relaxed);
        params.envEnabled = pLayerEnvEnabled[(size_t) layer]->load (relaxed) > 0.5f;
        params.muted = pLayerMute[(size_t) layer]->load (relaxed) > 0.5f;
        params.solo = pLayerSolo[(size_t) layer]->load (relaxed) > 0.5f;
        params.reverse = pLayerReverse[(size_t) layer]->load (relaxed) > 0.5f;
        params.pogo = pLayerPogo[(size_t) layer]->load (relaxed);
        params.trimStart = pLayerTrimStart[(size_t) layer]->load (relaxed);
        params.trimEnd = pLayerTrimEnd[(size_t) layer]->load (relaxed);
        params.fadeInMs = pLayerFadeIn[(size_t) layer]->load (relaxed);
        params.fadeOutMs = pLayerFadeOut[(size_t) layer]->load (relaxed);
        layerEngine.setLayerParameters (layer, params);
    }

    layerEngine.setTriggerMode (juce::roundToInt (pTriggerMode->load (relaxed)),
                                pThreshold->load (relaxed));
    layerEngine.setPreRollMilliseconds (pPreRollMs->load (relaxed));

    const bool manualTrig = triggerAllRequest.exchange (false, std::memory_order_acq_rel);
    const int maxChunk = juce::jmax (1, triggerBuffer.getNumSamples());
    const int mainOutChannels = mainOutput.getNumChannels();

    for (int chunkStart = 0; chunkStart < totalSamples; chunkStart += maxChunk)
    {
        const int chunkSamples = juce::jmin (maxChunk, totalSamples - chunkStart);

        triggerBuffer.copyFrom (0, 0, mainInput, 0, chunkStart, chunkSamples);
        if (inputChannels > 1)
            triggerBuffer.copyFrom (1, 0, mainInput, 1, chunkStart, chunkSamples);
        else
            triggerBuffer.copyFrom (1, 0, mainInput, 0, chunkStart, chunkSamples);

        float* trigPtrs[2] = { triggerBuffer.getWritePointer (0), triggerBuffer.getWritePointer (1) };
        juce::AudioBuffer<float> triggerSlice (trigPtrs, 2, chunkSamples);

        float* mainOutPtrs[2] = {
            mainOutput.getWritePointer (0) + chunkStart,
            mainOutChannels > 1 ? (mainOutput.getWritePointer (1) + chunkStart) : nullptr
        };
        juce::AudioBuffer<float> mainOutputSlice (mainOutPtrs, juce::jmin (2, mainOutChannels), chunkSamples);

        std::array<juce::AudioBuffer<float>, kMaxSignals> sliceLayerBuffers;
        std::array<juce::AudioBuffer<float>*, kMaxSignals> sliceLayerPointers {};
        for (int layer = 0; layer < kMaxSignals; ++layer)
        {
            if (layerOutputActive[(size_t) layer])
            {
                auto& fullBus = layerOutputBuffers[(size_t) layer];
                const int busCh = juce::jmin (2, fullBus.getNumChannels());
                float* busPtrs[2] = {
                    fullBus.getWritePointer (0) + chunkStart,
                    busCh > 1 ? (fullBus.getWritePointer (1) + chunkStart) : nullptr
                };
                sliceLayerBuffers[(size_t) layer].setDataToReferTo (busPtrs, busCh, chunkSamples);
                sliceLayerPointers[(size_t) layer] = &sliceLayerBuffers[(size_t) layer];
            }
        }

        const juce::MidiBuffer* chunkMidi = &midi;
        if (totalSamples > maxChunk)
        {
            sliceMidiBuffer.clear();
            for (const auto metadata : midi)
            {
                const int pos = metadata.samplePosition;
                if (pos >= chunkStart && pos < chunkStart + chunkSamples)
                    sliceMidiBuffer.addEvent (metadata.getMessage(), pos - chunkStart);
            }
            chunkMidi = &sliceMidiBuffer;
        }

        layerEngine.processBlock (triggerSlice, mainOutputSlice, sliceLayerPointers, *chunkMidi,
                                  manualTrig && chunkStart == 0);
    }

    const int numSamples = totalSamples;
    const int mainChannels = mainOutput.getNumChannels();
    float* outL = mainOutput.getWritePointer (0);
    float* outR = mainChannels > 1 ? mainOutput.getWritePointer (1) : outL;
    connectedMask.store (layerEngine.getLoadedMask(), relaxed);

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
            startNewPass (true, 0.0);
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
                                colF = juce::jmax (colF + diff, (double) curCol);
                            else
                                startNewPass (true, startCol);
                        }
                    }
                }
            }

            if (! ok)
                tempoRunning = false;
        }
    }

    const bool listenActive = listen && rangeActive;
    float v[kNumChan] {};
    float blockPeakMax = peakHold.load (relaxed);

    const int lastChunkStart = ((numSamples - 1) / maxChunk) * maxChunk;

    for (int i = 0; i < numSamples; ++i)
    {
        const float inputL = mainInput.getSample (0, i);
        const float inputR = inputChannels > 1 ? mainInput.getSample (1, i) : inputL;

        const float inputLevel = pickSample (inputL, inputR, inputMode);
        peakEnv = juce::jmax (std::abs (inputLevel), peakEnv * peakDecay);
        meanSquare += msAlpha * (inputLevel * inputLevel - meanSquare);

        if (capturing)
        {
            const int sampleInLastChunk = i - lastChunkStart;
            for (int layer = 0; layer < kMaxSignals; ++layer)
            {
                float y = 0.0f;
                if (sampleInLastChunk >= 0 && sampleInLastChunk < layerEngine.getLayerBuffer (layer).getNumSamples())
                {
                    const auto& layerAudio = layerEngine.getLayerBuffer (layer);
                    y = pickSample (layerAudio.getSample (0, sampleInLastChunk),
                                    layerAudio.getSample (1, sampleInLastChunk), inputMode);
                }
                else if (layerOutputActive[(size_t) layer])
                {
                    const auto& busBuf = layerOutputBuffers[(size_t) layer];
                    const float bl = busBuf.getSample (0, i);
                    const float br = busBuf.getNumChannels() > 1 ? busBuf.getSample (1, i) : bl;
                    y = pickSample (bl, br, inputMode);
                }
                float lo_ = 0.0f, hi_ = 0.0f;
                if (loOn) { rangeHp[layer].processSample (0, y, lo_, hi_); y = hi_; }
                if (hiOn) { rangeLp[layer].processSample (0, y, lo_, hi_); y = lo_; }
                v[layer] = y;
            }

            pushSpectrumSamples (v);
            pushSample (v);
        }

        if (listenActive)
        {
            float lo_ = 0.0f, hi_ = 0.0f;
            float a = outL[i];
            if (loOn) { listenHp[0].processSample (0, a, lo_, hi_); a = hi_; }
            if (hiOn) { listenLp[0].processSample (0, a, lo_, hi_); a = lo_; }
            outL[i] = a;

            if (mainChannels > 1)
            {
                float b = outR[i];
                if (loOn) { listenHp[1].processSample (0, b, lo_, hi_); b = hi_; }
                if (hiOn) { listenLp[1].processSample (0, b, lo_, hi_); b = lo_; }
                outR[i] = b;
            }
        }
    }

    const float rms = std::sqrt (meanSquare);
    const float crest = juce::jlimit (1.0f, 1000.0f, peakEnv / juce::jmax (rms, 1.0e-6f));

    peakNow.store (peakEnv, relaxed);
    rmsNow.store (rms, relaxed);
    crestNow.store (crest, relaxed);

    if (peakEnv > blockPeakMax) peakHold.store (peakEnv, relaxed);
    if (rms > rmsHold.load (relaxed)) rmsHold.store (rms, relaxed);
    if (rms > 1.0e-4f && crest > crestHold.load (relaxed)) crestHold.store (crest, relaxed);
}

bool WaveLensProcessor::loadSampleFile (int layer, const juce::File& file, juce::String& error)
{
    if (! layerEngine.loadSampleFromFile (layer, file, error))
        return false;

    samplePaths[(size_t) layer] = file.getFullPathName();
    connectedMask.store (layerEngine.getLoadedMask(), std::memory_order_relaxed);
    return true;
}

void WaveLensProcessor::clearSample (int layer)
{
    if (! juce::isPositiveAndBelow (layer, kMaxSignals))
        return;

    layerEngine.clearSample (layer);
    samplePaths[(size_t) layer].clear();
    connectedMask.store (layerEngine.getLoadedMask(), std::memory_order_relaxed);
}

juce::String WaveLensProcessor::getSampleName (int layer) const
{
    return layerEngine.getSampleName (layer);
}

LayeredSampleEngine::WaveformPreview WaveLensProcessor::getWaveformPreview (int layer) const noexcept
{
    return layerEngine.getWaveformPreview (layer);
}

void WaveLensProcessor::requestPreview (int layer) noexcept
{
    layerEngine.requestPreview (layer);
}

juce::AudioProcessorEditor* WaveLensProcessor::createEditor()
{
    return new WaveLensEditor (*this);
}

void WaveLensProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
    {
        for (int i = 0; i < kMaxSignals; ++i)
            xml->setAttribute ("samplePath" + juce::String (i + 1), samplePaths[(size_t) i]);
        copyXmlToBinary (*xml, destData);
    }
}

void WaveLensProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            for (int i = 0; i < kMaxSignals; ++i)
            {
                const auto path = xml->getStringAttribute ("samplePath" + juce::String (i + 1));
                samplePaths[(size_t) i] = path;
                if (path.isNotEmpty())
                {
                    juce::String error;
                    if (! loadSampleFile (i, juce::File (path), error))
                        samplePaths[(size_t) i].clear();
                }
                else
                    layerEngine.clearSample (i);
            }

            layerEngine.setPreRollMilliseconds (pPreRollMs->load (relaxed));
            connectedMask.store (layerEngine.getLoadedMask(), std::memory_order_relaxed);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WaveLensProcessor();
}
