#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

/**
    Six-layer transient sampler engine.

    Slots 0-2 are pre-sample layers. Their start is scheduled earlier than the
    delayed transient by the selected PRE lead.
    Slots 3-5 are main-sample layers. They start at the fixed 100 ms lookahead
    position, aligned with the latency-compensated dry input.

    Sample memory is immutable after loading. Replaced or cleared sample buffers are
    moved to a per-layer retired list tagged with the current audio-block epoch and
    reclaimed via collectRetiredSamples() once activeVoices reaches zero and the
    tagged audio block has completed.
*/
class LayeredSampleEngine final
{
public:
    static constexpr int kNumLayers = 6;
    static constexpr int kPreLayers = 3;
    // Fixed polyphony per slot: repeated hits can overlap without audio-thread allocation.
    static constexpr int kVoicesPerLayer = 4;
    static constexpr double kMaximumPreRollMilliseconds = 100.0;
    static constexpr double kDefaultPreRollMilliseconds = 50.0;
    static constexpr int kPreviewPoints = 2048;

    struct WaveformPreview
    {
        std::array<float, kPreviewPoints> minimum {};
        std::array<float, kPreviewPoints> maximum {};
        double durationMs = 0.0;
        bool loaded = false;
    };

    struct VoiceTailSnapshot
    {
        bool active = false;
        bool inRelease = false;
        double remainingSeconds = 0.0;
        double releaseRemainingSeconds = 0.0;
    };

    struct LayerParameters
    {
        float gain = 1.0f;
        float pan = 0.0f;
        float width = 1.0f; // 1.0 = original stereo, 0.0 = mono, 2.0 = widened
        float pitchSemitones = 0.0f;
        float delayShiftMs = 0.0f;
        float highPassHz = 20.0f;
        float lowPassHz = 20000.0f;
        float attackMs = 210.0f;
        float holdMs = 0.0f;
        float decayMs = 310.0f;
        float sustain = 0.76f;
        float releaseMs = 480.0f;
        int attackCurve = 2;   // 0..7 FL Studio modes (2 = Single curve)
        int decayCurve = 2;
        int releaseCurve = 2;
        float attackTension = 0.35f;
        float decayTension = -0.35f;
        float releaseTension = -0.45f;
        bool envEnabled = false;
        bool muted = false;
        bool solo = false;
        bool reverse = false;
        bool invertPolarity = false;
        float pogo = 0.0f;
        float trimStart = 0.0f;
        float trimEnd = 1.0f;
        float fadeInMs = 0.0f;
        float fadeOutMs = 0.0f;
    };

    struct EnvelopeSchedule
    {
        float attackEndMs = 210.0f;
        float holdEndMs = 210.0f;
        float decayEndMs = 520.0f;
        float releaseStartMs = 520.0f;
        float releaseMs = 480.0f;
    };

    static EnvelopeSchedule computeEnvelopeSchedule (const LayerParameters& p, float totalDurationMs) noexcept;
    static float envelopeLevelAtMs (const LayerParameters& p, const EnvelopeSchedule& sched, float ageMs) noexcept;
    static float evaluateCompleteEnvelopeAtMs (const LayerParameters& p, const EnvelopeSchedule& sched, float ageMs) noexcept;
    static float applyPowerTension (float u, float tension) noexcept;
    static float flSegmentLevel (float progress, float y0, float y1, int mode, float tension) noexcept;

    LayeredSampleEngine();
    ~LayeredSampleEngine();

    void prepareToPlay (double sampleRate, int maximumBlockSize);
    void setTriggerMode (int mode, float threshold) noexcept;
    void setPreRollMilliseconds (float milliseconds) noexcept;

    bool loadSampleFromFile (int layer, const juce::File& file, juce::String& error);
    void clearSample (int layer);
    juce::String getSampleName (int layer) const;
    WaveformPreview getWaveformPreview (int layer) const noexcept;
    void requestPreview (int layer) noexcept;
    void requestTrigger() noexcept;
    void collectRetiredSamples();
    int getLoadedMask() const noexcept;

    void setLayerParameters (int layer, const LayerParameters& parameters) noexcept;

    void processBlock (const juce::AudioBuffer<float>& triggerInput,
                       juce::AudioBuffer<float>& masterOutput,
                       const std::array<juce::AudioBuffer<float>*, kNumLayers>& layerOutputs,
                       const juce::MidiBuffer& midi,
                       bool manualTrigger = false) noexcept;

    void processBypassed() noexcept;

    int getMaximumDelaySamples() const noexcept { return maximumDelaySamples.load (std::memory_order_acquire); }
    int getPreRollSamples() const noexcept { return preRollSamples.load (std::memory_order_acquire); }
    double getSampleRate() const noexcept { return currentSampleRate.load (std::memory_order_acquire); }
    double getTailLengthSeconds() const noexcept;
    VoiceTailSnapshot getVoiceTailSnapshot (int layer, int voiceIndex) const noexcept;
    int getRetiredSampleCount (int layer);
    uint32_t getTriggerCount() const noexcept { return triggerCount.load (std::memory_order_relaxed); }
    const juce::AudioBuffer<float>& getLayerBuffer (int layer) const noexcept;

private:
    struct SampleData
    {
        juce::AudioBuffer<float> audio;
        double sourceSampleRate = 44100.0;
        juce::String displayName;
        WaveformPreview preview;
        std::atomic<int> activeVoices { 0 };
        uint64_t retiredAfterStartedBlock = 0;
    };

    struct Voice
    {
        SampleData* sample = nullptr;
        double position = 0.0;
        int delayRemaining = 0;
        int ageSamples = 0;
        float releaseStartLevel = 1.0f;
        bool inRelease = false;
        bool active = false;
    };

    struct VoiceTailState
    {
        std::atomic<bool> active { false };
        std::atomic<bool> inRelease { false };
        std::atomic<double> remainingSeconds { 0.0 };
        std::atomic<double> releaseRemainingSeconds { 0.0 };
    };

    struct AudioBlockEpochGuard;

    static void assignVoiceSample (Voice& voice, SampleData* nextSample) noexcept;
    static void resetVoice (Voice& voice) noexcept;
    void publishVoiceTailState (int layer, int voiceIndex, bool active, bool inRelease,
                                double remainingSeconds, double releaseRemainingSeconds) noexcept;
    void reclaimRetiredSamplesLocked();
    double computeActiveTailSeconds() noexcept;
    bool isLayerAudible (int layer) const noexcept;
    void triggerAllLayers() noexcept;
    void startLayer (int layer, bool ignoreTimingShift = false) noexcept;
    float renderLayerSample (int layer, float& left, float& right) noexcept;

    juce::AudioFormatManager formatManager;
    mutable juce::CriticalSection sampleStateLock;
    std::array<std::atomic<SampleData*>, kNumLayers> currentSamples {};
    std::array<std::atomic<bool>, kNumLayers> clearRequests {};
    std::array<std::atomic<bool>, kNumLayers> previewRequests {};
    std::array<std::unique_ptr<SampleData>, kNumLayers> activeSamples;
    std::array<std::vector<std::unique_ptr<SampleData>>, kNumLayers> retiredSamples;
    std::array<juce::String, kNumLayers> sampleNames;
    std::atomic<uint64_t> startedAudioBlocks { 0 };
    std::atomic<uint64_t> completedAudioBlocks { 0 };
    std::atomic<bool> triggerRequested { false };
    std::atomic<uint32_t> triggerCount { 0 };
    std::atomic<double> tailLengthSeconds { 0.0 };
    std::array<LayerParameters, kNumLayers> parameters {};
    std::array<std::array<Voice, kVoicesPerLayer>, kNumLayers> voices {};
    std::array<std::array<VoiceTailState, kVoicesPerLayer>, kNumLayers> voiceTailStates {};
    std::array<int, kNumLayers> nextVoiceToSteal {};
    std::array<juce::AudioBuffer<float>, kNumLayers> layerBuffers;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, kNumLayers> layerHighPass;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, kNumLayers> layerLowPass;
    std::array<float, kNumLayers> lastHighPassHz {};
    std::array<float, kNumLayers> lastLowPassHz {};
    std::array<bool, kNumLayers> layerHighPassActive {};
    std::array<bool, kNumLayers> layerLowPassActive {};
    std::array<bool, kNumLayers> layerWasAudible {};

    std::vector<float> delayLeft, delayRight;
    int delayWritePosition = 0;
    std::atomic<int> preRollSamples { 0 };
    std::atomic<int> maximumDelaySamples { 0 };
    int refractorySamples = 0;
    int refractoryRemaining = 0;

    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<double> currentPreRollMs { kDefaultPreRollMilliseconds };
    std::atomic<float> threshold { 0.03f };
    float previousInputLevel = 0.0f;
    std::atomic<int> triggerMode { 0 }; // 0 = Audio, 1 = MIDI, 2 = Both

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayeredSampleEngine)
};
