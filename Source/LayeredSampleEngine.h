#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

/**
    Six-layer transient sampler engine.

    Slots 0-2 are pre-sample layers. They start as soon as a trigger is detected.
    Slots 3-5 are main-sample layers. They start after the fixed pre-roll interval,
    aligned with the latency-compensated dry input.

    Sample memory is immutable after loading. Replaced sample buffers are retained
    until the processor is destroyed so the audio thread never observes freed memory.
*/
class LayeredSampleEngine final
{
public:
    static constexpr int kNumLayers = 6;
    static constexpr int kPreLayers = 3;
    static constexpr double kMaximumPreRollMilliseconds = 100.0;
    static constexpr double kDefaultPreRollMilliseconds = 50.0;

    struct LayerParameters
    {
        float gain = 1.0f;
        float pan = 0.0f;
        float pitchSemitones = 0.0f;
        float highPassHz = 20.0f;
        float lowPassHz = 20000.0f;
        float attackMs = 2.0f;
        float decayMs = 60.0f;
        float sustain = 1.0f;
        float releaseMs = 120.0f;
        int attackCurve = 1;
        int decayCurve = 1;
        int releaseCurve = 1;
        bool muted = false;
    };

    LayeredSampleEngine();
    ~LayeredSampleEngine() = default;

    void prepareToPlay (double sampleRate, int maximumBlockSize);
    void setTriggerMode (int mode, float threshold) noexcept;
    void setPreRollMilliseconds (float milliseconds) noexcept;

    bool loadSampleFromFile (int layer, const juce::File& file, juce::String& error);
    void clearSample (int layer);
    juce::String getSampleName (int layer) const;
    int getLoadedMask() const noexcept;

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

    void triggerAllLayers() noexcept;
    void startLayer (int layer) noexcept;
    float renderLayerSample (int layer, float& left, float& right) noexcept;
    float envelopeLevelAtAge (const LayerParameters& p, int age) const noexcept;
    float applyCurve (float progress, int curve) const noexcept;

    juce::AudioFormatManager formatManager;
    std::array<std::atomic<SampleData*>, kNumLayers> currentSamples {};
    std::array<std::atomic<bool>, kNumLayers> clearRequests {};
    std::array<std::vector<std::unique_ptr<SampleData>>, kNumLayers> ownedSamples;
    std::array<juce::String, kNumLayers> sampleNames;
    std::array<LayerParameters, kNumLayers> parameters {};
    std::array<Voice, kNumLayers> voices {};
    std::array<juce::AudioBuffer<float>, kNumLayers> layerBuffers;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, kNumLayers> layerHighPass;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, kNumLayers> layerLowPass;
    std::array<std::array<float, 257>, 5> curveTables {};

    std::vector<float> delayLeft, delayRight;
    int delayWritePosition = 0;
    int preRollSamples = 0;
    int maximumDelaySamples = 0;
    int refractorySamples = 0;
    int refractoryRemaining = 0;

    double currentSampleRate = 44100.0;
    float threshold = 0.12f;
    float previousInputLevel = 0.0f;
    int triggerMode = 0; // 0 = Audio, 1 = MIDI, 2 = Both

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayeredSampleEngine)
};
