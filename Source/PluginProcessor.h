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
    inline juce::String layerGain (int n)    { return "layerGain" + juce::String (n + 1); }
    inline juce::String layerPan (int n)     { return "layerPan" + juce::String (n + 1); }
    inline juce::String layerPitch (int n)   { return "layerPitch" + juce::String (n + 1); }
    inline juce::String layerAttack (int n)  { return "layerAttack" + juce::String (n + 1); }
    inline juce::String layerDecay (int n)   { return "layerDecay" + juce::String (n + 1); }
    inline juce::String layerSustain (int n) { return "layerSustain" + juce::String (n + 1); }
    inline juce::String layerRelease (int n) { return "layerRelease" + juce::String (n + 1); }
    inline juce::String layerAttackCurve (int n) { return "layerAttackCurve" + juce::String (n + 1); }
    inline juce::String layerDecayCurve (int n) { return "layerDecayCurve" + juce::String (n + 1); }
    inline juce::String layerReleaseCurve (int n) { return "layerReleaseCurve" + juce::String (n + 1); }
    inline juce::String layerMute (int n)    { return "layerMute" + juce::String (n + 1); }
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
    // Number of display columns and the six sample layers captured per column.
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
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "WaveLens"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    /** Asks the audio thread to clear the capture, the held pass and the meter holds. */
    void requestReset() noexcept { resetRequest.store (true); }
    bool loadSampleFile (int layer, const juce::File& file, juce::String& error);
    void clearSample (int layer);
    juce::String getSampleName (int layer) const;

    juce::AudioProcessorValueTreeState apvts;

    //==========================================================================
    // Data shared with the editor. Written by the audio thread, read by the UI.
    // Tearing between a min and a max is harmless for a display.
    using AF = std::atomic<float>;
    std::array<AF, kCols * kNumChan> colMin, colMax;     // current pass
    std::array<AF, kCols * kNumChan> heldMin, heldMax;   // previous pass
    std::array<std::array<AF, kSpectrumBins>, kMaxSignals> spectrumDb, heldSpectrumDb;

    std::atomic<int>  head { 0 };          // column being written
    std::atomic<int>  passStartCol { 0 };  // first valid column of the current pass
    std::atomic<int>  filled { 0 };        // columns written since reset (scroll mode)
    std::atomic<bool> hasData { false };
    std::atomic<int>  heldFrom { 0 }, heldTo { 0 };
    std::atomic<bool> heldValid { false };
    std::atomic<int>  connectedMask { 0 };   // bit n set = sample layer n+1 is loaded

    // Meter values (linear).
    std::atomic<float> peakNow { 0.0f }, rmsNow { 0.0f }, crestNow { 1.0f };
    std::atomic<float> peakHold { 0.0f }, rmsHold { 0.0f }, crestHold { 1.0f };

    static int idx (int ch, int col) noexcept { return ch * kCols + col; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses();

    // Capture engine (audio thread only) --------------------------------------
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
    double colF = 0.0;          // fractional column position
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

    // DSP ---------------------------------------------------------------------
    double sr = 44100.0;
    juce::dsp::LinkwitzRileyFilter<float> rangeHp[kMaxSignals], rangeLp[kMaxSignals];   // one pair per signal
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

    // Cached parameter pointers.
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
    std::array<std::atomic<float>*, kMaxSignals> pLayerGain {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerPan {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerPitch {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerAttack {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerDecay {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerSustain {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerRelease {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerAttackCurve {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerDecayCurve {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerReleaseCurve {};
    std::array<std::atomic<float>*, kMaxSignals> pLayerMute {};

    // Six immutable sample layers and a separate copy of the trigger input.
    LayeredSampleEngine layerEngine;
    juce::AudioBuffer<float> triggerBuffer;
    std::array<juce::String, kMaxSignals> samplePaths;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveLensProcessor)
};
