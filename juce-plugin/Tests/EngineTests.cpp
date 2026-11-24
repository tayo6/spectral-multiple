#include "LayeredSampleEngine.h"
#include "StateMigration.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace
{
int gFailures = 0;

void check (bool cond, const std::string& name, const std::string& detail = {})
{
    if (! cond)
    {
        ++gFailures;
        std::cerr << "[FAIL] " << name;
        if (! detail.empty())
            std::cerr << " -> " << detail;
        std::cerr << "\n";
    }
}

void writeU16LE (std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back ((uint8_t) (v & 0xff));
    out.push_back ((uint8_t) ((v >> 8) & 0xff));
}

void writeU32LE (std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back ((uint8_t) (v & 0xff));
    out.push_back ((uint8_t) ((v >> 8) & 0xff));
    out.push_back ((uint8_t) ((v >> 16) & 0xff));
    out.push_back ((uint8_t) ((v >> 24) & 0xff));
}

void writeU16BE (std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back ((uint8_t) ((v >> 8) & 0xff));
    out.push_back ((uint8_t) (v & 0xff));
}

void writeU32BE (std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back ((uint8_t) ((v >> 24) & 0xff));
    out.push_back ((uint8_t) ((v >> 16) & 0xff));
    out.push_back ((uint8_t) ((v >> 8) & 0xff));
    out.push_back ((uint8_t) (v & 0xff));
}

std::string writeWavFile16 (const fs::path& p, double sr, const std::vector<std::vector<float>>& chData)
{
    const uint16_t ch = (uint16_t) chData.size();
    const uint32_t frames = (uint32_t) chData[0].size();
    const uint32_t dataSize = frames * ch * 2;
    std::vector<uint8_t> bytes;
    bytes.reserve (44 + dataSize);

    bytes.insert (bytes.end(), { 'R', 'I', 'F', 'F' });
    writeU32LE (bytes, 36 + dataSize);
    bytes.insert (bytes.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    writeU32LE (bytes, 16);
    writeU16LE (bytes, 1); // PCM
    writeU16LE (bytes, ch);
    writeU32LE (bytes, (uint32_t) sr);
    writeU32LE (bytes, (uint32_t) sr * ch * 2);
    writeU16LE (bytes, (uint16_t) (ch * 2));
    writeU16LE (bytes, 16);
    bytes.insert (bytes.end(), { 'd', 'a', 't', 'a' });
    writeU32LE (bytes, dataSize);

    for (uint32_t i = 0; i < frames; ++i)
    {
        for (uint16_t c = 0; c < ch; ++c)
        {
            const float clamped = std::max (-1.0f, std::min (1.0f, chData[c][i]));
            const int16_t s = (int16_t) std::llround (clamped * 32767.0f);
            writeU16LE (bytes, (uint16_t) s);
        }
    }

    std::ofstream f (p, std::ios::binary);
    f.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
    return p.string();
}

std::string writeWavFileFloat32 (const fs::path& p, double sr, const std::vector<std::vector<float>>& chData)
{
    const uint16_t ch = (uint16_t) chData.size();
    const uint32_t frames = (uint32_t) chData[0].size();
    const uint32_t dataSize = frames * ch * 4;
    std::vector<uint8_t> bytes;
    bytes.reserve (44 + dataSize);

    bytes.insert (bytes.end(), { 'R', 'I', 'F', 'F' });
    writeU32LE (bytes, 36 + dataSize);
    bytes.insert (bytes.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    writeU32LE (bytes, 16);
    writeU16LE (bytes, 3); // IEEE float
    writeU16LE (bytes, ch);
    writeU32LE (bytes, (uint32_t) sr);
    writeU32LE (bytes, (uint32_t) sr * ch * 4);
    writeU16LE (bytes, (uint16_t) (ch * 4));
    writeU16LE (bytes, 32);
    bytes.insert (bytes.end(), { 'd', 'a', 't', 'a' });
    writeU32LE (bytes, dataSize);

    for (uint32_t i = 0; i < frames; ++i)
    {
        for (uint16_t c = 0; c < ch; ++c)
        {
            uint32_t bits = 0;
            std::memcpy (&bits, &chData[c][i], 4);
            writeU32LE (bytes, bits);
        }
    }

    std::ofstream f (p, std::ios::binary);
    f.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
    return p.string();
}

std::string writeAiffFile16 (const fs::path& p, double sr, const std::vector<std::vector<float>>& chData)
{
    const uint16_t ch = (uint16_t) chData.size();
    const uint32_t frames = (uint32_t) chData[0].size();
    const uint32_t pcmBytes = frames * ch * 2;
    const uint32_t ssndPayload = 8 + pcmBytes;
    const uint32_t commPayload = 18;
    const uint32_t formPayload = 4 + (8 + commPayload) + (8 + ssndPayload);

    std::vector<uint8_t> bytes;
    bytes.reserve (8 + formPayload);
    bytes.insert (bytes.end(), { 'F', 'O', 'R', 'M' });
    writeU32BE (bytes, formPayload);
    bytes.insert (bytes.end(), { 'A', 'I', 'F', 'F' });

    // COMM chunk
    bytes.insert (bytes.end(), { 'C', 'O', 'M', 'M' });
    writeU32BE (bytes, commPayload);
    writeU16BE (bytes, ch);
    writeU32BE (bytes, frames);
    writeU16BE (bytes, 16);

    // Encode sample rate as 80-bit IEEE extended float
    int exp2 = 0;
    double mant = std::frexp (sr, &exp2);
    uint16_t ieeeExp = (uint16_t) (16383 + exp2 - 1);
    uint64_t ieeeMant = (uint64_t) std::llround (mant * (double) (1ULL << 63) * 2.0);
    if (mant < 1.0)
        ieeeMant = (uint64_t) std::llround (mant * (double) (1ULL << 63) * 2.0);
    // frexp returns [0.5, 1.0); multiplying by 2^64 puts integer bit at bit 63
    ieeeMant = (uint64_t) std::llround (std::ldexp (mant, 64));
    writeU16BE (bytes, ieeeExp);
    for (int shift = 56; shift >= 0; shift -= 8)
        bytes.push_back ((uint8_t) ((ieeeMant >> shift) & 0xff));

    // SSND chunk
    bytes.insert (bytes.end(), { 'S', 'S', 'N', 'D' });
    writeU32BE (bytes, ssndPayload);
    writeU32BE (bytes, 0); // offset
    writeU32BE (bytes, 0); // blockSize
    for (uint32_t i = 0; i < frames; ++i)
    {
        for (uint16_t c = 0; c < ch; ++c)
        {
            const float clamped = std::max (-1.0f, std::min (1.0f, chData[c][i]));
            const int16_t s = (int16_t) std::llround (clamped * 32767.0f);
            writeU16BE (bytes, (uint16_t) s);
        }
    }

    std::ofstream f (p, std::ios::binary);
    f.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
    return p.string();
}

int findFirstExceeding (const std::vector<float>& buf, float thresh, int startIdx = 0)
{
    for (int i = startIdx; i < (int) buf.size(); ++i)
        if (std::abs (buf[(size_t) i]) >= thresh)
            return i;
    return -1;
}

double computeRms (const std::vector<float>& buf, int startIdx = 0, int count = -1)
{
    const int end = (count < 0) ? (int) buf.size() : std::min ((int) buf.size(), startIdx + count);
    if (end <= startIdx)
        return 0.0;
    double sumSq = 0.0;
    for (int i = startIdx; i < end; ++i)
        sumSq += (double) buf[(size_t) i] * (double) buf[(size_t) i];
    return std::sqrt (sumSq / (double) (end - startIdx));
}

struct CapturedOutputs
{
    std::vector<float> masterL, masterR;
    std::array<std::vector<float>, LayeredSampleEngine::kNumLayers> busL, busR;
};

CapturedOutputs runBlocks (LayeredSampleEngine& engine,
                           int totalSamples,
                           int blockSize,
                           const std::vector<float>& trigInL,
                           const std::vector<float>& trigInR,
                           const std::vector<std::pair<int, juce::MidiMessage>>& midiEvents,
                           int manualTriggerBlock = -1,
                           const std::array<bool, LayeredSampleEngine::kNumLayers>& connectBus = { true, true, true, true, true, true })
{
    CapturedOutputs out;
    out.masterL.assign ((size_t) totalSamples, 0.0f);
    out.masterR.assign ((size_t) totalSamples, 0.0f);
    for (int l = 0; l < LayeredSampleEngine::kNumLayers; ++l)
    {
        out.busL[(size_t) l].assign ((size_t) totalSamples, 0.0f);
        out.busR[(size_t) l].assign ((size_t) totalSamples, 0.0f);
    }

    int blockIdx = 0;
    for (int pos = 0; pos < totalSamples; pos += blockSize, ++blockIdx)
    {
        const int n = std::min (blockSize, totalSamples - pos);
        juce::AudioBuffer<float> inBuf (2, n);
        juce::AudioBuffer<float> masterBuf (2, n);
        std::array<juce::AudioBuffer<float>, LayeredSampleEngine::kNumLayers> layerBufs;
        std::array<juce::AudioBuffer<float>*, LayeredSampleEngine::kNumLayers> layerPtrs {};

        for (int i = 0; i < n; ++i)
        {
            inBuf.setSample (0, i, (size_t) (pos + i) < trigInL.size() ? trigInL[(size_t) (pos + i)] : 0.0f);
            inBuf.setSample (1, i, (size_t) (pos + i) < trigInR.size() ? trigInR[(size_t) (pos + i)] : 0.0f);
        }

        for (int l = 0; l < LayeredSampleEngine::kNumLayers; ++l)
        {
            if (connectBus[(size_t) l])
            {
                layerBufs[(size_t) l].setSize (2, n);
                layerPtrs[(size_t) l] = &layerBufs[(size_t) l];
            }
        }

        juce::MidiBuffer midi;
        for (const auto& ev : midiEvents)
            if (ev.first >= pos && ev.first < pos + n)
                midi.addEvent (ev.second, ev.first - pos);

        engine.processBlock (inBuf, masterBuf, layerPtrs, midi, blockIdx == manualTriggerBlock);

        for (int i = 0; i < n; ++i)
        {
            out.masterL[(size_t) (pos + i)] = masterBuf.getSample (0, i);
            out.masterR[(size_t) (pos + i)] = masterBuf.getSample (1, i);
            for (int l = 0; l < LayeredSampleEngine::kNumLayers; ++l)
            {
                if (connectBus[(size_t) l])
                {
                    out.busL[(size_t) l][(size_t) (pos + i)] = layerBufs[(size_t) l].getSample (0, i);
                    out.busR[(size_t) l][(size_t) (pos + i)] = layerBufs[(size_t) l].getSample (1, i);
                }
            }
        }
    }
    return out;
}

//==============================================================================
void testLoadingMonoStereoAndEdgeCases (const fs::path& dir)
{
    LayeredSampleEngine engine;
    engine.prepareToPlay (44100.0, 256);

    juce::String err;
    check (! engine.loadSampleFromFile (0, juce::File ((dir / "missing.wav").string()), err),
           "P1: missing file fails", err.toStdString());
    check (err.isNotEmpty(), "P1: missing file sets error");

    check (! engine.loadSampleFromFile (-1, juce::File ((dir / "missing.wav").string()), err),
           "P1: negative layer rejected");

    // Mono WAV
    const auto monoWav = writeWavFile16 (dir / "mono.wav", 44100.0, { { 0.8f, -0.4f, 0.2f, 0.0f } });
    check (engine.loadSampleFromFile (0, juce::File (monoWav), err), "P1: mono WAV loads", err.toStdString());
    check (engine.getSampleName (0) == "mono", "P1: sample name updated");

    // Stereo float WAV
    const auto stereoWav = writeWavFileFloat32 (dir / "stereo32.wav", 48000.0, {
        { 0.9f, 0.5f, 0.1f, 0.0f },
        { -0.9f, -0.5f, -0.1f, 0.0f }
    });
    check (engine.loadSampleFromFile (1, juce::File (stereoWav), err), "P1: stereo float32 WAV loads", err.toStdString());

    // Stereo AIFF
    const auto stereoAiff = writeAiffFile16 (dir / "stereo.aif", 44100.0, {
        { 0.7f, 0.3f, 0.0f, 0.0f },
        { -0.2f, 0.6f, 0.0f, 0.0f }
    });
    check (engine.loadSampleFromFile (2, juce::File (stereoAiff), err), "P1: stereo AIFF loads", err.toStdString());

    // 1-sample WAV edge case
    const auto oneSampleWav = writeWavFileFloat32 (dir / "onesample.wav", 44100.0, { { 0.75f } });
    check (engine.loadSampleFromFile (3, juce::File (oneSampleWav), err), "P1: 1-sample WAV loads", err.toStdString());

    check (engine.getLoadedMask() == 0x0f, "P1: loaded mask matches layers 0..3");

    engine.setTriggerMode (1, 0.05f); // MIDI
    engine.setPreRollMilliseconds (0.0f);
    std::vector<float> zero (8192, 0.0f);
    auto out = runBlocks (engine, 8192, 256, zero, zero, { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });
    const int dMax = engine.getMaximumDelaySamples();

    check (std::abs (out.busL[0][(size_t) dMax] - 0.8f) < 0.002f
        && std::abs (out.busR[0][(size_t) dMax] - 0.8f) < 0.002f,
        "P1: mono file duplicated to L and R");
    check (std::abs (out.busL[1][(size_t) dMax] - 0.9f) < 0.002f
        && std::abs (out.busR[1][(size_t) dMax] + 0.9f) < 0.002f,
        "P1: stereo float32 WAV preserves distinct L and R");
    check (std::abs (out.busL[2][(size_t) dMax] - 0.7f) < 0.002f
        && std::abs (out.busR[2][(size_t) dMax] + 0.2f) < 0.002f,
        "P1: stereo AIFF preserves distinct L and R");
    check (std::abs (out.busL[3][(size_t) dMax] - 0.75f) < 0.002f,
        "P1: 1-sample WAV plays cleanly without silence/oob");
}

void testQsbrMemoryReclamation (const fs::path& dir)
{
    LayeredSampleEngine engine;
    engine.prepareToPlay (44100.0, 256);
    engine.setTriggerMode (1, 0.05f);
    engine.setPreRollMilliseconds (0.0f);

    std::vector<float> tone (4410, 0.5f); // 100 ms sample
    const auto wavPath = writeWavFileFloat32 (dir / "qsbr.wav", 44100.0, { tone, tone });

    juce::String err;
    check (engine.loadSampleFromFile (0, juce::File (wavPath), err), "P1-QSBR: initial load");

    // Trigger voice on old sample, then replace mid-flight!
    std::vector<float> zero (256, 0.0f);
    runBlocks (engine, 256, 256, zero, zero, { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });
    // Now voice is in delay / active queue. Replace 5 times while rendering past sample end.
    for (int i = 0; i < 5; ++i)
        check (engine.loadSampleFromFile (0, juce::File (wavPath), err), "P1-QSBR: reload while playing");

    // Advance 12000 samples (> 100 ms lookahead + 100 ms playback) so old voice finishes
    std::vector<float> zeroLong (12000, 0.0f);
    runBlocks (engine, 12000, 256, zeroLong, zeroLong, {});

    check (engine.getRetiredSampleCount (0) == 0,
           "P1-QSBR: retired samples reclaimed cleanly after voices finish",
           "remaining=" + std::to_string (engine.getRetiredSampleCount (0)));

    // Repeat load + clear 40 times without active voices
    for (int i = 0; i < 40; ++i)
    {
        engine.loadSampleFromFile (0, juce::File (wavPath), err);
        runBlocks (engine, 256, 256, zero, zero, {});
        engine.clearSample (0);
    }
    // Allow one subsequent audio callback to complete (QSBR grace period) before final reclamation check
    runBlocks (engine, 256, 256, zero, zero, {});
    check (engine.getRetiredSampleCount (0) == 0,
           "P1-QSBR: 40x load/clear cycles retain zero stale buffers",
           "remaining=" + std::to_string (engine.getRetiredSampleCount (0)));
}

void testReverseAndFractionalInterpolation (const fs::path& dir)
{
    LayeredSampleEngine engine;
    engine.prepareToPlay (44100.0, 256);
    engine.setTriggerMode (1, 0.05f);
    engine.setPreRollMilliseconds (0.0f);

    // Linear ramp 0.0 -> 1.0 over 101 samples so exact linear interpolation matches analytical values
    const int N = 101;
    std::vector<float> ramp (N);
    for (int i = 0; i < N; ++i)
        ramp[(size_t) i] = (float) i / 100.0f;

    const auto wav = writeWavFileFloat32 (dir / "ramp.wav", 44100.0, { ramp, ramp });
    juce::String err;
    check (engine.loadSampleFromFile (3, juce::File (wav), err), "P1-Interp: load ramp");

    // Fractional non-integer speed (~ +5 semitones => speed = 2^(5/12) ≈ 1.33483985)
    LayeredSampleEngine::LayerParameters p;
    p.pitchSemitones = 5.0f;
    p.reverse = false;
    engine.setLayerParameters (3, p);

    const int dMax = engine.getMaximumDelaySamples();
    std::vector<float> zero (8192, 0.0f);
    auto fwd = runBlocks (engine, 8192, 256, zero, zero, { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });

    const double speed = std::pow (2.0, 5.0 / 12.0);
    double maxErrFwd = 0.0;
    for (int k = 0; k * speed < N - 1; ++k)
    {
        const double expected = (k * speed) / 100.0;
        const double actual = fwd.busL[3][(size_t) (dMax + k)];
        maxErrFwd = std::max (maxErrFwd, std::abs (expected - actual));
    }
    check (maxErrFwd < 1.0e-5, "P1-Interp: forward fractional interpolation exact on linear ramp",
           "maxErr=" + std::to_string (maxErrFwd));

    // Reverse mode with fractional speed
    engine.prepareToPlay (44100.0, 256);
    p.reverse = true;
    engine.setLayerParameters (3, p);
    auto rev = runBlocks (engine, 8192, 256, zero, zero, { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });

    double maxErrRev = 0.0;
    float minVal = 10.0f, maxVal = -10.0f;
    for (int k = 0; k * speed < N - 1; ++k)
    {
        const float actual = rev.busL[3][(size_t) (dMax + k)];
        minVal = std::min (minVal, actual);
        maxVal = std::max (maxVal, actual);
        const double pos = (double) (N - 1) - (k * speed);
        const double expected = pos / 100.0;
        maxErrRev = std::max (maxErrRev, std::abs (expected - actual));
    }
    check (minVal >= -1.0e-6f && maxVal <= 1.0f + 1.0e-6f,
           "P1-Interp: reverse playback zero overshoot/extrapolation");
    check (maxErrRev < 1.0e-5, "P1-Interp: reverse fractional interpolation exact on linear ramp",
           "maxErr=" + std::to_string (maxErrRev));
}

void testPitchPogoTrimFadesFiltersAndPolyphony (const fs::path& dir)
{
    LayeredSampleEngine engine;
    engine.prepareToPlay (44100.0, 256);
    engine.setTriggerMode (1, 0.05f);
    engine.setPreRollMilliseconds (0.0f);

    // 4410 samples (100 ms) of +1.0 DC pulse with tag slope for trim test, plus 60Hz+8kHz mix for filters
    std::vector<float> flat (4410, 0.5f);
    std::vector<float> sloped (1000);
    for (int i = 0; i < 1000; ++i)
        sloped[(size_t) i] = (i < 250 || i >= 750) ? 0.1f : 0.8f;

    const auto flatWav = writeWavFileFloat32 (dir / "flat.wav", 44100.0, { flat, flat });
    const auto slopeWav = writeWavFileFloat32 (dir / "sloped.wav", 44100.0, { sloped, sloped });
    juce::String err;
    engine.loadSampleFromFile (3, juce::File (flatWav), err);
    engine.loadSampleFromFile (4, juce::File (slopeWav), err);

    const int dMax = engine.getMaximumDelaySamples();
    std::vector<float> zero (16384, 0.0f);

    // Trim & fades single-voice check on Layer 4
    LayeredSampleEngine::LayerParameters p4;
    p4.trimStart = 0.25f;
    p4.trimEnd = 0.75f;
    p4.fadeInMs = 2.0f;
    p4.fadeOutMs = 2.0f;
    engine.setLayerParameters (4, p4);

    auto outSingle = runBlocks (engine, 16384, 256, zero, zero, { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });
    // Trim only kept 0.8f region (500 samples), faded in and faded out smoothly
    check (std::abs (outSingle.busL[4][(size_t) dMax]) < 0.05f, "P1-Fade: fadeIn starts near zero");
    check (std::abs (outSingle.busL[4][(size_t) (dMax + 250)] - 0.8f) < 0.01f, "P1-Trim/Fade: middle plays trimmed 0.8f at unity");
    check (std::abs (outSingle.busL[4][(size_t) (dMax + 499)]) < 0.05f && std::abs (outSingle.busL[4][(size_t) (dMax + 510)]) < 1.0e-5f,
           "P1-Fade/Trim: fadeOut ends cleanly at trimmed length");

    // 4-voice polyphony + 5th stealing on Layer 3
    engine.prepareToPlay (44100.0, 256);
    LayeredSampleEngine::LayerParameters p3;
    p3.muted = false;
    engine.setLayerParameters (3, p3);

    std::vector<std::pair<int, juce::MidiMessage>> polyMidi;
    for (int i = 0; i < 5; ++i)
        polyMidi.push_back ({ i * 10, juce::MidiMessage::noteOn (1, 60, 127) });

    auto outPoly = runBlocks (engine, 16384, 256, zero, zero, polyMidi);
    const float peakPoly = *std::max_element (outPoly.busL[3].begin(), outPoly.busL[3].end());
    check (peakPoly > 1.99f && peakPoly <= 2.005f,
           "P1-Polyphony: 5th trigger steals oldest, capping at 4 voices (2.00)",
           "peak=" + std::to_string (peakPoly));

    // Pitch +12st halves sample duration from 4410 to 2205 samples
    engine.prepareToPlay (44100.0, 256);
    LayeredSampleEngine::LayerParameters pOct;
    pOct.pitchSemitones = 12.0f;
    engine.setLayerParameters (3, pOct);
    auto outOct = runBlocks (engine, 16384, 256, zero, zero, { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });
    check (std::abs (outOct.busL[3][(size_t) (dMax + 2200)] - 0.5f) < 0.01f
        && std::abs (outOct.busL[3][(size_t) (dMax + 2210)]) < 1.0e-5f,
           "P1-Pitch: +12 semitones halves playback length accurately");

    // Verify all 8 FL Studio curve modes remain bounded [0, 0.5] and release to zero
    for (int mode = 0; mode <= 7; ++mode)
    {
        engine.prepareToPlay (44100.0, 256);
        LayeredSampleEngine::LayerParameters pm;
        pm.envEnabled = true;
        pm.attackMs = 20.0f;
        pm.holdMs = 10.0f;
        pm.decayMs = 25.0f;
        pm.sustain = 0.5f;
        pm.releaseMs = 25.0f;
        pm.attackCurve = mode;
        pm.decayCurve = mode;
        pm.releaseCurve = mode;
        engine.setLayerParameters (3, pm);
        auto outMode = runBlocks (engine, 16384, 256, zero, zero, { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });
        float minM = 10.0f, maxM = -10.0f;
        for (float v : outMode.busL[3]) { minM = std::min (minM, v); maxM = std::max (maxM, v); }
        check (minM >= -1.0e-5f && maxM <= 0.5001f && std::abs (outMode.busL[3][(size_t) (dMax + 4410)]) < 1.0e-4f,
               "P1-Envelope: FL curve mode " + std::to_string (mode) + " bounded & releases cleanly");
    }

    // Envelope + Pogo stability check (monotonic attack/decay under heavy Pogo bend)
    engine.prepareToPlay (44100.0, 256);
    LayeredSampleEngine::LayerParameters pEnv;
    pEnv.envEnabled = true;
    pEnv.pogo = 0.8f;
    pEnv.attackMs = 20.0f;
    pEnv.holdMs = 10.0f;
    pEnv.decayMs = 30.0f;
    pEnv.sustain = 0.4f;
    pEnv.releaseMs = 20.0f;
    engine.setLayerParameters (3, pEnv);
    auto outEnv = runBlocks (engine, 16384, 256, zero, zero, { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });
    const float envPeak = *std::max_element (outEnv.busL[3].begin(), outEnv.busL[3].end());
    check (envPeak > 0.45f && envPeak <= 0.505f && std::abs (outEnv.busL[3][(size_t) (dMax + 4410)]) < 1.0e-5f,
           "P1-Envelope/Pogo: ADSR envelope peaks cleanly and releases smoothly under Pogo bend");

    // Filter test: neutral bypass vs active HP / LP
    engine.prepareToPlay (44100.0, 256);
    std::vector<float> imp (16, 0.0f);
    imp[0] = 1.0f;
    const auto impWav = writeWavFileFloat32 (dir / "imp.wav", 44100.0, { imp, imp });
    engine.loadSampleFromFile (3, juce::File (impWav), err);
    engine.setLayerParameters (3, LayeredSampleEngine::LayerParameters {}); // default 20 Hz / 20 kHz
    auto outBypass = runBlocks (engine, 8192, 256, zero, zero, { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });
    check (std::abs (outBypass.busL[3][(size_t) dMax] - 1.0f) < 1.0e-6f
        && std::abs (outBypass.busL[3][(size_t) (dMax + 1)]) < 1.0e-6f,
           "P1-Filter: neutral 20Hz/20kHz passes impulse with zero phase distortion");

    // Active HP @ 800 Hz attenuating 60 Hz sine, and active LP @ 1000 Hz attenuating 8000 Hz sine
    std::vector<float> dualSine (8820);
    for (int i = 0; i < (int) dualSine.size(); ++i)
        dualSine[(size_t) i] = 0.5f * std::sin (2.0 * 3.14159265358979 * 60.0 * i / 44100.0);
    const auto lowWav = writeWavFileFloat32 (dir / "low60.wav", 44100.0, { dualSine, dualSine });
    std::vector<float> highSine (8820);
    for (int i = 0; i < (int) highSine.size(); ++i)
        highSine[(size_t) i] = 0.5f * std::sin (2.0 * 3.14159265358979 * 8000.0 * i / 44100.0);
    const auto highWav = writeWavFileFloat32 (dir / "high8k.wav", 44100.0, { highSine, highSine });

    engine.prepareToPlay (44100.0, 256);
    engine.loadSampleFromFile (3, juce::File (lowWav), err);
    engine.loadSampleFromFile (4, juce::File (highWav), err);
    LayeredSampleEngine::LayerParameters hpP, lpP;
    hpP.highPassHz = 800.0f;
    lpP.lowPassHz = 1000.0f;
    engine.setLayerParameters (3, hpP);
    engine.setLayerParameters (4, lpP);
    auto outFilt = runBlocks (engine, 16384, 256, zero, zero, { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });
    const double rmsHp60 = computeRms (outFilt.busL[3], dMax + 2000, 4410);
    const double rmsLp8k = computeRms (outFilt.busL[4], dMax + 2000, 4410);
    check (rmsHp60 < 0.01, "P1-Filter: HPF @ 800 Hz strongly attenuates 60 Hz", "rms=" + std::to_string (rmsHp60));
    check (rmsLp8k < 0.01, "P1-Filter: LPF @ 1000 Hz strongly attenuates 8000 Hz", "rms=" + std::to_string (rmsLp8k));
}

void testTriggerModes (const fs::path& dir)
{
    LayeredSampleEngine engine;
    engine.prepareToPlay (44100.0, 256);
    std::vector<float> click (128, 0.0f);
    click[0] = 0.9f;
    const auto wav = writeWavFileFloat32 (dir / "click.wav", 44100.0, { click, click });
    juce::String err;
    engine.loadSampleFromFile (3, juce::File (wav), err);
    engine.setPreRollMilliseconds (0.0f);

    std::vector<float> zero (8192, 0.0f);
    std::vector<float> trigSpike (8192, 0.0f);
    trigSpike[50] = 0.5f; // Audio transient > threshold (0.03)

    // Mode 0: Audio only - must still obey manualTrigger == true!
    engine.setTriggerMode (0, 0.03f);
    auto outManualInAudioMode = runBlocks (engine, 8192, 256, zero, zero, {}, 0);
    check (findFirstExceeding (outManualInAudioMode.busL[3], 0.5f) == engine.getMaximumDelaySamples(),
           "P1-Trigger: manualTrigger fires even when triggerMode == Audio");

    // Mode 0 ignores MIDI NoteOn
    engine.prepareToPlay (44100.0, 256);
    auto outMidiInAudioMode = runBlocks (engine, 8192, 256, zero, zero,
                                         { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });
    check (findFirstExceeding (outMidiInAudioMode.busL[3], 0.1f) < 0,
           "P1-Trigger: triggerMode == Audio ignores MIDI");

    // Mode 1: MIDI only ignores audio transient
    engine.prepareToPlay (44100.0, 256);
    engine.setTriggerMode (1, 0.03f);
    auto outAudioInMidiMode = runBlocks (engine, 8192, 256, trigSpike, trigSpike, {});
    check (findFirstExceeding (outAudioInMidiMode.busL[3], 0.1f) < 0,
           "P1-Trigger: triggerMode == MIDI ignores Audio threshold");

    // Mode 2: Both responds to audio transient at exact sample 50
    engine.prepareToPlay (44100.0, 256);
    engine.setTriggerMode (2, 0.03f);
    auto outAudio = runBlocks (engine, 8192, 256, trigSpike, trigSpike, {});
    check (findFirstExceeding (outAudio.busL[3], 0.5f) == 50 + engine.getMaximumDelaySamples(),
           "P1-Trigger: triggerMode == Both triggers at exact transient offset");
}

//==============================================================================
void testPreAndMainTimingAccuracy (const fs::path& dir)
{
    std::vector<float> imp (64, 0.0f);
    imp[0] = 1.0f;

    const double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
    const int bufferSizes[] = { 64, 128, 256, 512, 1024 };
    const float preLeads[] = { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f };

    for (double sr : sampleRates)
    {
        const auto preWav = writeWavFileFloat32 (dir / "imp_pre.wav", sr, { imp, imp });
        const auto mainWav = writeWavFileFloat32 (dir / "imp_main.wav", sr, { imp, imp });

        for (int bs : bufferSizes)
        {
            for (float leadMs : preLeads)
            {
                LayeredSampleEngine engine;
                engine.prepareToPlay (sr, bs);
                juce::String err;
                for (int l = 0; l < 3; ++l)
                    engine.loadSampleFromFile (l, juce::File (preWav), err);
                for (int l = 3; l < 6; ++l)
                    engine.loadSampleFromFile (l, juce::File (mainWav), err);

                engine.setTriggerMode (0, 0.1f); // Audio threshold
                engine.setPreRollMilliseconds (leadMs);

                const int totalSamples = (int) (sr * 0.35);
                const int trigInPos = 137; // arbitrary unaligned sample index
                std::vector<float> trigL ((size_t) totalSamples, 0.0f);
                trigL[(size_t) trigInPos] = 0.8f;

                auto out = runBlocks (engine, totalSamples, bs, trigL, trigL, {});
                const int dMax = engine.getMaximumDelaySamples();
                const int preS = engine.getPreRollSamples();

                // Dry impulse in master minus layer outputs must land at trigInPos + dMax
                std::vector<float> dryOnly ((size_t) totalSamples, 0.0f);
                for (int i = 0; i < totalSamples; ++i)
                {
                    float layersSum = 0.0f;
                    for (int l = 0; l < 6; ++l)
                        layersSum += out.busL[(size_t) l][(size_t) i];
                    dryOnly[(size_t) i] = out.masterL[(size_t) i] - layersSum;
                }

                const int dryPos = findFirstExceeding (dryOnly, 0.5f);
                check (dryPos == trigInPos + dMax,
                       "P2-Timing: dry lookahead latency exact",
                       "sr=" + std::to_string ((int) sr) + " bs=" + std::to_string (bs));

                const int expectedPrePos = dryPos - preS;
                for (int l = 0; l < 3; ++l)
                {
                    const int prePos = findFirstExceeding (out.busL[(size_t) l], 0.5f);
                    check (prePos == expectedPrePos,
                           "P2-Timing: PRE " + std::to_string (l + 1) + " ahead of dry by exact PRE lead",
                           "sr=" + std::to_string ((int) sr) + " bs=" + std::to_string (bs)
                               + " lead=" + std::to_string ((int) leadMs)
                               + " got=" + std::to_string (prePos) + " exp=" + std::to_string (expectedPrePos));
                }

                for (int l = 3; l < 6; ++l)
                {
                    const int mainPos = findFirstExceeding (out.busL[(size_t) l], 0.5f);
                    check (mainPos == dryPos,
                           "P2-Timing: MAIN " + std::to_string (l - 2) + " unaffected by PRE lead & aligned with dry",
                           "sr=" + std::to_string ((int) sr) + " bs=" + std::to_string (bs)
                               + " lead=" + std::to_string ((int) leadMs)
                               + " got=" + std::to_string (mainPos) + " exp=" + std::to_string (dryPos));
                }
            }
        }
    }

    // Independent individual layer timing shifts
    LayeredSampleEngine engine;
    engine.prepareToPlay (48000.0, 128);
    juce::String err;
    const auto impWav = writeWavFileFloat32 (dir / "imp48.wav", 48000.0, { imp, imp });
    for (int l = 0; l < 6; ++l)
        engine.loadSampleFromFile (l, juce::File (impWav), err);
    engine.setTriggerMode (1, 0.1f);
    engine.setPreRollMilliseconds (40.0f); // PRE base = -40ms => 60ms delay (2880 samples)

    LayeredSampleEngine::LayerParameters pPre2, pMain2;
    pPre2.delayShiftMs = -15.0f;  // PRE 2 moves earlier -> -55ms => 45ms delay (2160 samples)
    pMain2.delayShiftMs = 22.0f;  // MAIN 2 moves later -> +22ms => 122ms delay (5856 samples)
    engine.setLayerParameters (1, pPre2);
    engine.setLayerParameters (4, pMain2);

    std::vector<float> zero (16384, 0.0f);
    auto out = runBlocks (engine, 16384, 128, zero, zero, { { 100, juce::MidiMessage::noteOn (1, 60, 127) } });
    const int dryAnchor = 100 + 4800; // 100 + 100ms @ 48kHz
    check (findFirstExceeding (out.busL[0], 0.5f) == dryAnchor - 1920, "P2-Shift: PRE 1 untouched at -40ms");
    check (findFirstExceeding (out.busL[1], 0.5f) == dryAnchor - 2640, "P2-Shift: PRE 2 shifted -15ms to -55ms");
    check (findFirstExceeding (out.busL[3], 0.5f) == dryAnchor,        "P2-Shift: MAIN 1 untouched at 0ms");
    check (findFirstExceeding (out.busL[4], 0.5f) == dryAnchor + 1056, "P2-Shift: MAIN 2 shifted +22ms");
}

//==============================================================================
void testIndependentAudioRouting (const fs::path& dir)
{
    LayeredSampleEngine engine;
    engine.prepareToPlay (44100.0, 256);
    engine.setTriggerMode (1, 0.05f);
    engine.setPreRollMilliseconds (0.0f);

    juce::String err;
    const float markers[6] = { 0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f };
    for (int l = 0; l < 6; ++l)
    {
        std::vector<float> s (512, markers[l]);
        const auto wav = writeWavFileFloat32 (dir / ("layer_" + std::to_string (l) + ".wav"), 44100.0, { s, s });
        engine.loadSampleFromFile (l, juce::File (wav), err);
    }

    const int dMax = engine.getMaximumDelaySamples();
    std::vector<float> zero (8192, 0.0f);

    // 1. All 6 buses enabled: verify distinct amplitude per bus and exact sum into Master
    auto outAll = runBlocks (engine, 8192, 256, zero, zero, { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });
    float expectedSum = 0.0f;
    for (int l = 0; l < 6; ++l)
    {
        expectedSum += markers[l];
        check (std::abs (outAll.busL[(size_t) l][(size_t) dMax] - markers[l]) < 1.0e-4f,
               "P3-Routing: Bus " + std::to_string (l) + " carries only its own layer signal");
    }
    check (std::abs (outAll.masterL[(size_t) dMax] - expectedSum) < 1.0e-3f,
           "P3-Routing: Master accurately sums all 6 active layers");

    // 2. Partial optional buses connected (e.g. buses 1 and 4 disabled / nullptr)
    engine.prepareToPlay (44100.0, 256);
    auto outPartial = runBlocks (engine, 8192, 256, zero, zero,
                                 { { 0, juce::MidiMessage::noteOn (1, 60, 127) } },
                                 -1, { true, false, true, true, false, true });
    check (std::abs (outPartial.masterL[(size_t) dMax] - expectedSum) < 1.0e-3f,
           "P3-Routing: disabled optional buses do not crash or drop Master layer contribution");

    // 3. Mute on Layer 1 (PRE 2) & Pan + Gain on Layer 3 (MAIN 1)
    engine.prepareToPlay (44100.0, 256);
    LayeredSampleEngine::LayerParameters pMute, pPanGain;
    pMute.muted = true;
    pPanGain.gain = 0.5f;
    pPanGain.pan = -1.0f; // hard left
    engine.setLayerParameters (1, pMute);
    engine.setLayerParameters (3, pPanGain);

    auto outCtrl = runBlocks (engine, 8192, 256, zero, zero, { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });
    check (computeRms (outCtrl.busL[1]) == 0.0 && computeRms (outCtrl.busR[1]) == 0.0,
           "P3-Routing: Muted layer bus outputs exact zero (including filter reset)");
    check (std::abs (outCtrl.busL[3][(size_t) dMax] - (markers[3] * 0.5f)) < 1.0e-4f
        && std::abs (outCtrl.busR[3][(size_t) dMax]) < 1.0e-5f,
           "P3-Routing: Gain + hard-left pan apply cleanly to dedicated bus");

    // 4. Solo on Layer 4 (MAIN 2)
    engine.prepareToPlay (44100.0, 256);
    engine.setLayerParameters (1, LayeredSampleEngine::LayerParameters {});
    engine.setLayerParameters (3, LayeredSampleEngine::LayerParameters {});
    LayeredSampleEngine::LayerParameters pSolo;
    pSolo.solo = true;
    engine.setLayerParameters (4, pSolo);

    auto outSolo = runBlocks (engine, 8192, 256, zero, zero, { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });
    for (int l = 0; l < 6; ++l)
    {
        if (l == 4)
            check (std::abs (outSolo.busL[4][(size_t) dMax] - markers[4]) < 1.0e-4f, "P3-Solo: soloed layer bus plays");
        else
            check (computeRms (outSolo.busL[(size_t) l]) == 0.0,
                   "P3-Solo: non-soloed bus " + std::to_string (l) + " silenced");
    }
    check (std::abs (outSolo.masterL[(size_t) dMax] - markers[4]) < 1.0e-4f,
           "P3-Solo: Master receives only soloed layer");
}

void testTailLengthSeconds (const fs::path& dir)
{
    LayeredSampleEngine engine;
    engine.prepareToPlay (44100.0, 256);
    check (engine.getTailLengthSeconds() == 0.0, "Tail: initially zero before triggering");

    std::vector<float> flat (22050, 0.5f); // 500 ms @ 44.1 kHz
    const auto wav = writeWavFileFloat32 (dir / "tail.wav", 44100.0, { flat, flat });
    juce::String err;
    check (engine.loadSampleFromFile (3, juce::File (wav), err), "Tail: load wav");

    LayeredSampleEngine::LayerParameters p;
    p.pitchSemitones = 12.0f; // halves playback duration (500ms -> 250ms)
    p.trimStart = 0.2f;
    p.trimEnd = 0.8f;         // 60% of sample -> 150ms pitched
    p.delayShiftMs = 20.0f;   // 100ms lookahead + 20ms shift = 120ms delay -> total ~270ms
    p.envEnabled = true;
    p.releaseMs = 40.0f;
    engine.setLayerParameters (3, p);
    engine.setTriggerMode (1, 0.05f);
    engine.setPreRollMilliseconds (50.0f);

    engine.requestTrigger();
    std::vector<float> zero (256, 0.0f);
    runBlocks (engine, 256, 256, zero, zero, {});

    const double tailStart = engine.getTailLengthSeconds();
    check (tailStart > 0.24 && tailStart < 0.30,
           "Tail: accounts for delay, trim, pitch, and release immediately after trigger",
           "tail=" + std::to_string (tailStart));

    // With negative Pogo bend, effective playback stretches longer than unbent duration
    LayeredSampleEngine enginePogo;
    enginePogo.prepareToPlay (44100.0, 256);
    enginePogo.loadSampleFromFile (3, juce::File (wav), err);
    p.pogo = -0.8f;
    enginePogo.setLayerParameters (3, p);
    enginePogo.requestTrigger();
    runBlocks (enginePogo, 256, 256, zero, zero, {});
    check (enginePogo.getTailLengthSeconds() > tailStart,
           "Tail: negative Pogo bend extends predicted tail duration");

    // Advance well past end of playback -> tail must decay smoothly to exact zero
    std::vector<float> longZero (22050, 0.0f);
    runBlocks (engine, 22050, 256, longZero, longZero, {});
    check (engine.getTailLengthSeconds() == 0.0,
           "Tail: returns to zero after playback + release completes",
           "tail=" + std::to_string (engine.getTailLengthSeconds()));
}

void testEnvelopeScheduleAndCurveParity (const fs::path& dir)
{
    // Both a short 25 ms sample (forcing proportional scaling) and a longer 400 ms sample
    const int lengths[] = { 1103, 17640 };
    const float pitches[] = { 0.0f, 7.0f };

    for (int li = 0; li < 2; ++li)
    {
        const int len = lengths[li];
        std::vector<float> unity (static_cast<size_t> (len), 1.0f);
        const auto wav = writeWavFileFloat32 (dir / ("env_parity_" + std::to_string (len) + ".wav"),
                                              44100.0, { unity, unity });

        for (int mode = 0; mode <= 7; ++mode)
        {
            LayeredSampleEngine engine;
            engine.prepareToPlay (44100.0, 256);
            engine.setTriggerMode (1, 0.05f);
            engine.setPreRollMilliseconds (0.0f);
            juce::String err;
            check (engine.loadSampleFromFile (3, juce::File (wav), err), "EnvParity: load sample");

            LayeredSampleEngine::LayerParameters p;
            p.envEnabled = true;
            p.pitchSemitones = pitches[li];
            p.trimStart = 0.10f;
            p.trimEnd = 0.90f;
            p.attackMs = 60.0f;
            p.holdMs = 20.0f;
            p.decayMs = 80.0f;
            p.sustain = 0.62f;
            p.releaseMs = 70.0f;
            p.attackCurve = mode;
            p.decayCurve = mode;
            p.releaseCurve = mode;
            p.attackTension = 0.65f;
            p.decayTension = -0.55f;
            p.releaseTension = -0.75f;
            engine.setLayerParameters (3, p);

            const int dMax = engine.getMaximumDelaySamples();
            const int trimStartIdx = (int) std::floor ((double) p.trimStart * (double) len);
            const int trimEndIdx = (int) std::ceil ((double) p.trimEnd * (double) len);
            const int trimmedLen = trimEndIdx - trimStartIdx;
            const double speed = std::pow (2.0, (double) p.pitchSemitones / 12.0);
            const float totalDurationMs = (float) (((double) trimmedLen / speed) / 44100.0 * 1000.0);
            const auto sched = LayeredSampleEngine::computeEnvelopeSchedule (p, totalDurationMs);

            const int totalSim = dMax + (int) std::ceil ((double) trimmedLen / speed) + 512;
            std::vector<float> zero ((size_t) totalSim, 0.0f);
            auto out = runBlocks (engine, totalSim, 256, zero, zero,
                                  { { 0, juce::MidiMessage::noteOn (1, 60, 127) } });

            double maxDiff = 0.0;
            const int numPlayed = (int) std::floor ((double) (trimmedLen - 1) / speed);
            for (int k = 1; k < numPlayed - 1; ++k)
            {
                const float ageMs = (float) ((double) k / 44100.0 * 1000.0);
                // Ignore the 1-sample boundary tick where release latches discrete sample remainingMs
                if (std::abs (ageMs - sched.releaseStartMs) < 0.06f)
                    continue;
                const float expected = LayeredSampleEngine::evaluateCompleteEnvelopeAtMs (p, sched, ageMs);
                const float actual = out.busL[3][(size_t) (dMax + k)];
                maxDiff = std::max (maxDiff, (double) std::abs (expected - actual));
            }
            check (maxDiff < 0.025,
                   "EnvParity: DSP output matches shared schedule+curve math for mode " + std::to_string (mode),
                   "len=" + std::to_string (len) + " maxDiff=" + std::to_string (maxDiff));
        }
    }
}

void testStateVersionMigration()
{
    using wavelens::state::migratePreDelayMsForVersion;

    // v1 default (-50ms PRE delay, 50ms ignored preRoll) -> net -50ms means new delay = 0ms
    check (std::abs (migratePreDelayMsForVersion (1, 0, 3, -50.0f, 50.0f) - 0.0f) < 1.0e-5f,
           "Migration: v1 default -50ms PRE shifts to 0ms under 50ms preRoll");

    // v1 custom (-35ms PRE delay, 50ms preRoll) -> +15ms shift so -50 + 15 = -35ms
    check (std::abs (migratePreDelayMsForVersion (1, 1, 3, -35.0f, 50.0f) - 15.0f) < 1.0e-5f,
           "Migration: v1 custom -35ms PRE preserves net timing (+15ms)");

    // v1 custom preRoll (80ms preRoll, -50ms delay) -> +30ms shift so -80 + 30 = -50ms
    check (std::abs (migratePreDelayMsForVersion (1, 2, 3, -50.0f, 80.0f) - 30.0f) < 1.0e-5f,
           "Migration: v1 respects restored preRollMs (+80ms)");

    // v1 MAIN layer (index 3..5) must stay untouched
    check (std::abs (migratePreDelayMsForVersion (1, 3, 3, 12.5f, 50.0f) - 12.5f) < 1.0e-5f,
           "Migration: v1 MAIN layer untouched");

    // v2 state must never be shifted again
    check (std::abs (migratePreDelayMsForVersion (2, 0, 3, -20.0f, 50.0f) + 20.0f) < 1.0e-5f,
           "Migration: v2 state not double-migrated");
}

void testChunkedOversizedBlockMonitoring (const fs::path& dir)
{
    LayeredSampleEngine engine;
    const int hostPrepareChunk = 256;
    const int oversizedHostBlock = 2048;
    engine.prepareToPlay (44100.0, hostPrepareChunk);
    engine.setTriggerMode (1, 0.05f);
    engine.setPreRollMilliseconds (0.0f);

    std::vector<float> pulse (2000, 0.45f);
    const auto wav = writeWavFileFloat32 (dir / "oversized.wav", 44100.0, { pulse, pulse });
    juce::String err;
    check (engine.loadSampleFromFile (2, juce::File (wav), err), "OversizedBlock: load layer 2");

    // Mirror WaveLensProcessor's per-chunk processing even when optional layer buses 1..6 are all disabled
    const int dMax = engine.getMaximumDelaySamples();
    int capturedNonZeroSamples = 0;
    const int midiTriggerGlobalPos = 100;

    for (int chunkStart = 0; chunkStart < dMax + oversizedHostBlock; chunkStart += hostPrepareChunk)
    {
        const int n = std::min (hostPrepareChunk, dMax + oversizedHostBlock - chunkStart);
        juce::AudioBuffer<float> inBuf (2, n);
        juce::AudioBuffer<float> masterBuf (2, n);
        std::array<juce::AudioBuffer<float>*, LayeredSampleEngine::kNumLayers> disabledBuses {};
        juce::MidiBuffer midi;
        if (midiTriggerGlobalPos >= chunkStart && midiTriggerGlobalPos < chunkStart + n)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 127), midiTriggerGlobalPos - chunkStart);

        engine.processBlock (inBuf, masterBuf, disabledBuses, midi, false);

        const auto& monitorBuf = engine.getLayerBuffer (2);
        for (int s = 0; s < n; ++s)
            if (std::abs (monitorBuf.getSample (0, s)) > 0.2f)
                ++capturedNonZeroSamples;
    }

    check (capturedNonZeroSamples > 1500,
           "OversizedBlock: per-chunk layer buffer captures full waveform across all chunks even with buses disabled",
           "captured=" + std::to_string (capturedNonZeroSamples));
}

void testConcurrentSampleReplacementStress (const fs::path& dir)
{
    LayeredSampleEngine engine;
    engine.prepareToPlay (44100.0, 128);
    engine.setTriggerMode (2, 0.05f);
    engine.setPreRollMilliseconds (40.0f);

    std::vector<float> toneA (2205, 0.5f);  // 50 ms
    std::vector<float> toneB (6615, -0.4f); // 150 ms
    const auto wavA = writeWavFileFloat32 (dir / "stressA.wav", 44100.0, { toneA, toneA });
    const auto wavB = writeWavFileFloat32 (dir / "stressB.wav", 44100.0, { toneB, toneB });

    juce::String err;
    for (int l = 0; l < 6; ++l)
        engine.loadSampleFromFile (l, juce::File (l % 2 == 0 ? wavA : wavB), err);

    std::atomic<bool> stop { false };
    std::atomic<int> blocksProcessed { 0 };
    std::atomic<int> loadsCompleted { 0 };

    std::thread audioThread ([&]
    {
        juce::AudioBuffer<float> inBuf (2, 128);
        juce::AudioBuffer<float> masterBuf (2, 128);
        std::array<juce::AudioBuffer<float>, 6> lBufs;
        std::array<juce::AudioBuffer<float>*, 6> lPtrs {};
        for (int l = 0; l < 6; ++l)
        {
            lBufs[(size_t) l].setSize (2, 128);
            lPtrs[(size_t) l] = &lBufs[(size_t) l];
        }

        for (int iter = 0; iter < 2400; ++iter)
        {
            inBuf.clear();
            juce::MidiBuffer midi;
            if (iter % 7 == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 127), (iter * 13) % 128);
            if (iter % 11 == 0)
                inBuf.setSample (0, 5, 0.8f);

            engine.processBlock (inBuf, masterBuf, lPtrs, midi, (iter % 19) == 0);
            blocksProcessed.fetch_add (1, std::memory_order_relaxed);
        }
        stop.store (true, std::memory_order_release);
    });

    std::thread uiThread ([&]
    {
        int step = 0;
        while (! stop.load (std::memory_order_acquire) || step < 300)
        {
            const int layer = step % 6;
            if (step % 5 == 0)
                engine.clearSample (layer);
            else
            {
                juce::String loadErr;
                engine.loadSampleFromFile (layer, juce::File ((step % 2 == 0) ? wavA : wavB), loadErr);
            }

            if (step % 3 == 0)
                engine.requestTrigger();
            if (step % 4 == 0)
                engine.requestPreview (layer);

            (void) engine.getWaveformPreview (layer);
            (void) engine.getSampleName (layer);
            (void) engine.getLoadedMask();
            (void) engine.getTailLengthSeconds();
            engine.collectRetiredSamples();

            loadsCompleted.fetch_add (1, std::memory_order_relaxed);
            ++step;
            if (step >= 500 && stop.load (std::memory_order_acquire))
                break;
        }
    });

    audioThread.join();
    uiThread.join();

    // Flush active voices completely after concurrent stress and ensure all retired buffers reclaim
    std::vector<float> zeroLong (24000, 0.0f);
    runBlocks (engine, 24000, 128, zeroLong, zeroLong, {});
    engine.collectRetiredSamples();

    int leftoverRetired = 0;
    for (int l = 0; l < 6; ++l)
        leftoverRetired += engine.getRetiredSampleCount (l);

    check (blocksProcessed.load() == 2400 && loadsCompleted.load() >= 300 && leftoverRetired == 0,
           "QSBR-Stress: concurrent audio + UI sample replacement/clear reclaims 100% cleanly",
           "blocks=" + std::to_string (blocksProcessed.load())
               + " ops=" + std::to_string (loadsCompleted.load())
               + " leftover=" + std::to_string (leftoverRetired));
}

void testVoiceReleaseAndRemainingThreadSafety (const fs::path& dir)
{
    LayeredSampleEngine engine;
    engine.prepareToPlay (44100.0, 256);
    engine.setTriggerMode (1, 0.05f);
    engine.setPreRollMilliseconds (50.0f);

    std::vector<float> flat (7938, 0.5f); // 180 ms @ 44.1 kHz
    const auto wav = writeWavFileFloat32 (dir / "release_track.wav", 44100.0, { flat, flat });
    juce::String err;
    check (engine.loadSampleFromFile (3, juce::File (wav), err), "VoiceTail: load wav");

    LayeredSampleEngine::LayerParameters p;
    p.envEnabled = true;
    p.attackMs = 20.0f;
    p.holdMs = 0.0f;
    p.decayMs = 30.0f;
    p.sustain = 0.7f;
    p.releaseMs = 60.0f;
    engine.setLayerParameters (3, p);

    const auto initSnap = engine.getVoiceTailSnapshot (3, 0);
    check (! initSnap.active && ! initSnap.inRelease
        && initSnap.remainingSeconds == 0.0 && initSnap.releaseRemainingSeconds == 0.0,
           "VoiceTail: initially inactive and zero");

    engine.requestTrigger();
    std::vector<float> zero256 (256, 0.0f);
    runBlocks (engine, 256, 256, zero256, zero256, {});

    const auto snapStart = engine.getVoiceTailSnapshot (3, 0);
    check (snapStart.active && ! snapStart.inRelease,
           "VoiceTail: active and not yet inRelease while in pre-release delay/attack");
    check (snapStart.releaseRemainingSeconds > 0.055 && snapStart.releaseRemainingSeconds < 0.065,
           "VoiceTail: reports scheduled ~60ms release before entering release phase",
           "rel=" + std::to_string (snapStart.releaseRemainingSeconds));
    check (std::abs (engine.getTailLengthSeconds() - snapStart.remainingSeconds) < 1.0e-9,
           "VoiceTail: aggregate getTailLengthSeconds matches max active voice remainingSeconds");

    // Advance 240 ms (100 ms delay + 140 ms playback -> 40 ms remaining, inside 60 ms release)
    const int advanceSamples = (int) std::llround (0.240 * 44100.0) - 256;
    std::vector<float> zeroAdv ((size_t) advanceSamples, 0.0f);
    runBlocks (engine, advanceSamples, 256, zeroAdv, zeroAdv, {});

    const auto snapRel = engine.getVoiceTailSnapshot (3, 0);
    check (snapRel.active && snapRel.inRelease,
           "VoiceTail: reports inRelease == true once playback crosses release threshold");
    check (snapRel.releaseRemainingSeconds > 0.025 && snapRel.releaseRemainingSeconds < 0.050
        && std::abs (snapRel.remainingSeconds - snapRel.releaseRemainingSeconds) < 1.0e-4,
           "VoiceTail: during release phase, remainingSeconds tracks release countdown cleanly",
           "rem=" + std::to_string (snapRel.remainingSeconds) + " rel=" + std::to_string (snapRel.releaseRemainingSeconds));

    // Bypass resets active voices and zeros tail cleanly without lingering tail
    engine.processBypassed();
    const auto snapBypass = engine.getVoiceTailSnapshot (3, 0);
    check (! snapBypass.active && ! snapBypass.inRelease
        && snapBypass.remainingSeconds == 0.0 && engine.getTailLengthSeconds() == 0.0,
           "VoiceTail: processBypassed clears voice release state and tail length to zero");

    // Concurrent lock-free stress: audio thread triggering/releasing/bypassing vs host polling tail + snapshots
    std::atomic<bool> stopPolling { false };
    std::atomic<bool> sawInvalid { false };
    std::atomic<int> polls { 0 };

    std::thread pollThread ([&]
    {
        while (! stopPolling.load (std::memory_order_acquire))
        {
            const double t = engine.getTailLengthSeconds();
            if (std::isnan (t) || t < 0.0 || t > 5.0)
                sawInvalid.store (true, std::memory_order_relaxed);
            for (int l = 0; l < LayeredSampleEngine::kNumLayers; ++l)
            {
                for (int v = 0; v < LayeredSampleEngine::kVoicesPerLayer; ++v)
                {
                    const auto s = engine.getVoiceTailSnapshot (l, v);
                    if (std::isnan (s.remainingSeconds) || std::isnan (s.releaseRemainingSeconds)
                        || s.remainingSeconds < 0.0 || s.releaseRemainingSeconds < 0.0)
                        sawInvalid.store (true, std::memory_order_relaxed);
                }
            }
            if ((polls.fetch_add (1, std::memory_order_relaxed) % 17) == 0)
                engine.setPreRollMilliseconds ((float) (10 + (polls.load (std::memory_order_relaxed) % 80)));
        }
    });

    juce::AudioBuffer<float> inBuf (2, 128), masterBuf (2, 128);
    std::array<juce::AudioBuffer<float>*, 6> emptyPtrs {};
    for (int iter = 0; iter < 1500; ++iter)
    {
        inBuf.clear();
        juce::MidiBuffer midi;
        if (iter % 12 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 127), 0);
        if (iter % 97 == 96)
            engine.processBypassed();
        else
            engine.processBlock (inBuf, masterBuf, emptyPtrs, midi, false);
    }

    stopPolling.store (true, std::memory_order_release);
    pollThread.join();

    check (! sawInvalid.load() && polls.load() > 0,
           "VoiceTail-Concurrency: concurrent audio callback + non-blocking tail/snapshot polling has zero invalid states",
           "polls=" + std::to_string (polls.load()));
}
} // namespace

int main()
{
    const fs::path tempDir = fs::temp_directory_path() / "spectral_multiple_engine_tests";
    fs::create_directories (tempDir);

    testLoadingMonoStereoAndEdgeCases (tempDir);
    testQsbrMemoryReclamation (tempDir);
    testReverseAndFractionalInterpolation (tempDir);
    testPitchPogoTrimFadesFiltersAndPolyphony (tempDir);
    testTriggerModes (tempDir);
    testPreAndMainTimingAccuracy (tempDir);
    testIndependentAudioRouting (tempDir);
    testTailLengthSeconds (tempDir);
    testVoiceReleaseAndRemainingThreadSafety (tempDir);
    testEnvelopeScheduleAndCurveParity (tempDir);
    testStateVersionMigration();
    testChunkedOversizedBlockMonitoring (tempDir);
    testConcurrentSampleReplacementStress (tempDir);

    fs::remove_all (tempDir);

    if (gFailures == 0)
    {
        std::cout << "[PASS] All C++17 engine, tail, envelope parity, migration, timing, routing, and concurrency stress tests passed.\n";
        return 0;
    }

    std::cerr << "[FAIL] " << gFailures << " check(s) failed.\n";
    return 1;
}
