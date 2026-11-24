#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "LayeredSampleEngine.h"

#include <array>
#include <atomic>

namespace ids
{
    inline constexpr const char* view      = "view";
    inline constexpr const char* capture   = "capture";    // Tempo / Live / Scroll
    inline constexpr const char* bars      = "bars";       // tempo window
    inline constexpr const char* timeMs    = "timeMs";     // free window
    inline constexpr const char* input     = "input";      // Peak / L / R / M / S
    inline constexpr const char* gain      = "gain";       // display gain, dB
    inline constexpr const char* rangeLo   = "rangeLo";
    inline constexpr const char* rangeHi   = "rangeHi";
    inline constexpr const char* listen    = "listen";
    inline constexpr const char* meter     = "meter";      // Peak / RMS / Crest
    inline constexpr const char* layout    = "layout";     // Overlay / Split (multi-signal)
    inline constexpr const char* showSig[6] = { "show1", "show2", "show3", "show4", "show5", "show6" };
    inline constexpr const char* freeze    = "freeze";
    inline constexpr const char* hold      = "hold";
    inline constexpr const char* triggerMode = "triggerMode";
    inline constexpr const char* threshold   = "threshold";
    inline constexpr const char* preRollMs   = "preRollMs";
    inline juce::String layerGain (int n)    { return "layerGain" + juce::String (n + 1); }
    inline juce::String layerPan (int n)     { return "layerPan" + juce::String (n + 1); }
    inline juce::String layerPitch (int n)   { return "layerPitch" + juce::String (n + 1); }
    inline juce::String layerDelay (int n)   { return "layerDelay" + juce::String (n + 1); }
    inline juce::String layerHighPass (int n) { return "layerHighPass" + juce::String (n + 1); }
    inline juce::String layerLowPass (int n) { return "layerLowPass" + juce::String (n + 1); }
    inline juce::String layerAttack (int n)  { return "layerAttack" + juce::String (n + 1); }
    inline juce::String layerDecay (int n)   { return "layerDecay" + juce::String (n + 1); }
    inline juce::String layerHold (int n)    { return "layerHold" + juce::String (n + 1); }
    inline juce::String layerSustain (int n) { return "layerSustain" + juce::String (n + 1); }
    inline juce::String layerRelease (int n) { return "layerRelease" + juce::String (n + 1); }
    inline juce::String layerAttackCurve (int n) { return "layerAttackCurve" + juce::String (n + 1); }
    inline juce::String layerDecayCurve (int n) { return "layerDecayCurve" + juce::String (n + 1); }
    inline juce::String layerReleaseCurve (int n) { return "layerReleaseCurve" + juce::String (n + 1); }
    inline juce::String layerAttackTension (int n) { return "layerAttackTension" + juce::String (n + 1); }
    inline juce::String layerDecayTension (int n) { return "layerDecayTension" + juce::String (n + 1); }
    inline juce::String layerReleaseTension (int n) { return "layerReleaseTension" + juce::String (n + 1); }
    inline juce::String layerEnvEnabled (int n) { return "layerEnvEnabled" + juce::String (n + 1); }
    inline juce::String layerMute (int n)    { return "layerMute" + juce::String (n + 1); }
    inline juce::String layerSolo (int n)    { return "layerSolo" + juce::String (n + 1); }
    inline juce::String layerReverse (int n) { return "layerReverse" + juce::String (n + 1); }
    inline juce::String layerPogo (int n)    { return "layerPogo" + juce::String (n + 1); }
    inline juce::String layerTrimStart (int n) { return "layerTrimStart" + juce::String (n + 1); }
    inline juce::String layerTrimEnd (int n) { return "layerTrimEnd" + juce::String (n + 1); }
    inline juce::String layerFadeIn (int n)  { return "layerFadeIn" + juce::String (n + 1); }
    inline juce::String layerFadeOut (int n) { return "layerFadeOut" + juce::String (n + 1); }
}

enum class CaptureMode { Tempo = 0, Live, Scroll };
enum class InputMode   { Peak = 0, L, R, M, S };

/** Window length in bars for the tempo-synced modes. */
inline double barsFromIndex (int index)
{
    static constexpr double table[] = { 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0, 4.0 };
    return table[juce::jlimit (0, 6, index)];
}

//==============================================================================
class WaveLensProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kCols = 1024;
    static constexpr int kMaxSignals = 6;
    static constexpr int kFFTOrder = 12, kFFTSize = 1 << kFFTOrder, kSpectrumBins = 512, kSpectrumHop = 1024;
    enum Chan { Sig1 = 0, Sig2, Sig3, Sig4, Sig5, Sig6, kNumChan };

    WaveLensProcessor();
    ~WaveLensProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "WaveLens"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return layerEngine.getTailLengthSeconds(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    void requestReset() noexcept { resetRequest.store (true); }
    void requestTrigger() noexcept { layerEngine.requestTrigger(); }
    void collectSampleGarbage() { layerEngine.collectRetiredSamples(); }
    bool loadSampleFile (int layer, const juce::File& file, juce::String& error);
    void clearSample (int layer);
    juce::String getSampleName (int layer) const;
    LayeredSampleEngine::WaveformPreview getWaveformPreview (int layer) const noexcept;
    void requestPreview (int layer) noexcept;

    juce::AudioProcessorValueTreeState apvts;

    using AF = std::atomic<float>;
    std::array<AF, kCols * kNumChan> colMin, colMax;
    std::array<AF, kCols * kNumChan> heldMin, heldMax;
    std::array<std::array<AF, kSpectrumBins>, kMaxSignals> spectrumDb, heldSpectrumDb;

    std::atomic<int>  head { 0 };
    std::atomic<int>  passStartCol { 0 };
    std::atomic<int>  filled { 0 };
    std::atomic<bool> hasData { false };
    std::atomic<int>  heldFrom { 0 }, heldTo { 0 };
    std::atomic<bool> heldValid { false };
    std::atomic<int>  connectedMask { 0 };

    std::atomic<float> peakNow { 0.0f }, rmsNow { 0.0f }, crestNow { 1.0f };
    std::atomic<float> peakHold { 0.0f }, rmsHold { 0.0f }, crestHold { 1.0f };

    static int idx (int ch, int col) noexcept { return ch * kCols + col; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses();

    void resetCapture (bool clearHeld);
    void startNewPass (bool copyToHeld, double startColumn);
    void copyPassToHeld();
    void onWrap();
    void initColumn (int col, const float* v) noexcept;
    void updateColumn (int col, const float* v) noexcept;
    void pushSample (const float* v) noexcept;
    void pushSpectrumSamples (const float* v) noexcept;
    void processSpectrumFrame() noexcept;

    CaptureMode mode = CaptureMode::Tempo;
    double colF = 0.0;
    double colsPerSample = 1.0;
    int    curCol = 0;
    int    passStart = 0;
    int    filledCount = 0;
    bool   needInit = true;
    bool   havePass = false;
    bool   tempoRunning = false;
    bool   wasFrozen = false;

    int    lastModeIdx = -1, lastBarsIdx = -1;
    float  lastTimeMs = -1.0f;

    double sr = 44100.0;
    juce::dsp::LinkwitzRileyFilter<float> rangeHp[kMaxSignals], rangeLp[kMaxSignals];
    juce::dsp::LinkwitzRileyFilter<float> listenHp[2], listenLp[2];
    juce::dsp::FFT spectrumFft { kFFTOrder };
    std::array<float, kFFTSize> fftWindow {};
    std::array<std::array<float, kFFTSize>, kMaxSignals> fftRing {};
    std::array<std::array<float, kFFTSize * 2>, kMaxSignals> fftScratch {};
    std::array<int, kMaxSignals> fftWritePos {};
    std::array<int, kSpectrumBins> spectrumBinLo {};
    std::array<float, kSpectrumBins> spectrumBinMix {};
    int fftSamplesSinceUpdate = 0, fftSamplesCollected = 0;
    float fftWindowSum = 1.0f;
    float  lastRangeLo = -1.0f, lastRangeHi = -1.0f;
    bool   lastRangeActive = false;

    float  peakEnv = 0.0f, meanSquare = 0.0f;
    float  peakDecay = 0.999f, msAlpha = 0.0001f;

    std::atomic<bool> resetRequest { false };

    std::atomic<float>* pView = nullptr;
    std::atomic<float>* pCapture = nullptr;
    std::atomic<float>* pBars = nullptr;
    std::atomic<float>* pTimeMs = nullptr;
    std::atomic<float>* pInput = nullptr;
    std::atomic<float>* pRangeLo = nullptr;
    std::atomic<float>* pRangeHi = nullptr;
    std::atomic<float>* pListen = nullptr;
    std::atomic<float>* pFreeze = nullptr;
    std::atomic<float>* pTriggerMode = nullptr;
    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pPreRollMs = nullptr;
    std::array<std::atomic<float>*, kMaxSignals> pLayerGain {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerPan {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerPitch {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerDelay {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerHighPass {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerLowPass {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerAttack {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerDecay {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerHold {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerSustain {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerRelease {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerAttackCurve {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerDecayCurve {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerReleaseCurve {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerAttackTension {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerDecayTension {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerReleaseTension {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerEnvEnabled {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerMute {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerSolo {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerReverse {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerPogo {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerTrimStart {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerTrimEnd {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerFadeIn {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerFadeOut {};

    LayeredSampleEngine layerEngine;
    juce::AudioBuffer<float> triggerBuffer;
    juce::MidiBuffer sliceMidiBuffer;
    std::array<juce::String, kMaxSignals> samplePaths;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveLensProcessor)
};