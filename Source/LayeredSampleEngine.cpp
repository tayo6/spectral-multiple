#include "LayeredSampleEngine.h"

#include <cmath>

LayeredSampleEngine::LayeredSampleEngine()
{
    formatManager.registerBasicFormats();

    for (int i = 0; i < kNumLayers; ++i)
    {
        currentSamples[(size_t) i].store (nullptr, std::memory_order_relaxed);
        clearRequests[(size_t) i].store (false, std::memory_order_relaxed);
        previewRequests[(size_t) i].store (false, std::memory_order_relaxed);
        layerTailSeconds[(size_t) i].store (0.0f, std::memory_order_relaxed);
        tailEstimateDirty[(size_t) i].store (false, std::memory_order_relaxed);
    }
}

void LayeredSampleEngine::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    currentSampleRate = juce::jmax (1.0, sampleRate);
    maximumDelaySamples = juce::jmax (1, juce::roundToInt (currentSampleRate * kMaximumPreRollMilliseconds * 0.001));
    maximumTailSeconds.store ((float) ((double) maximumDelaySamples / currentSampleRate), std::memory_order_relaxed);
    preRollSamples = juce::jlimit (0, maximumDelaySamples,
                                   juce::roundToInt (currentSampleRate * kDefaultPreRollMilliseconds * 0.001));
    refractorySamples = juce::jmax (1, juce::roundToInt (currentSampleRate * 0.025));
    refractoryRemaining = 0;
    previousInputLevel = 0.0f;

    const auto delaySize = (size_t) (maximumDelaySamples + 1);
    delayLeft.assign (delaySize, 0.0f);
    delayRight.assign (delaySize, 0.0f);
    delayWritePosition = 0;

    for (int layer = 0; layer < kNumLayers; ++layer)
    {
        juce::dsp::ProcessSpec filterSpec { currentSampleRate,
                                            (juce::uint32) juce::jmax (1, maximumBlockSize), 2 };
        layerHighPass[(size_t) layer].prepare (filterSpec);
        layerHighPass[(size_t) layer].setType (juce::dsp::LinkwitzRileyFilterType::highpass);
        layerHighPass[(size_t) layer].setCutoffFrequency (20.0f);
        layerHighPass[(size_t) layer].reset();

        layerLowPass[(size_t) layer].prepare (filterSpec);
        layerLowPass[(size_t) layer].setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        layerLowPass[(size_t) layer].setCutoffFrequency ((float) juce::jmin (20000.0, currentSampleRate * 0.45));
        layerLowPass[(size_t) layer].reset();
    }

    for (auto& buffer : layerBuffers)
    {
        buffer.setSize (2, juce::jmax (1, maximumBlockSize), false, true, false);
        buffer.clear();
    }

    for (auto& layerVoices : voices)
        for (auto& voice : layerVoices)
            voice = {};
    nextVoiceToSteal.fill (0);
    for (auto& request : clearRequests)
        request.store (false, std::memory_order_relaxed);
    for (auto& request : previewRequests)
        request.store (false, std::memory_order_relaxed);
}

void LayeredSampleEngine::setTriggerMode (int mode, float newThreshold) noexcept
{
    triggerMode = juce::jlimit (0, 2, mode);
    threshold = juce::jlimit (0.001f, 1.0f, newThreshold);
}

void LayeredSampleEngine::setPreRollMilliseconds (float milliseconds) noexcept
{
    const float clamped = juce::jlimit (0.0f, (float) kMaximumPreRollMilliseconds, milliseconds);
    preRollSamples = juce::jlimit (0, maximumDelaySamples,
                                   juce::roundToInt (currentSampleRate * clamped * 0.001));
}

bool LayeredSampleEngine::loadSampleFromFile (int layer, const juce::File& file, juce::String& error)
{
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
    {
        error = "Invalid layer number.";
        return false;
    }

    if (! file.existsAsFile())
    {
        error = "The selected sample file does not exist.";
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        error = "JUCE could not read this audio file.";
        return false;
    }

    // Keep memory use bounded for this first engine milestone.
    const auto maximumSamples = (juce::int64) (reader->sampleRate * 180.0);
    if (reader->lengthInSamples > maximumSamples)
    {
        error = "Samples longer than three minutes are not supported yet.";
        return false;
    }

    const int sampleCount = (int) reader->lengthInSamples;
    const size_t requiredSampleBytes = (size_t) sampleCount * 2u * sizeof (float);
    if (requiredSampleBytes > kMaximumRetainedSampleBytes - retainedSampleBytes)
    {
        error = "The sample memory budget is full. Reopen the plugin to reclaim memory from replaced samples.";
        return false;
    }

    auto data = std::make_unique<SampleData>();
    data->audio.setSize (2, sampleCount, false, true, false);
    data->sourceSampleRate = reader->sampleRate;
    data->displayName = file.getFileNameWithoutExtension();

    bool readOk = false;
    if (reader->numChannels == 1)
    {
        juce::AudioBuffer<float> mono (1, sampleCount);
        readOk = reader->read (&mono, 0, sampleCount, 0, true, false);
        if (readOk)
        {
            data->audio.copyFrom (0, 0, mono, 0, 0, sampleCount);
            data->audio.copyFrom (1, 0, mono, 0, 0, sampleCount);
        }
    }
    else
    {
        readOk = reader->read (&data->audio, 0, sampleCount, 0, true, true);
    }

    if (! readOk)
    {
        error = "The audio file could not be decoded.";
        return false;
    }

    data->preview.durationMs = (double) sampleCount * 1000.0 / juce::jmax (1.0, reader->sampleRate);
    data->preview.loaded = true;
    for (int point = 0; point < kPreviewPoints; ++point)
    {
        const int rawFirst = (int) ((juce::int64) point * sampleCount / kPreviewPoints);
        const int rawLast = (int) ((juce::int64) (point + 1) * sampleCount / kPreviewPoints);
        const int first = juce::jlimit (0, sampleCount - 1, rawFirst);
        const int last = juce::jlimit (first + 1, sampleCount, juce::jmax (first + 1, rawLast));
        float minimum = 1.0f, maximum = -1.0f;
        for (int sample = first; sample < last; ++sample)
            for (int channel = 0; channel < 2; ++channel)
            {
                const float value = data->audio.getSample (channel, sample);
                minimum = juce::jmin (minimum, value);
                maximum = juce::jmax (maximum, value);
            }
        data->preview.minimum[(size_t) point] = minimum;
        data->preview.maximum[(size_t) point] = maximum;
    }

    auto* raw = data.get();

    // Never free an old buffer while the audio thread might still be reading it.
    // All versions remain owned until this processor instance is destroyed.
    ownedSamples[(size_t) layer].push_back (std::move (data));
    retainedSampleBytes += requiredSampleBytes;
    sampleNames[(size_t) layer] = raw->displayName;
    currentSamples[(size_t) layer].store (raw, std::memory_order_release);
    tailEstimateDirty[(size_t) layer].store (true, std::memory_order_release);

    error.clear();
    return true;
}

void LayeredSampleEngine::clearSample (int layer)
{
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
        return;

    currentSamples[(size_t) layer].store (nullptr, std::memory_order_release);
    clearRequests[(size_t) layer].store (true, std::memory_order_release);
    tailEstimateDirty[(size_t) layer].store (true, std::memory_order_release);
    sampleNames[(size_t) layer].clear();
}

juce::String LayeredSampleEngine::getSampleName (int layer) const
{
    return juce::isPositiveAndBelow (layer, kNumLayers) ? sampleNames[(size_t) layer] : juce::String();
}

LayeredSampleEngine::WaveformPreview LayeredSampleEngine::getWaveformPreview (int layer) const noexcept
{
    WaveformPreview preview;
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
        return preview;

    if (auto* sample = currentSamples[(size_t) layer].load (std::memory_order_acquire))
        return sample->preview;

    return preview;
}

void LayeredSampleEngine::requestPreview (int layer) noexcept
{
    if (juce::isPositiveAndBelow (layer, kNumLayers))
        previewRequests[(size_t) layer].store (true, std::memory_order_release);
}

int LayeredSampleEngine::getLoadedMask() const noexcept
{
    int mask = 0;
    for (int i = 0; i < kNumLayers; ++i)
        if (currentSamples[(size_t) i].load (std::memory_order_acquire) != nullptr)
            mask |= (1 << i);

    return mask;
}

void LayeredSampleEngine::setLayerParameters (int layer, const LayerParameters& p) noexcept
{
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
        return;

    auto& target = parameters[(size_t) layer];
    const bool tailParametersChanged = target.pitchSemitones != p.pitchSemitones
        || target.pogo != p.pogo || target.trimStart != p.trimStart || target.trimEnd != p.trimEnd;
    const bool recomputeTail = tailParametersChanged
        || tailEstimateDirty[(size_t) layer].exchange (false, std::memory_order_acq_rel);

    target.gain = juce::jlimit (0.0f, 2.0f, p.gain);
    target.pan = juce::jlimit (-1.0f, 1.0f, p.pan);
    target.pitchSemitones = juce::jlimit (-24.0f, 24.0f, p.pitchSemitones);
    target.delayShiftMs = juce::jlimit (-100.0f, 100.0f, p.delayShiftMs);
    target.highPassHz = juce::jlimit (20.0f, 1000.0f, p.highPassHz);
    target.lowPassHz = juce::jlimit (1000.0f, (float) juce::jmax (1000.0, currentSampleRate * 0.45), p.lowPassHz);
    target.highPassHz = juce::jmin (target.highPassHz, target.lowPassHz * 0.8f);
    target.lowPassHz = juce::jmax (target.lowPassHz, target.highPassHz * 1.25f);
    layerHighPass[(size_t) layer].setCutoffFrequency (target.highPassHz);
    layerLowPass[(size_t) layer].setCutoffFrequency (juce::jmin (target.lowPassHz, (float) (currentSampleRate * 0.45)));
    target.attackMs = juce::jlimit (0.1f, 2000.0f, p.attackMs);
    target.holdMs = juce::jlimit (0.0f, 2000.0f, p.holdMs);
    target.decayMs = juce::jlimit (0.1f, 5000.0f, p.decayMs);
    target.sustain = juce::jlimit (0.0f, 1.0f, p.sustain);
    target.releaseMs = juce::jlimit (0.1f, 10000.0f, p.releaseMs);
    target.attackCurve = juce::jlimit (0, 7, p.attackCurve);
    target.decayCurve = juce::jlimit (0, 7, p.decayCurve);
    target.releaseCurve = juce::jlimit (0, 7, p.releaseCurve);
    target.attackTension = juce::jlimit (-1.0f, 1.0f, p.attackTension);
    target.decayTension = juce::jlimit (-1.0f, 1.0f, p.decayTension);
    target.releaseTension = juce::jlimit (-1.0f, 1.0f, p.releaseTension);
    target.envEnabled = p.envEnabled;
    target.muted = p.muted;
    target.solo = p.solo;
    target.reverse = p.reverse;
    target.pogo = juce::jlimit (-1.0f, 1.0f, p.pogo);
    target.trimStart = juce::jlimit (0.0f, 0.98f, p.trimStart);
    target.trimEnd = juce::jlimit (target.trimStart + 0.01f, 1.0f, p.trimEnd);
    target.fadeInMs = juce::jlimit (0.0f, 2000.0f, p.fadeInMs);
    target.fadeOutMs = juce::jlimit (0.0f, 5000.0f, p.fadeOutMs);

    if (recomputeTail)
        updateTailEstimate (layer);
}

bool LayeredSampleEngine::isLayerAudible (int layer) const noexcept
{
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
        return false;

    const auto& p = parameters[(size_t) layer];
    if (p.muted)
        return false;

    bool anySolo = false;
    for (int i = 0; i < kNumLayers; ++i)
    {
        if (parameters[(size_t) i].solo)
        {
            anySolo = true;
            break;
        }
    }

    return ! anySolo || p.solo;
}

void LayeredSampleEngine::updateTailEstimate (int layer) noexcept
{
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
        return;

    double estimateSeconds = 0.0;
    auto* sample = currentSamples[(size_t) layer].load (std::memory_order_acquire);
    if (sample != nullptr && sample->sourceSampleRate > 0.0)
    {
        const auto& p = parameters[(size_t) layer];
        const float trimFraction = juce::jlimit (0.01f, 1.0f, p.trimEnd - p.trimStart);
        const double sourceDuration = ((double) sample->audio.getNumSamples() * (double) trimFraction)
                                      / sample->sourceSampleRate;

        // Estimate the variable Pogo pitch ramp without doing this work every audio block.
        constexpr int integrationSteps = 32;
        double durationMultiplier = 0.0;
        for (int i = 0; i < integrationSteps; ++i)
        {
            const double u = ((double) i + 0.5) / (double) integrationSteps;
            const double pogoSemitones = (double) p.pogo * 24.0 * std::pow (1.0 - u, 2.5);
            const double semitones = (double) p.pitchSemitones + pogoSemitones;
            durationMultiplier += std::pow (2.0, -semitones / 12.0);
        }
        estimateSeconds = sourceDuration * durationMultiplier / (double) integrationSteps;
        estimateSeconds += (double) maximumDelaySamples * 2.0 / juce::jmax (1.0, currentSampleRate);
    }

    layerTailSeconds[(size_t) layer].store ((float) estimateSeconds, std::memory_order_relaxed);
    // The delayed dry-through path still has this fixed latency even with no samples loaded.
    float maximum = maximumDelaySamples > 0
        ? (float) ((double) maximumDelaySamples / juce::jmax (1.0, currentSampleRate))
        : 0.0f;
    for (const auto& value : layerTailSeconds)
        maximum = juce::jmax (maximum, value.load (std::memory_order_relaxed));
    maximumTailSeconds.store (maximum, std::memory_order_relaxed);
}

void LayeredSampleEngine::triggerAllLayers() noexcept
{
    for (int i = 0; i < kNumLayers; ++i)
        startLayer (i);
}

void LayeredSampleEngine::startLayer (int layer, bool ignoreTimingShift) noexcept
{
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
        return;

    auto* sample = currentSamples[(size_t) layer].load (std::memory_order_acquire);
    if (sample == nullptr)
        return;

    if (! ignoreTimingShift && ! isLayerAudible (layer))
        return;
    if (ignoreTimingShift && parameters[(size_t) layer].muted)
        return;

    auto& layerVoices = voices[(size_t) layer];

    int selectedVoice = -1;
    for (int offset = 0; offset < kVoicesPerLayer; ++offset)
    {
        const int candidate = (nextVoiceToSteal[(size_t) layer] + offset) % kVoicesPerLayer;
        if (! layerVoices[(size_t) candidate].active)
        {
            selectedVoice = candidate;
            break;
        }
    }

    if (selectedVoice < 0)
        selectedVoice = nextVoiceToSteal[(size_t) layer];

    nextVoiceToSteal[(size_t) layer] = (selectedVoice + 1) % kVoicesPerLayer;

    auto& voice = layerVoices[(size_t) selectedVoice];
    voice.sample = sample;
    voice.position = 0.0;
    const int shiftSamples = ignoreTimingShift ? 0
        : juce::roundToInt (parameters[(size_t) layer].delayShiftMs * (float) currentSampleRate * 0.001f);
    const int preLayerCompatibilityOffset = layer < kPreLayers
        ? juce::roundToInt ((float) kDefaultPreRollMilliseconds * (float) currentSampleRate * 0.001f)
        : 0;
    const int baseDelaySamples = layer < kPreLayers
        ? maximumDelaySamples - preRollSamples + preLayerCompatibilityOffset
        : maximumDelaySamples;

    voice.delayRemaining = ignoreTimingShift
        ? 0
        : juce::jlimit (0, maximumDelaySamples * 2, baseDelaySamples + shiftSamples);
    voice.ageSamples = 0;
    voice.releaseStartLevel = 1.0f;
    voice.inRelease = false;
    voice.active = true;
}

float LayeredSampleEngine::applyPowerTension (float u, float tension) noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, u);
    const float ten = juce::jlimit (-1.0f, 1.0f, tension);
    if (std::abs (ten) < 1.0e-4f)
        return t;
    const float expVal = std::pow (2.0f, std::abs (ten) * 3.0f);
    return ten >= 0.0f ? (1.0f - std::pow (1.0f - t, expVal)) : std::pow (t, expVal);
}

float LayeredSampleEngine::flSegmentLevel (float progress, float y0, float y1, int mode, float tension) noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, progress);
    const float ten = juce::jlimit (-1.0f, 1.0f, tension);
    const int m = juce::jlimit (0, 7, mode);
    const float dirTen = (y1 >= y0) ? ten : -ten;

    float frac = t;
    switch (m)
    {
        case 0: // Hold
            frac = t >= 1.0f ? 1.0f : 0.0f;
            break;
        case 1: // Smooth
        {
            const float s = applyPowerTension (t, dirTen);
            frac = s * s * (3.0f - 2.0f * s);
            break;
        }
        case 2: // Single curve
            frac = applyPowerTension (t, dirTen);
            break;
        case 3: // Half sine
        {
            const float hs = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t);
            frac = applyPowerTension (hs, dirTen);
            break;
        }
        case 4: // Stairs
        {
            const float steps = juce::jmax (2.0f, std::round (2.0f + (ten + 1.0f) * 6.0f));
            frac = t >= 1.0f ? 1.0f : std::floor (t * steps) / steps;
            break;
        }
        case 5: // Smooth stairs
        {
            const float steps = juce::jmax (2.0f, std::round (2.0f + (ten + 1.0f) * 6.0f));
            if (t >= 1.0f)
            {
                frac = 1.0f;
            }
            else
            {
                const float scaled = t * steps;
                const float idx = std::floor (scaled);
                const float f = scaled - idx;
                const float smoothF = f * f * (3.0f - 2.0f * f);
                frac = (idx + smoothF) / steps;
            }
            break;
        }
        case 6: // Pulse
        {
            const float cycles = juce::jmax (1.0f, std::round (1.0f + (ten + 1.0f) * 5.0f));
            if (t >= 1.0f)
            {
                frac = 1.0f;
            }
            else
            {
                const float phase = std::fmod (t * cycles, 1.0f);
                frac = phase < 0.5f ? 0.0f : 1.0f;
            }
            break;
        }
        case 7: // Wave
        {
            const float cycles = juce::jmax (1.0f, std::round (1.0f + (ten + 1.0f) * 5.0f));
            frac = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi * (2.0f * cycles + 1.0f) * t));
            break;
        }
        default:
            frac = applyPowerTension (t, dirTen);
            break;
    }

    return y0 + (y1 - y0) * frac;
}

float LayeredSampleEngine::envelopeLevelAtAge (const LayerParameters& p,
                                                int age) const noexcept
{
    if (! p.envEnabled)
        return 1.0f;

    const float attackSamples = juce::jmax (1.0f, p.attackMs * 0.001f * (float) currentSampleRate);
    const float decaySamples = juce::jmax (1.0f, p.decayMs * 0.001f * (float) currentSampleRate);

    if ((float) age < attackSamples)
        return flSegmentLevel ((float) age / attackSamples, 0.0f, 1.0f, p.attackCurve, p.attackTension);

    const float holdSamples = p.holdMs * 0.001f * (float) currentSampleRate;
    if ((float) age < attackSamples + holdSamples)
        return 1.0f;

    const float decayProgress = ((float) age - attackSamples - holdSamples) / decaySamples;
    if (decayProgress < 1.0f)
        return flSegmentLevel (decayProgress, 1.0f, p.sustain, p.decayCurve, p.decayTension);

    return p.sustain;
}

float LayeredSampleEngine::renderLayerSample (int layer, float& left, float& right) noexcept
{
    left = right = 0.0f;
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
        return 0.0f;

    const auto& p = parameters[(size_t) layer];
    auto& layerVoices = voices[(size_t) layer];

    if (! isLayerAudible (layer))
    {
        for (auto& voice : layerVoices)
            voice.active = false;
    }
    else
    {
        const float leftPan = p.pan > 0.0f ? 1.0f - p.pan : 1.0f;
        const float rightPan = p.pan < 0.0f ? 1.0f + p.pan : 1.0f;

        for (auto& voice : layerVoices)
        {
            if (! voice.active || voice.sample == nullptr)
            {
                voice.active = false;
                continue;
            }

            if (voice.delayRemaining > 0)
            {
                --voice.delayRemaining;
                continue;
            }

            const auto& audio = voice.sample->audio;
            const int length = audio.getNumSamples();

            if (length <= 1)
            {
                voice.active = false;
                continue;
            }

            const int trimStartIdx = juce::jlimit (0, length - 2, (int) std::floor ((double) p.trimStart * (double) length));
            const int trimEndIdx = juce::jlimit (trimStartIdx + 1, length, (int) std::ceil ((double) p.trimEnd * (double) length));
            const int trimmedLength = trimEndIdx - trimStartIdx;

            if (trimmedLength <= 0 || voice.position >= (double) trimmedLength)
            {
                voice.active = false;
                continue;
            }

            const double u = juce::jlimit (0.0, 1.0, voice.position / (double) trimmedLength);
            const double pogoSemitones = (double) p.pogo * 24.0 * std::pow (1.0 - u, 2.5);
            const double speed = (voice.sample->sourceSampleRate / currentSampleRate)
                                 * std::pow (2.0, ((double) p.pitchSemitones + pogoSemitones) / 12.0);

            const double samplePos = p.reverse
                ? ((double) (trimEndIdx - 1) - voice.position)
                : ((double) trimStartIdx + voice.position);

            const int index0 = juce::jlimit (trimStartIdx, trimEndIdx - 1, (int) std::floor (samplePos));
            const int index1 = p.reverse ? juce::jmax (trimStartIdx, index0 - 1)
                                         : juce::jmin (trimEndIdx - 1, index0 + 1);
            // Both directions interpolate from index0 toward index1 using a positive fraction.
            // For reverse playback index1 is the preceding sample, so this avoids the old
            // negative interpolation fraction and the resulting level overshoot.
            const float fraction = (float) (samplePos - (double) index0);

            const float rawL = audio.getSample (0, index0)
                               + (audio.getSample (0, index1) - audio.getSample (0, index0)) * fraction;
            const float rawR = audio.getSample (1, index0)
                               + (audio.getSample (1, index1) - audio.getSample (1, index0)) * fraction;

            const float safeSpeed = (float) juce::jmax (0.0001, speed);
            const float totalOutputSamples = (float) trimmedLength / safeSpeed;
            const float elapsedSamples = (float) voice.ageSamples;
            const float remainingSamples = (float) ((double) trimmedLength - voice.position) / safeSpeed;
            const float requestedReleaseSamples = juce::jmax (1.0f, p.releaseMs * 0.001f * (float) currentSampleRate);
            const float releaseSamples = juce::jmin (requestedReleaseSamples,
                                                      juce::jmax (1.0f, totalOutputSamples * 0.5f));

            float envelope = envelopeLevelAtAge (p, voice.ageSamples);

            if (p.envEnabled && remainingSamples <= releaseSamples)
            {
                if (! voice.inRelease)
                {
                    voice.releaseStartLevel = envelope;
                    voice.inRelease = true;
                }

                const float releaseProgress = 1.0f - remainingSamples / releaseSamples;
                envelope = flSegmentLevel (releaseProgress, voice.releaseStartLevel, 0.0f,
                                           p.releaseCurve, p.releaseTension);
            }

            const float fadeInSamples = p.fadeInMs * 0.001f * (float) currentSampleRate;
            const float fadeOutSamples = p.fadeOutMs * 0.001f * (float) currentSampleRate;
            const float fadeInGain = fadeInSamples > 0.5f
                ? juce::jlimit (0.0f, 1.0f, elapsedSamples / fadeInSamples)
                : 1.0f;
            const float fadeOutGain = fadeOutSamples > 0.5f
                ? juce::jlimit (0.0f, 1.0f, remainingSamples / fadeOutSamples)
                : 1.0f;

            const float gain = p.gain * envelope * fadeInGain * fadeOutGain;
            left += rawL * gain * leftPan;
            right += rawR * gain * rightPan;

            voice.position += speed;
            ++voice.ageSamples;

            if (voice.position >= (double) trimmedLength)
                voice.active = false;
        }
    }

    left = layerLowPass[(size_t) layer].processSample (
        0, layerHighPass[(size_t) layer].processSample (0, left));
    right = layerLowPass[(size_t) layer].processSample (
        1, layerHighPass[(size_t) layer].processSample (1, right));

    return (left + right) * 0.5f;
}

void LayeredSampleEngine::processBlock (const juce::AudioBuffer<float>& triggerInput,
                                        juce::AudioBuffer<float>& masterOutput,
                                        const std::array<juce::AudioBuffer<float>*, kNumLayers>& layerOutputs,
                                        const juce::MidiBuffer& midi) noexcept
{
    const int numSamples = juce::jmin (triggerInput.getNumSamples(), masterOutput.getNumSamples());
    const int inputChannels = triggerInput.getNumChannels();
    const int outputChannels = masterOutput.getNumChannels();

    if (numSamples <= 0 || inputChannels <= 0 || outputChannels <= 0
        || delayLeft.empty() || delayRight.empty())
        return;

    for (int layer = 0; layer < kNumLayers; ++layer)
    {
        if (clearRequests[(size_t) layer].exchange (false, std::memory_order_acq_rel))
        {
            for (auto& voice : voices[(size_t) layer])
                voice = {};
            nextVoiceToSteal[(size_t) layer] = 0;
        }
        if (previewRequests[(size_t) layer].exchange (false, std::memory_order_acq_rel))
            startLayer (layer, true);
    }

    for (auto& buffer : layerBuffers)
    {
        if (numSamples > buffer.getNumSamples())
            return;

        buffer.clear (0, numSamples);
    }

    masterOutput.clear (0, numSamples);
    for (auto* bus : layerOutputs)
        if (bus != nullptr)
            bus->clear (0, numSamples);

    juce::MidiBuffer::Iterator midiIterator (midi);
    juce::MidiMessage midiMessage;
    int midiPosition = 0;
    bool hasMidiEvent = midiIterator.getNextEvent (midiMessage, midiPosition);

    const bool audioEnabled = triggerMode == 0 || triggerMode == 2;
    const bool midiEnabled = triggerMode == 1 || triggerMode == 2;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        bool midiTriggeredThisSample = false;
        while (hasMidiEvent && midiPosition <= sample)
        {
            if (midiEnabled && midiMessage.isNoteOn())
            {
                triggerAllLayers();
                midiTriggeredThisSample = true;
            }

            hasMidiEvent = midiIterator.getNextEvent (midiMessage, midiPosition);
        }

        const float inputL = triggerInput.getSample (0, sample);
        const float inputR = inputChannels > 1 ? triggerInput.getSample (1, sample) : inputL;
        const float inputLevel = juce::jmax (std::abs (inputL), std::abs (inputR));

        if (audioEnabled && ! midiTriggeredThisSample && refractoryRemaining <= 0
            && inputLevel >= threshold && previousInputLevel < threshold)
        {
            triggerAllLayers();
            refractoryRemaining = refractorySamples;
        }

        previousInputLevel = inputLevel;
        if (refractoryRemaining > 0)
            --refractoryRemaining;

        delayLeft[(size_t) delayWritePosition] = inputL;
        delayRight[(size_t) delayWritePosition] = inputR;
        const int readPosition = (delayWritePosition + (int) delayLeft.size() - maximumDelaySamples)
                                 % (int) delayLeft.size();
        const float dryL = delayLeft[(size_t) readPosition];
        const float dryR = delayRight[(size_t) readPosition];
        delayWritePosition = (delayWritePosition + 1) % (int) delayLeft.size();

        float masterL = dryL;
        float masterR = dryR;

        for (int layer = 0; layer < kNumLayers; ++layer)
        {
            float layerL = 0.0f, layerR = 0.0f;
            renderLayerSample (layer, layerL, layerR);

            layerBuffers[(size_t) layer].setSample (0, sample, layerL);
            layerBuffers[(size_t) layer].setSample (1, sample, layerR);

            if (auto* bus = layerOutputs[(size_t) layer])
            {
                if (bus->getNumChannels() > 0)
                    bus->setSample (0, sample, layerL);
                if (bus->getNumChannels() > 1)
                    bus->setSample (1, sample, layerR);
            }

            masterL += layerL;
            masterR += layerR;
        }

        if (outputChannels > 1)
        {
            masterOutput.setSample (0, sample, masterL);
            masterOutput.setSample (1, sample, masterR);
        }
        else
            masterOutput.setSample (0, sample, 0.5f * (masterL + masterR));
    }
}

const juce::AudioBuffer<float>& LayeredSampleEngine::getLayerBuffer (int layer) const noexcept
{
    jassert (juce::isPositiveAndBelow (layer, kNumLayers));
    return layerBuffers[(size_t) juce::jlimit (0, kNumLayers - 1, layer)];
}