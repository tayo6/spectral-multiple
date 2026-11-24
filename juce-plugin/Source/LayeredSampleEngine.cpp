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
        lastHighPassHz[(size_t) i] = 20.0f;
        lastLowPassHz[(size_t) i] = 20000.0f;
        layerHighPassActive[(size_t) i] = false;
        layerLowPassActive[(size_t) i] = false;
        layerWasAudible[(size_t) i] = true;
    }
}

void LayeredSampleEngine::assignVoiceSample (Voice& voice, SampleData* nextSample) noexcept
{
    if (voice.sample != nullptr)
        voice.sample->activeVoices.fetch_sub (1, std::memory_order_acq_rel);

    voice.sample = nextSample;

    if (voice.sample != nullptr)
        voice.sample->activeVoices.fetch_add (1, std::memory_order_acq_rel);
}

void LayeredSampleEngine::reclaimRetiredSamplesLocked()
{
    const uint64_t currentBlock = completedAudioBlocks.load (std::memory_order_acquire);

    for (auto& layerRetired : retiredSamples)
    {
        for (size_t i = 0; i < layerRetired.size();)
        {
            const auto& candidate = layerRetired[i];
            if (candidate != nullptr
                && candidate->activeVoices.load (std::memory_order_acquire) <= 0
                && currentBlock > candidate->retiredAfterBlock)
            {
                layerRetired[i] = std::move (layerRetired.back());
                layerRetired.pop_back();
            }
            else
            {
                ++i;
            }
        }
    }
}

int LayeredSampleEngine::getRetiredSampleCount (int layer)
{
    const juce::ScopedLock lock (sampleStateLock);
    reclaimRetiredSamplesLocked();
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
        return 0;
    return (int) retiredSamples[(size_t) layer].size();
}

void LayeredSampleEngine::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    currentSampleRate = juce::jmax (1.0, sampleRate);
    maximumDelaySamples = juce::jmax (1, juce::roundToInt (currentSampleRate * kMaximumPreRollMilliseconds * 0.001));
    preRollSamples = juce::jlimit (0, maximumDelaySamples,
                                   juce::roundToInt (currentSampleRate * currentPreRollMs * 0.001));
    refractorySamples = juce::jmax (1, juce::roundToInt (currentSampleRate * 0.025));
    refractoryRemaining = 0;
    previousInputLevel = 0.0f;

    const auto delaySize = (size_t) (maximumDelaySamples + 1);
    delayLeft.assign (delaySize, 0.0f);
    delayRight.assign (delaySize, 0.0f);
    delayWritePosition = 0;

    const int safeBlockSize = juce::jmax (1, maximumBlockSize);
    const float defaultLpCutoff = (float) juce::jmin (20000.0, currentSampleRate * 0.45);

    for (int layer = 0; layer < kNumLayers; ++layer)
    {
        juce::dsp::ProcessSpec filterSpec { currentSampleRate,
                                            (juce::uint32) safeBlockSize, 2 };
        layerHighPass[(size_t) layer].prepare (filterSpec);
        layerHighPass[(size_t) layer].setType (juce::dsp::LinkwitzRileyFilterType::highpass);
        layerHighPass[(size_t) layer].setCutoffFrequency (20.0f);
        layerHighPass[(size_t) layer].reset();

        layerLowPass[(size_t) layer].prepare (filterSpec);
        layerLowPass[(size_t) layer].setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        layerLowPass[(size_t) layer].setCutoffFrequency (defaultLpCutoff);
        layerLowPass[(size_t) layer].reset();

        lastHighPassHz[(size_t) layer] = 20.0f;
        lastLowPassHz[(size_t) layer] = defaultLpCutoff;
        layerHighPassActive[(size_t) layer] = false;
        layerLowPassActive[(size_t) layer] = false;
        layerWasAudible[(size_t) layer] = true;
    }

    for (auto& buffer : layerBuffers)
    {
        buffer.setSize (2, safeBlockSize, false, true, false);
        buffer.clear();
    }

    for (auto& layerVoices : voices)
    {
        for (auto& voice : layerVoices)
        {
            assignVoiceSample (voice, nullptr);
            voice.position = 0.0;
            voice.delayRemaining = 0;
            voice.ageSamples = 0;
            voice.releaseStartLevel = 1.0f;
            voice.inRelease = false;
            voice.active = false;
        }
    }
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
    currentPreRollMs = (double) juce::jlimit (0.0f, (float) kMaximumPreRollMilliseconds, milliseconds);
    preRollSamples = juce::jlimit (0, maximumDelaySamples,
                                   juce::roundToInt (currentSampleRate * currentPreRollMs * 0.001));
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
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
    {
        error = "JUCE could not read this audio file.";
        return false;
    }

    // Keep memory use bounded while allowing real multi-second samples.
    const auto maximumSamples = (juce::int64) (reader->sampleRate * 180.0);
    if (reader->lengthInSamples > maximumSamples)
    {
        error = "Samples longer than three minutes are not supported yet.";
        return false;
    }

    const int sampleCount = (int) reader->lengthInSamples;
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

    data->preview.durationMs = (double) sampleCount * 1000.0 / reader->sampleRate;
    data->preview.loaded = true;
    for (int point = 0; point < kPreviewPoints; ++point)
    {
        const int first = juce::jlimit (0, sampleCount - 1, (int) ((juce::int64) point * sampleCount / kPreviewPoints));
        const int last = juce::jlimit (first + 1, sampleCount,
                                       juce::jmax (first + 1, (int) ((juce::int64) (point + 1) * sampleCount / kPreviewPoints)));
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
    {
        const juce::ScopedLock lock (sampleStateLock);
        reclaimRetiredSamplesLocked();

        if (activeSamples[(size_t) layer] != nullptr)
        {
            activeSamples[(size_t) layer]->retiredAfterBlock = completedAudioBlocks.load (std::memory_order_acquire);
            retiredSamples[(size_t) layer].push_back (std::move (activeSamples[(size_t) layer]));
        }

        activeSamples[(size_t) layer] = std::move (data);
        sampleNames[(size_t) layer] = raw->displayName;
        currentSamples[(size_t) layer].store (raw, std::memory_order_release);
    }

    error.clear();
    return true;
}

void LayeredSampleEngine::clearSample (int layer)
{
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
        return;

    const juce::ScopedLock lock (sampleStateLock);
    currentSamples[(size_t) layer].store (nullptr, std::memory_order_release);
    clearRequests[(size_t) layer].store (true, std::memory_order_release);
    sampleNames[(size_t) layer].clear();

    if (activeSamples[(size_t) layer] != nullptr)
    {
        activeSamples[(size_t) layer]->retiredAfterBlock = completedAudioBlocks.load (std::memory_order_acquire);
        retiredSamples[(size_t) layer].push_back (std::move (activeSamples[(size_t) layer]));
    }
    reclaimRetiredSamplesLocked();
}

juce::String LayeredSampleEngine::getSampleName (int layer) const
{
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
        return {};
    const juce::ScopedLock lock (sampleStateLock);
    return sampleNames[(size_t) layer];
}

LayeredSampleEngine::WaveformPreview LayeredSampleEngine::getWaveformPreview (int layer) const noexcept
{
    WaveformPreview preview;
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
        return preview;

    const juce::ScopedLock lock (sampleStateLock);
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
    const float maxLp = (float) juce::jmax (1000.0, currentSampleRate * 0.45);

    target.gain = juce::jlimit (0.0f, 2.0f, p.gain);
    target.pan = juce::jlimit (-1.0f, 1.0f, p.pan);
    target.pitchSemitones = juce::jlimit (-24.0f, 24.0f, p.pitchSemitones);
    target.delayShiftMs = juce::jlimit (-100.0f, 100.0f, p.delayShiftMs);
    target.highPassHz = juce::jlimit (20.0f, 1000.0f, p.highPassHz);
    target.lowPassHz = juce::jlimit (1000.0f, maxLp, p.lowPassHz);
    target.highPassHz = juce::jmin (target.highPassHz, target.lowPassHz * 0.8f);
    target.lowPassHz = juce::jmax (target.lowPassHz, target.highPassHz * 1.25f);

    const bool hpActive = target.highPassHz > 20.5f;
    const bool lpActive = p.lowPassHz < 19990.0f;

    if (hpActive != layerHighPassActive[(size_t) layer])
    {
        layerHighPass[(size_t) layer].reset();
        layerHighPassActive[(size_t) layer] = hpActive;
    }
    if (hpActive && std::abs (target.highPassHz - lastHighPassHz[(size_t) layer]) > 1.0e-3f)
    {
        layerHighPass[(size_t) layer].setCutoffFrequency (target.highPassHz);
        lastHighPassHz[(size_t) layer] = target.highPassHz;
    }

    if (lpActive != layerLowPassActive[(size_t) layer])
    {
        layerLowPass[(size_t) layer].reset();
        layerLowPassActive[(size_t) layer] = lpActive;
    }
    if (lpActive && std::abs (target.lowPassHz - lastLowPassHz[(size_t) layer]) > 1.0e-3f)
    {
        layerLowPass[(size_t) layer].setCutoffFrequency (juce::jmin (target.lowPassHz, maxLp));
        lastLowPassHz[(size_t) layer] = target.lowPassHz;
    }

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
    target.trimStart = juce::jlimit (0.0f, 0.99f, p.trimStart);
    target.trimEnd = juce::jlimit (target.trimStart + 0.005f, 1.0f, p.trimEnd);
    target.fadeInMs = juce::jlimit (0.0f, 2000.0f, p.fadeInMs);
    target.fadeOutMs = juce::jlimit (0.0f, 5000.0f, p.fadeOutMs);
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
    {
        // Prefer stealing the oldest voice deterministically.
        int bestAge = -1;
        selectedVoice = nextVoiceToSteal[(size_t) layer];
        for (int offset = 0; offset < kVoicesPerLayer; ++offset)
        {
            const int candidate = (nextVoiceToSteal[(size_t) layer] + offset) % kVoicesPerLayer;
            if (layerVoices[(size_t) candidate].ageSamples > bestAge)
            {
                bestAge = layerVoices[(size_t) candidate].ageSamples;
                selectedVoice = candidate;
            }
        }
    }

    nextVoiceToSteal[(size_t) layer] = (selectedVoice + 1) % kVoicesPerLayer;

    auto& voice = layerVoices[(size_t) selectedVoice];
    assignVoiceSample (voice, sample);
    voice.position = 0.0;

    const int baseDelay = (layer < kPreLayers)
        ? juce::jmax (0, maximumDelaySamples - preRollSamples)
        : maximumDelaySamples;
    const int shiftSamples = juce::roundToInt (parameters[(size_t) layer].delayShiftMs * (float) currentSampleRate * 0.001f);

    voice.delayRemaining = ignoreTimingShift
        ? 0
        : juce::jlimit (0, maximumDelaySamples * 2, baseDelay + shiftSamples);
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

LayeredSampleEngine::EnvelopeSchedule LayeredSampleEngine::computeEnvelopeSchedule (const LayerParameters& p,
                                                                                      float totalDurationMs) noexcept
{
    const float dur = juce::jmax (1.0f, totalDurationMs);
    const float a = juce::jmax (0.1f, p.attackMs);
    const float h = juce::jmax (0.0f, p.holdMs);
    const float d = juce::jmax (0.1f, p.decayMs);
    const float r = juce::jmax (0.1f, p.releaseMs);
    const float sum = a + h + d + r;
    const float scale = sum > dur ? (dur / sum) : 1.0f;

    const float effA = a * scale;
    const float effH = h * scale;
    const float effD = d * scale;
    const float effR = r * scale;

    EnvelopeSchedule sched;
    sched.attackEndMs = effA;
    sched.holdEndMs = effA + effH;
    sched.decayEndMs = effA + effH + effD;
    sched.releaseStartMs = juce::jmax (sched.decayEndMs, dur - effR);
    sched.releaseMs = juce::jmax (0.1f, dur - sched.releaseStartMs);
    return sched;
}

float LayeredSampleEngine::envelopeLevelAtMs (const LayerParameters& p,
                                                const EnvelopeSchedule& sched,
                                                float ageMs) noexcept
{
    if (! p.envEnabled)
        return 1.0f;

    const float attSpan = juce::jmax (0.05f, sched.attackEndMs);
    if (ageMs < sched.attackEndMs)
        return flSegmentLevel (ageMs / attSpan, 0.0f, 1.0f, p.attackCurve, p.attackTension);

    if (ageMs < sched.holdEndMs)
        return 1.0f;

    const float decSpan = juce::jmax (0.05f, sched.decayEndMs - sched.holdEndMs);
    if (ageMs < sched.decayEndMs)
        return flSegmentLevel ((ageMs - sched.holdEndMs) / decSpan, 1.0f, p.sustain, p.decayCurve, p.decayTension);

    return p.sustain;
}

float LayeredSampleEngine::renderLayerSample (int layer, float& left, float& right) noexcept
{
    left = right = 0.0f;
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
        return 0.0f;

    const auto& p = parameters[(size_t) layer];
    auto& layerVoices = voices[(size_t) layer];
    const bool audible = isLayerAudible (layer);

    if (! audible)
    {
        for (auto& voice : layerVoices)
        {
            voice.active = false;
            assignVoiceSample (voice, nullptr);
        }

        if (layerWasAudible[(size_t) layer])
        {
            layerHighPass[(size_t) layer].reset();
            layerLowPass[(size_t) layer].reset();
            layerWasAudible[(size_t) layer] = false;
        }
        return 0.0f;
    }

    layerWasAudible[(size_t) layer] = true;

    const float leftPan = p.pan > 0.0f ? 1.0f - p.pan : 1.0f;
    const float rightPan = p.pan < 0.0f ? 1.0f + p.pan : 1.0f;

    for (auto& voice : layerVoices)
    {
        if (! voice.active || voice.sample == nullptr)
        {
            voice.active = false;
            assignVoiceSample (voice, nullptr);
            continue;
        }

        if (voice.delayRemaining > 0)
        {
            --voice.delayRemaining;
            continue;
        }

        const auto& audio = voice.sample->audio;
        const int length = audio.getNumSamples();

        if (length <= 0)
        {
            voice.active = false;
            assignVoiceSample (voice, nullptr);
            continue;
        }

        const int trimStartIdx = (length <= 1) ? 0
            : juce::jlimit (0, length - 2, (int) std::floor ((double) p.trimStart * (double) length));
        const int trimEndIdx = (length <= 1) ? 1
            : juce::jlimit (trimStartIdx + 1, length, (int) std::ceil ((double) p.trimEnd * (double) length));
        const int trimmedLength = trimEndIdx - trimStartIdx;

        if (trimmedLength <= 0 || voice.position >= (double) trimmedLength)
        {
            voice.active = false;
            assignVoiceSample (voice, nullptr);
            continue;
        }

        const double u = juce::jlimit (0.0, 1.0, voice.position / (double) trimmedLength);
        const double baseSpeed = (voice.sample->sourceSampleRate / currentSampleRate)
                               * std::pow (2.0, (double) p.pitchSemitones / 12.0);
        const double pogoSemitones = (double) p.pogo * 24.0 * std::pow (1.0 - u, 2.5);
        const double speed = baseSpeed * std::pow (2.0, pogoSemitones / 12.0);

        const double rawSamplePos = p.reverse
            ? ((double) (trimEndIdx - 1) - voice.position)
            : ((double) trimStartIdx + voice.position);
        const double clampedPos = juce::jlimit ((double) trimStartIdx, (double) (trimEndIdx - 1), rawSamplePos);

        const int index0 = juce::jlimit (trimStartIdx, trimEndIdx - 1, (int) std::floor (clampedPos));
        const int index1 = juce::jmin (trimEndIdx - 1, index0 + 1);
        const float fraction = (float) juce::jlimit (0.0, 1.0, clampedPos - (double) index0);

        const float rawL = audio.getSample (0, index0)
                           + (audio.getSample (0, index1) - audio.getSample (0, index0)) * fraction;
        const float rawR = audio.getSample (1, index0)
                           + (audio.getSample (1, index1) - audio.getSample (1, index0)) * fraction;

        // Derive envelope timing from base speed so dynamic Pogo bends never warp ADSR schedules.
        const float safeBaseSpeed = (float) juce::jmax (0.0001, baseSpeed);
        const float totalOutputSamples = (float) trimmedLength / safeBaseSpeed;
        const float elapsedSamplesBase = (float) (voice.position / safeBaseSpeed);
        const float remainingSamplesBase = (float) (((double) trimmedLength - voice.position) / safeBaseSpeed);
        const float totalDurationMs = (totalOutputSamples / (float) currentSampleRate) * 1000.0f;
        const float elapsedMs = (elapsedSamplesBase / (float) currentSampleRate) * 1000.0f;
        const float remainingMs = (remainingSamplesBase / (float) currentSampleRate) * 1000.0f;

        float envelope = 1.0f;
        if (p.envEnabled)
        {
            const auto sched = computeEnvelopeSchedule (p, totalDurationMs);
            if (remainingMs <= sched.releaseMs)
            {
                if (! voice.inRelease)
                {
                    voice.releaseStartLevel = envelopeLevelAtMs (p, sched, elapsedMs);
                    voice.inRelease = true;
                }

                const float releaseProgress = juce::jlimit (0.0f, 1.0f, 1.0f - remainingMs / juce::jmax (0.1f, sched.releaseMs));
                envelope = flSegmentLevel (releaseProgress, voice.releaseStartLevel, 0.0f,
                                           p.releaseCurve, p.releaseTension);
            }
            else
            {
                envelope = envelopeLevelAtMs (p, sched, elapsedMs);
            }
        }

        const float fadeInSamples = p.fadeInMs * 0.001f * (float) currentSampleRate;
        const float fadeOutSamples = p.fadeOutMs * 0.001f * (float) currentSampleRate;
        const float fadeInGain = fadeInSamples > 0.5f
            ? juce::jlimit (0.0f, 1.0f, (float) voice.ageSamples / fadeInSamples)
            : 1.0f;
        const float fadeOutGain = fadeOutSamples > 0.5f
            ? juce::jlimit (0.0f, 1.0f, remainingSamplesBase / fadeOutSamples)
            : 1.0f;

        const float gain = p.gain * envelope * fadeInGain * fadeOutGain;
        left += rawL * gain * leftPan;
        right += rawR * gain * rightPan;

        voice.position += speed;
        ++voice.ageSamples;

        if (voice.position >= (double) trimmedLength)
        {
            voice.active = false;
            assignVoiceSample (voice, nullptr);
        }
    }

    if (layerHighPassActive[(size_t) layer])
    {
        left = layerHighPass[(size_t) layer].processSample (0, left);
        right = layerHighPass[(size_t) layer].processSample (1, right);
    }

    if (layerLowPassActive[(size_t) layer])
    {
        left = layerLowPass[(size_t) layer].processSample (0, left);
        right = layerLowPass[(size_t) layer].processSample (1, right);
    }

    return (left + right) * 0.5f;
}

void LayeredSampleEngine::processBlock (const juce::AudioBuffer<float>& triggerInput,
                                        juce::AudioBuffer<float>& masterOutput,
                                        const std::array<juce::AudioBuffer<float>*, kNumLayers>& layerOutputs,
                                        const juce::MidiBuffer& midi,
                                        bool manualTrigger) noexcept
{
    const int numSamples = juce::jmin (triggerInput.getNumSamples(), masterOutput.getNumSamples());
    const int inputChannels = triggerInput.getNumChannels();
    const int outputChannels = masterOutput.getNumChannels();

    if (numSamples <= 0 || inputChannels <= 0 || outputChannels <= 0
        || delayLeft.empty() || delayRight.empty())
    {
        completedAudioBlocks.fetch_add (1, std::memory_order_release);
        return;
    }

    for (int layer = 0; layer < kNumLayers; ++layer)
    {
        if (clearRequests[(size_t) layer].exchange (false, std::memory_order_acq_rel))
        {
            for (auto& voice : voices[(size_t) layer])
            {
                assignVoiceSample (voice, nullptr);
                voice.position = 0.0;
                voice.delayRemaining = 0;
                voice.ageSamples = 0;
                voice.releaseStartLevel = 1.0f;
                voice.inRelease = false;
                voice.active = false;
            }
            nextVoiceToSteal[(size_t) layer] = 0;
        }
        if (previewRequests[(size_t) layer].exchange (false, std::memory_order_acq_rel))
            startLayer (layer, true);
    }

    for (auto& buffer : layerBuffers)
    {
        if (numSamples > buffer.getNumSamples())
        {
            completedAudioBlocks.fetch_add (1, std::memory_order_release);
            return;
        }

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
        bool externalTriggeredThisSample = false;

        if (manualTrigger && sample == 0)
        {
            triggerAllLayers();
            externalTriggeredThisSample = true;
        }

        while (hasMidiEvent && midiPosition <= sample)
        {
            if (midiEnabled && midiMessage.isNoteOn() && ! externalTriggeredThisSample)
            {
                triggerAllLayers();
                externalTriggeredThisSample = true;
            }

            hasMidiEvent = midiIterator.getNextEvent (midiMessage, midiPosition);
        }

        const float inputL = triggerInput.getSample (0, sample);
        const float inputR = inputChannels > 1 ? triggerInput.getSample (1, sample) : inputL;
        const float inputLevel = juce::jmax (std::abs (inputL), std::abs (inputR));

        if (audioEnabled && ! externalTriggeredThisSample && refractoryRemaining <= 0
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

    completedAudioBlocks.fetch_add (1, std::memory_order_release);
}

const juce::AudioBuffer<float>& LayeredSampleEngine::getLayerBuffer (int layer) const noexcept
{
    jassert (juce::isPositiveAndBelow (layer, kNumLayers));
    return layerBuffers[(size_t) juce::jlimit (0, kNumLayers - 1, layer)];
}
