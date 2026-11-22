#include "LayeredSampleEngine.h"

#include <cmath>

LayeredSampleEngine::LayeredSampleEngine()
{
    formatManager.registerBasicFormats();

    for (int i = 0; i < kNumLayers; ++i)
    {
        currentSamples[(size_t) i].store (nullptr, std::memory_order_relaxed);
        clearRequests[(size_t) i].store (false, std::memory_order_relaxed);
    }
}

void LayeredSampleEngine::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    currentSampleRate = juce::jmax (1.0, sampleRate);
    preRollSamples = juce::jmax (1, juce::roundToInt (currentSampleRate * kPreRollMilliseconds * 0.001));
    refractorySamples = juce::jmax (1, juce::roundToInt (currentSampleRate * 0.025));
    refractoryRemaining = 0;
    previousInputLevel = 0.0f;

    const auto delaySize = (size_t) (preRollSamples + 1);
    delayLeft.assign (delaySize, 0.0f);
    delayRight.assign (delaySize, 0.0f);
    delayWritePosition = 0;

    for (auto& buffer : layerBuffers)
    {
        buffer.setSize (2, juce::jmax (1, maximumBlockSize), false, true, false);
        buffer.clear();
    }

    for (auto& voice : voices)
        voice = {};
    for (auto& request : clearRequests)
        request.store (false, std::memory_order_relaxed);
}

void LayeredSampleEngine::setTriggerMode (int mode, float newThreshold) noexcept
{
    triggerMode = juce::jlimit (0, 2, mode);
    threshold = juce::jlimit (0.001f, 1.0f, newThreshold);
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
    target.attackMs = juce::jlimit (0.1f, 2000.0f, p.attackMs);
    target.decayMs = juce::jlimit (0.1f, 5000.0f, p.decayMs);
    target.sustain = juce::jlimit (0.0f, 1.0f, p.sustain);
    target.releaseMs = juce::jlimit (0.1f, 10000.0f, p.releaseMs);
    target.muted = p.muted;
}

void LayeredSampleEngine::triggerAllLayers() noexcept
{
    for (int i = 0; i < kNumLayers; ++i)
        startLayer (i);
}

void LayeredSampleEngine::startLayer (int layer) noexcept
{
    auto* sample = currentSamples[(size_t) layer].load (std::memory_order_acquire);
    if (sample == nullptr || parameters[(size_t) layer].muted)
        return;

    auto& voice = voices[(size_t) layer];
    voice.sample = sample;
    voice.position = 0.0;
    voice.delayRemaining = layer < kPreLayers ? 0 : preRollSamples;
    voice.ageSamples = 0;
    voice.active = true;
}

float LayeredSampleEngine::renderLayerSample (int layer, float& left, float& right) noexcept
{
    left = right = 0.0f;
    auto& voice = voices[(size_t) layer];

    if (! voice.active || voice.sample == nullptr)
        return 0.0f;

    if (voice.delayRemaining > 0)
    {
        --voice.delayRemaining;
        return 0.0f;
    }

    const auto& p = parameters[(size_t) layer];
    if (p.muted)
    {
        voice.active = false;
        return 0.0f;
    }

    const auto& audio = voice.sample->audio;
    const int length = audio.getNumSamples();

    if (length <= 0 || voice.position >= (double) (length - 1))
    {
        voice.active = false;
        return 0.0f;
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

    const float attackSamples = juce::jmax (1.0f, p.attackMs * 0.001f * (float) currentSampleRate);
    const float decaySamples = juce::jmax (1.0f, p.decayMs * 0.001f * (float) currentSampleRate);
    float envelope = 1.0f;

    if ((float) voice.ageSamples < attackSamples)
        envelope = (float) voice.ageSamples / attackSamples;
    else if ((float) voice.ageSamples < attackSamples + decaySamples)
    {
        const float decayPosition = ((float) voice.ageSamples - attackSamples) / decaySamples;
        envelope = 1.0f + (p.sustain - 1.0f) * juce::jlimit (0.0f, 1.0f, decayPosition);
    }
    else
        envelope = p.sustain;

    const float releaseSamples = juce::jmax (1.0f, p.releaseMs * 0.001f * (float) currentSampleRate);
    const float remaining = (float) ((double) (length - 1) - voice.position) / (float) juce::jmax (0.0001, speed);
    const float releaseGain = juce::jlimit (0.0f, 1.0f, remaining / releaseSamples);
    envelope *= releaseGain;

    const float leftPan = p.pan > 0.0f ? 1.0f - p.pan : 1.0f;
    const float rightPan = p.pan < 0.0f ? 1.0f + p.pan : 1.0f;
    const float gain = p.gain * envelope;

    left = rawL * gain * leftPan;
    right = rawR * gain * rightPan;

    voice.position += speed;
    ++voice.ageSamples;

    if (voice.position >= (double) (length - 1))
        voice.active = false;

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
        if (clearRequests[(size_t) layer].exchange (false, std::memory_order_acq_rel))
            voices[(size_t) layer] = {};

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
        const int readPosition = (delayWritePosition + (int) delayLeft.size() - preRollSamples)
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
