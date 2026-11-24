#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

/**
    Six-layer transient sampler engine.

    Slots 0-2 are pre-sample layers. Their start is scheduled earlier than the
    delayed transient by the selected PRE lead.
    Slots 3-5 are main-sample layers. They start at the fixed 100 ms lookahead
    position, aligned with the latency-compensated dry input.

    Sample memory is immutable after loading. Replaced sample buffers are retained
    until the processor is destroyed so the audio thread never observes freed memory.
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
    // Old sample versions are retained for safe audio-thread handover. Bound that history.
    static constexpr size_t kMaximumRetainedSampleBytes = 512u * 1024u * 1024u;

    struct WaveformPreview
    {
        std::array<float, kPreviewPoints> minimum {};
        std::array<float, kPreviewPoints> maximum {};
        double durationMs = 0.0;
        bool loaded = false;
    };

    struct LayerParameters
    {
        float gain = 1.0f;
        float pan = 0.0f;
        float pitchSemitones = 0.0f;
        float delayShiftMs = 0.0f;
        float highPassHz = 20.0f;
        float lowPassHz = 20000.0f;
        float attackMs = 2.0f;
        float holdMs = 20.0f;
        float decayMs = 140.0f;
        float sustain = 0.78f;
        float releaseMs = 200.0f;
        int attackCurve = 2;   // 0..7 FL Studio modes (2 = Single curve)
        int decayCurve = 2;
        int releaseCurve = 2;
        float attackTension = 0.25f;
        float decayTension = -0.35f;
        float releaseTension = -0.45f;
        bool envEnabled = true;
        bool muted = false;
        bool solo = false;
        bool reverse = false;
        float pogo = 0.0f;
        float trimStart = 0.0f;
        float trimEnd = 1.0f;
        float fadeInMs = 0.0f;
        float fadeOutMs = 0.0f;
    };

    LayeredSampleEngine();
    ~LayeredSampleEngine() = default;

    void prepareToPlay (double sampleRate, int maximumBlockSize);
    void setTriggerMode (int mode, float threshold) noexcept;
    void setPreRollMilliseconds (float milliseconds) noexcept;

    bool loadSampleFromFile (int layer, const juce::File& file, juce::String& error);
    void clearSample (int layer);
    juce::String getSampleName (int layer) const;
    WaveformPreview getWaveformPreview (int layer) const noexcept;
    void requestPreview (int layer) noexcept;
    int getLoadedMask() const noexcept;
    double getTailLengthSeconds() const noexcept { return maximumTailSeconds.load (std::memory_order_relaxed); }

    void setLayerParameters (int layer, const LayerParameters& parameters) noexcept;

    void processBlock (const juce::AudioBuffer<float>& triggerInput,
                       juce::AudioBuffer<float>& masterOutput,
                       const std::array<juce::AudioBuffer<float>*, kNumLayers>& layerOutputs,
                       const juce::MidiBuffer& midi) noexcept;

    const juce::AudioBuffer<float>& getLayerBuffer (int layer) const noexcept;

private:
    struct SampleData
    {
        juce::AudioBuffer<float> audio;
        double sourceSampleRate = 44100.0;
        juce::String displayName;
        WaveformPreview preview;
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

    bool isLayerAudible (int layer) const noexcept;
    void triggerAllLayers() noexcept;
    void startLayer (int layer, bool ignoreTimingShift = false) noexcept;
    float renderLayerSample (int layer, float& left, float& right) noexcept;
    float envelopeLevelAtAge (const LayerParameters& p, int age) const noexcept;
    void updateTailEstimate (int layer) noexcept;
    static float applyPowerTension (float u, float tension) noexcept;
    static float flSegmentLevel (float progress, float y0, float y1, int mode, float tension) noexcept;

    juce::AudioFormatManager formatManager;
    std::array<std::atomic<SampleData*>, kNumLayers> currentSamples {};
    std::array<std::atomic<bool>, kNumLayers> clearRequests {};
    std::array<std::atomic<bool>, kNumLayers> previewRequests {};
    std::array<std::vector<std::unique_ptr<SampleData>>, kNumLayers> ownedSamples;
    size_t retainedSampleBytes = 0;
    std::array<std::atomic<float>, kNumLayers> layerTailSeconds {};
    std::array<std::atomic<bool>, kNumLayers> tailEstimateDirty {};
    std::atomic<float> maximumTailSeconds { 0.0f };
    std::array<juce::String, kNumLayers> sampleNames;
    std::array<LayerParameters, kNumLayers> parameters {};
    std::array<std::array<Voice, kVoicesPerLayer>, kNumLayers> voices {};
    std::array<int, kNumLayers> nextVoiceToSteal {};
    std::array<juce::AudioBuffer<float>, kNumLayers> layerBuffers;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, kNumLayers> layerHighPass;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, kNumLayers> layerLowPass;

    std::vector<float> delayLeft, delayRight;
    int delayWritePosition = 0;
    int preRollSamples = 0;
    int maximumDelaySamples = 0;
    int refractorySamples = 0;
    int refractoryRemaining = 0;

    double currentSampleRate = 44100.0;
    float threshold = 0.03f;
    float previousInputLevel = 0.0f;
    int triggerMode = 0; // 0 = Audio, 1 = MIDI, 2 = Both

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayeredSampleEngine)
};