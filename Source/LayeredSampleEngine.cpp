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
    }
}

void LayeredSampleEngine::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    currentSampleRate = juce::jmax (1.0, sampleRate);
    maximumDelaySamples = juce::jmax (1, juce::roundToInt (currentSampleRate * kMaximumPreRollMilliseconds * 0.001));
    preRollSamples = juce::jlimit (0, maximumDelaySamples,
                                   juce::roundToInt (currentSampleRate * kDefaultPreRollMilliseconds * 0.001));
    refractorySamples = juce::jmax (1, juce::roundToInt (currentSampleRate * 0.025));
    refractoryRemaining = 0;
    previousInputLevel = 0.0f;

    const auto delaySize = (size_t) (maximumDelaySamples + 1);
    delayLeft.assign (delaySize, 0.0f);
    delayRight.assign (delaySize, 0.0f);
    delayWritePosition = 0;

    for (int curve = 0; curve < 7; ++curve)
        for (int i = 0; i <= 256; ++i)
        {
            const float t = (float) i / 256.0f;
            float value = t;

            switch (curve)
            {
                case 0: value = t; break; // Linear
                case 1: value = t * t; break; // Convex / slow start
                case 2: value = 1.0f - (1.0f - t) * (1.0f - t); break; // Concave / fast start
                case 3: value = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); break; // S curve
                case 4: value = (std::exp (5.0f * t) - 1.0f) / (std::exp (5.0f) - 1.0f); break;
                case 5: value = std::log1p (9.0f * t) / std::log (10.0f); break;
                case 6:
                {
                    // Cubic Bezier from (0,0) to (1,1), control points (0.22,0.02) and (0.42,0.98).
                    float u = t;
                    for (int iteration = 0; iteration < 6; ++iteration)
                    {
                        const float oneMinus = 1.0f - u;
                        const float x = 3.0f * oneMinus * oneMinus * u * 0.22f
                                        + 3.0f * oneMinus * u * u * 0.42f + u * u * u;
                        const float dx = 3.0f * oneMinus * oneMinus * 0.22f
                                         + 6.0f * oneMinus * u * (0.42f - 0.22f)
                                         + 3.0f * u * u * (1.0f - 0.42f);
                        if (std::abs (dx) > 1.0e-5f)
                            u = juce::jlimit (0.0f, 1.0f, u + (t - x) / dx);
                    }
                    const float oneMinus = 1.0f - u;
                    value = 3.0f * oneMinus * oneMinus * u * 0.02f
                            + 3.0f * oneMinus * u * u * 0.98f + u * u * u;
                    break;
                }
                default: break;
            }

            curveTables[(size_t) curve][(size_t) i] = juce::jlimit (0.0f, 1.0f, value);
        }

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
        const int first = (int) ((juce::int64) point * sampleCount / kPreviewPoints);
        const int last = juce::jmax (first + 1, (int) ((juce::int64) (point + 1) * sampleCount / kPreviewPoints));
        float minimum = 1.0f, maximum = -1.0f;
        for (int sample = first; sample < juce::jmin (sampleCount, last); ++sample)
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
    sampleNames[(size_t) layer] = raw->displayName;
    currentSamples[(size_t) layer].store (raw, std::memory_order_release);

    error.clear();
    return true;
}

void LayeredSampleEngine::clearSample (int layer)
{
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
        return;

    currentSamples[(size_t) layer].store (nullptr, std::memory_order_release);
    clearRequests[(size_t) layer].store (true, std::memory_order_release);
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
    target.attackCurve = juce::jlimit (0, 6, p.attackCurve);
    target.decayCurve = juce::jlimit (0, 6, p.decayCurve);
    target.releaseCurve = juce::jlimit (0, 6, p.releaseCurve);
    target.muted = p.muted;
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
    if (sample == nullptr || parameters[(size_t) layer].muted)
        return;

    auto& layerVoices = voices[(size_t) layer];

    // Prefer a free voice. If all four are active, steal slots round-robin so a
    // new hit does not automatically cut off the immediately preceding hit.
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
    // The host sees a fixed 100 ms lookahead. Each layer can move independently
    // around the delayed dry transient: negative values advance it, positive values delay it.
    const int shiftSamples = ignoreTimingShift ? 0
        : juce::roundToInt (parameters[(size_t) layer].delayShiftMs * (float) currentSampleRate * 0.001f);
    voice.delayRemaining = juce::jlimit (0, maximumDelaySamples * 2, maximumDelaySamples + shiftSamples);
    voice.ageSamples = 0;
    voice.releaseStartLevel = 1.0f;
    voice.inRelease = false;
    voice.active = true;
}

float LayeredSampleEngine::applyCurve (float progress, int curve) const noexcept
{
    const float p = juce::jlimit (0.0f, 1.0f, progress);
    const int index = juce::jlimit (0, 6, curve);
    const float tablePosition = p * 256.0f;
    const int lower = juce::jlimit (0, 255, (int) tablePosition);
    const float fraction = tablePosition - (float) lower;
    const auto& table = curveTables[(size_t) index];
    return table[(size_t) lower]
           + (table[(size_t) (lower + 1)] - table[(size_t) lower]) * fraction;
}

float LayeredSampleEngine::envelopeLevelAtAge (const LayerParameters& p,
                                                int age) const noexcept
{
    const float attackSamples = juce::jmax (1.0f, p.attackMs * 0.001f * (float) currentSampleRate);
    const float decaySamples = juce::jmax (1.0f, p.decayMs * 0.001f * (float) currentSampleRate);

    if ((float) age < attackSamples)
        return applyCurve ((float) age / attackSamples, p.attackCurve);

    const float holdSamples = p.holdMs * 0.001f * (float) currentSampleRate;
    if ((float) age < attackSamples + holdSamples)
        return 1.0f;

    const float decayProgress = ((float) age - attackSamples - holdSamples) / decaySamples;
    if (decayProgress < 1.0f)
        return 1.0f + (p.sustain - 1.0f) * applyCurve (decayProgress, p.decayCurve);

    return p.sustain;
}

float LayeredSampleEngine::renderLayerSample (int layer, float& left, float& right) noexcept
{
    left = right = 0.0f;
    if (! juce::isPositiveAndBelow (layer, kNumLayers))
        return 0.0f;

    const auto& p = parameters[(size_t) layer];
    auto& layerVoices = voices[(size_t) layer];

    if (p.muted)
    {
        for (auto& voice : layerVoices)
            voice.active = false;
    }
    else
    {
        const float leftPan = p.pan > 0.0f ? 1.0f - p.pan : 1.0f;
        const float rightPan = p.pan < 0.0f ? 1.0f + p.pan : 1.0f;

        // Mix active voices for this slot first, then run its filters once on the
        // combined signal. This preserves independent filtering per layer.
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

            if (length <= 0 || voice.position >= (double) length)
            {
                voice.active = false;
                continue;
            }

            const double speed = (voice.sample->sourceSampleRate / currentSampleRate)
                                 * std::pow (2.0, (double) p.pitchSemitones / 12.0);
            const int index0 = juce::jlimit (0, length - 1, (int) voice.position);
            const int index1 = juce::jmin (length - 1, index0 + 1);
            const float fraction = (float) (voice.position - (double) index0);

            const float rawL = audio.getSample (0, index0)
                               + (audio.getSample (0, index1) - audio.getSample (0, index0)) * fraction;
            const float rawR = audio.getSample (1, index0)
                               + (audio.getSample (1, index1) - audio.getSample (1, index0)) * fraction;

            const float totalOutputSamples = (float) length / (float) juce::jmax (0.0001, speed);
            const float remainingSamples = (float) ((double) length - voice.position)
                                           / (float) juce::jmax (0.0001, speed);
            const float requestedReleaseSamples = juce::jmax (1.0f, p.releaseMs * 0.001f * (float) currentSampleRate);
            // Very short one-shots cannot contain a full release segment. Reserve at most
            // the final half of the file for release so the attack portion is not muted.
            const float releaseSamples = juce::jmin (requestedReleaseSamples,
                                                      juce::jmax (1.0f, totalOutputSamples * 0.5f));

            float envelope = envelopeLevelAtAge (p, voice.ageSamples);

            // In one-shot mode, RELEASE is placed over the sample's final releaseMs.
            if (remainingSamples <= releaseSamples)
            {
                if (! voice.inRelease)
                {
                    voice.releaseStartLevel = envelope;
                    voice.inRelease = true;
                }

                const float releaseProgress = 1.0f - remainingSamples / releaseSamples;
                envelope = voice.releaseStartLevel
                           * (1.0f - applyCurve (releaseProgress, p.releaseCurve));
            }

            const float gain = p.gain * envelope;
            left += rawL * gain * leftPan;
            right += rawR * gain * rightPan;

            voice.position += speed;
            ++voice.ageSamples;

            if (voice.position >= (double) length)
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
