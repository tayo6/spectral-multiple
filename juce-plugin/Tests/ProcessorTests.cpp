#include "PluginProcessor.h"
#include "StateMigration.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
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

juce::File writeWavFileFloat32 (const fs::path& p, double sr, const std::vector<std::vector<float>>& chData)
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
    return juce::File (p.string());
}

juce::File writeAiffFile16 (const fs::path& p, double sr, const std::vector<std::vector<float>>& chData)
{
    const uint16_t ch = (uint16_t) chData.size();
    const uint32_t frames = (uint32_t) chData[0].size();
    const uint32_t pcmBytes = frames * ch * 2;
    const uint32_t ssndPayload = 8 + pcmBytes;
    const uint32_t ssndPad = ssndPayload & 1u;
    const uint32_t commPayload = 18;
    const uint32_t formPayload = 4 + (8 + commPayload) + (8 + ssndPayload + ssndPad);

    std::vector<uint8_t> bytes;
    bytes.reserve (8 + formPayload);
    bytes.insert (bytes.end(), { 'F', 'O', 'R', 'M' });
    writeU32BE (bytes, formPayload);
    bytes.insert (bytes.end(), { 'A', 'I', 'F', 'F' });

    bytes.insert (bytes.end(), { 'C', 'O', 'M', 'M' });
    writeU32BE (bytes, commPayload);
    writeU16BE (bytes, ch);
    writeU32BE (bytes, frames);
    writeU16BE (bytes, 16);

    int exp2 = 0;
    const double mant = std::frexp (sr, &exp2);
    uint16_t ieeeExp = (uint16_t) (16383 + exp2 - 1);
    const double hiDouble = std::floor (std::ldexp (mant, 32));
    const uint64_t hiPart = (uint64_t) hiDouble;
    const uint64_t loPart = (uint64_t) std::llround (std::ldexp (mant, 64) - std::ldexp (hiDouble, 32));
    uint64_t ieeeMant = (hiPart << 32) + loPart;
    if (ieeeMant == 0)
    {
        ieeeMant = 1ULL << 63;
        ++ieeeExp;
    }
    writeU16BE (bytes, ieeeExp);
    for (int shift = 56; shift >= 0; shift -= 8)
        bytes.push_back ((uint8_t) ((ieeeMant >> shift) & 0xff));

    bytes.insert (bytes.end(), { 'S', 'S', 'N', 'D' });
    writeU32BE (bytes, ssndPayload);
    writeU32BE (bytes, 0);
    writeU32BE (bytes, 0);
    for (uint32_t i = 0; i < frames; ++i)
    {
        for (uint16_t c = 0; c < ch; ++c)
        {
            const float clamped = std::max (-1.0f, std::min (1.0f, chData[c][i]));
            const int16_t s = (int16_t) std::llround (clamped * 32767.0f);
            writeU16BE (bytes, (uint16_t) s);
        }
    }
    if (ssndPad != 0)
        bytes.push_back (0);

    std::ofstream f (p, std::ios::binary);
    f.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
    return juce::File (p.string());
}

void setParam (WaveLensProcessor& proc, const juce::String& id, float plainValue)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    else
        check (false, "Param lookup", id.toStdString());
}

float getParamPlain (WaveLensProcessor& proc, const juce::String& id)
{
    if (auto* raw = proc.apvts.getRawParameterValue (id))
        return raw->load();
    check (false, "Raw param lookup", id.toStdString());
    return 0.0f;
}

class TestXmlProcessorHelper final : public juce::AudioProcessor
{
public:
    TestXmlProcessorHelper() = default;
    const juce::String getName() const override { return "TestXmlHelper"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    using juce::AudioProcessor::processBlock;
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    static void encodeXml (const juce::XmlElement& xml, juce::MemoryBlock& dest)
    {
        copyXmlToBinary (xml, dest);
    }

    static std::unique_ptr<juce::XmlElement> decodeXml (const juce::MemoryBlock& block)
    {
        return getXmlFromBinary (block.getData(), (int) block.getSize());
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestXmlProcessorHelper)
};

class MockPlayHead final : public juce::AudioPlayHead
{
public:
    bool playing = true;
    double bpm = 120.0;
    double ppq = 0.0;
    int tsNum = 4;
    int tsDen = 4;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        TimeSignature ts;
        ts.numerator = tsNum;
        ts.denominator = tsDen;
        info.setTimeSignature (ts);
        return info;
    }
};

//==============================================================================
void testRealJuceAiffLoading (const fs::path& dir)
{
    const auto aiff44 = writeAiffFile16 (dir / "real_44.aif", 44100.0, {
        { 0.7f, 0.3f, -0.1f, 0.0f },
        { -0.2f, 0.6f, 0.1f, 0.0f }
    });
    const auto aiff48 = writeAiffFile16 (dir / "real_48.aif", 48000.0, {
        { 0.5f, -0.5f },
        { -0.5f, 0.5f }
    });
    const auto aiff96 = writeAiffFile16 (dir / "real_96.aif", 96000.0, {
        { 0.4f, 0.2f },
        { -0.4f, -0.2f }
    });

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    for (const auto& pair : std::vector<std::pair<juce::File, double>> {
             { aiff44, 44100.0 }, { aiff48, 48000.0 }, { aiff96, 96000.0 } })
    {
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (pair.first));
        check (reader != nullptr, "RealJUCE-AIFF: AiffAudioFormat opens fixture",
               pair.first.getFileName().toStdString());
        if (reader != nullptr)
        {
            check (std::abs (reader->sampleRate - pair.second) < 0.01 && reader->numChannels == 2,
                   "RealJUCE-AIFF: exact sample rate & stereo channels decoded");
        }
    }

    {
    WaveLensProcessor proc;
    proc.prepareToPlay (44100.0, 256);

    juce::String err;
    check (proc.loadSampleFile (0, aiff44, err), "RealJUCE-AIFF: WaveLensProcessor loads stereo AIFF",
           err.toStdString());
    check (proc.connectedMask.load() == (1 << 0), "RealJUCE-AIFF: connectedMask updated");
    const auto sampleName = proc.getSampleName (0);
    check (sampleName == "real_44", "RealJUCE-AIFF: sample name preserved");
    }
}

void testProcessorMonitorOversizedBlockAndDisabledBuses (const fs::path& dir)
{
    // Segmented 2048-sample signal so we can distinguish chunk 0, middle chunks, and final chunk:
    //   0..255     -> 0.20f (chunk 0)
    //   256..1791  -> 0.50f (chunks 1..6)
    //   1792..2047 -> 0.85f (chunk 7)
    std::vector<float> segmented (2048, 0.50f);
    for (int i = 0; i < 256; ++i)
        segmented[(size_t) i] = 0.20f;
    for (int i = 1792; i < 2048; ++i)
        segmented[(size_t) i] = 0.85f;
    const auto wav = writeWavFileFloat32 (dir / "segmented_2048.wav", 44100.0, { segmented, segmented });

    // 1. Optional output buses disabled (default WaveLens bus layout: only Master enabled)
    {
        WaveLensProcessor proc;
        for (int b = 1; b < proc.getBusCount (false); ++b)
        {
            auto* bus = proc.getBus (false, b);
            check (bus != nullptr && ! bus->isEnabled(),
                   "ProcMonitor: optional output bus defaults to disabled");
        }

        proc.prepareToPlay (44100.0, 256); // internal triggerBuffer maxChunk = 256
        setParam (proc, ids::capture, 1.0f);     // Live mode (no PlayHead needed)
        setParam (proc, ids::timeMs, 100.0f);    // 4410 samples across 1024 columns -> ~475 cols for 2048 samples
        setParam (proc, ids::triggerMode, 1.0f); // MIDI trigger
        setParam (proc, ids::preRollMs, 100.0f); // PRE layer 0 starts at delay = 0 samples

        juce::String err;
        check (proc.loadSampleFile (0, wav, err), "ProcMonitor: load segmented WAV on PRE 1", err.toStdString());

        const int hostBlockSize = 2048; // 8x prepared block size (maxChunk = 256)
        juce::AudioBuffer<float> buffer (juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels()),
                                         hostBlockSize);
        buffer.clear();

        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);

        proc.processBlock (buffer, midi);

        const int filledCols = proc.filled.load();
        check (proc.hasData.load(), "ProcMonitor(DisabledBuses): monitor marked hasData");
        check (filledCols >= 450 && filledCols < WaveLensProcessor::kCols,
               "ProcMonitor(DisabledBuses): monitor advanced across full oversized block",
               "filled=" + std::to_string (filledCols));

        // Verify chunk 0 (cols ~10..45) was not lost or overwritten
        float earlyMinPeak = 10.0f, earlyMaxPeak = 0.0f;
        for (int c = 10; c <= 45; ++c)
        {
            const float v = proc.colMax[(size_t) WaveLensProcessor::idx (0, c)].load();
            earlyMinPeak = std::min (earlyMinPeak, v);
            earlyMaxPeak = std::max (earlyMaxPeak, v);
        }
        check (earlyMinPeak > 0.18f && earlyMaxPeak < 0.22f,
               "ProcMonitor(DisabledBuses): early chunk 0 (~0.20) preserved in monitor",
               "min=" + std::to_string (earlyMinPeak) + " max=" + std::to_string (earlyMaxPeak));

        // Verify middle chunks (cols ~140..320) reflect 0.50f
        float midMinPeak = 10.0f, midMaxPeak = 0.0f;
        for (int c = 140; c <= 320; ++c)
        {
            const float v = proc.colMax[(size_t) WaveLensProcessor::idx (0, c)].load();
            midMinPeak = std::min (midMinPeak, v);
            midMaxPeak = std::max (midMaxPeak, v);
        }
        check (midMinPeak > 0.48f && midMaxPeak < 0.52f,
               "ProcMonitor(DisabledBuses): middle chunks (~0.50) contributed to monitor",
               "min=" + std::to_string (midMinPeak) + " max=" + std::to_string (midMaxPeak));

        // Verify final chunk 7 (cols ~430..465) reflects 0.85f
        float lateMinPeak = 10.0f, lateMaxPeak = 0.0f;
        for (int c = 430; c <= 465; ++c)
        {
            const float v = proc.colMax[(size_t) WaveLensProcessor::idx (0, c)].load();
            lateMinPeak = std::min (lateMinPeak, v);
            lateMaxPeak = std::max (lateMaxPeak, v);
        }
        check (lateMinPeak > 0.83f && lateMaxPeak < 0.87f,
               "ProcMonitor(DisabledBuses): final chunk (~0.85) contributed to monitor",
               "min=" + std::to_string (lateMinPeak) + " max=" + std::to_string (lateMaxPeak));

        // Verify zero unexpected silent columns anywhere across the entire captured span
        float globalMinColMax = 10.0f;
        for (int c = 0; c < filledCols; ++c)
            globalMinColMax = std::min (globalMinColMax, proc.colMax[(size_t) WaveLensProcessor::idx (0, c)].load());
        check (globalMinColMax > 0.18f,
               "ProcMonitor(DisabledBuses): zero unexpected silence gaps between chunks",
               "minColMax=" + std::to_string (globalMinColMax));

        // Master output audio buffer must match all three segments across the oversized block
        check (std::abs (buffer.getSample (0, 100) - 0.20f) < 0.01f
            && std::abs (buffer.getSample (0, 1000) - 0.50f) < 0.01f
            && std::abs (buffer.getSample (0, 1900) - 0.85f) < 0.01f,
               "ProcMonitor(DisabledBuses): Master output continuous across all chunks");

        // Next block of silence must write silence (no stale sample carryover)
        buffer.clear();
        juce::MidiBuffer emptyMidi;
        proc.processBlock (buffer, emptyMidi);
        const int tailCol = proc.head.load();
        check (std::abs (proc.colMax[(size_t) WaveLensProcessor::idx (0, tailCol)].load()) < 1.0e-4f,
               "ProcMonitor(DisabledBuses): subsequent silent block clears monitor trail (no stale audio)");
    }

    // 2. All optional layer output buses enabled + Tempo capture mode with PlayHead
    {
        WaveLensProcessor proc;
        check (proc.enableAllBuses(), "ProcMonitor(EnabledBuses): enableAllBuses succeeded");
        MockPlayHead playHead;
        proc.setPlayHead (&playHead);
        proc.prepareToPlay (44100.0, 256);

        setParam (proc, ids::capture, 0.0f);     // Tempo mode
        setParam (proc, ids::bars, 0.0f);        // 1/16 bar = 125 ms @ 120 BPM
        setParam (proc, ids::triggerMode, 1.0f); // MIDI trigger
        setParam (proc, ids::preRollMs, 100.0f);

        juce::String err;
        check (proc.loadSampleFile (0, wav, err), "ProcMonitor(EnabledBuses): load WAV", err.toStdString());

        const int hostBlockSize = 1536; // 6x maxChunk = 256
        juce::AudioBuffer<float> buffer (juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels()),
                                         hostBlockSize);
        buffer.clear();

        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);

        proc.processBlock (buffer, midi);

        check (proc.hasData.load() && proc.filled.load() >= 260,
               "ProcMonitor(EnabledBuses/Tempo): monitor captured across oversized Tempo block");
        check (std::abs (proc.colMax[(size_t) WaveLensProcessor::idx (0, 15)].load() - 0.20f) < 0.02f
            && std::abs (proc.colMax[(size_t) WaveLensProcessor::idx (0, 120)].load() - 0.50f) < 0.02f,
               "ProcMonitor(EnabledBuses/Tempo): chunk boundaries rendered & monitored faithfully");

        auto pre1Out = proc.getBusBuffer (buffer, false, 1);
        check (pre1Out.getNumChannels() == 2
            && std::abs (pre1Out.getSample (0, 100) - 0.20f) < 0.01f
            && std::abs (pre1Out.getSample (0, 1000) - 0.50f) < 0.01f,
               "ProcMonitor(EnabledBuses/Tempo): dedicated PRE 1 bus continuous across oversized block");
    }
}

// WaveLensProcessor owns substantial fixed-size oscilloscope/FFT buffers. These state
// migration tests create several processor instances; keep them heap-allocated so the
// Windows test runner's limited stack is not exhausted by the function's local frame.
void testRealProcessorStateSerializationAndMigration (const fs::path& dir)
{
    const auto preWav = writeWavFileFloat32 (dir / "state_pre0.wav", 44100.0, { { 0.4f, 0.2f }, { 0.4f, 0.2f } });
    const auto mainWav = writeWavFileFloat32 (dir / "state_main3.wav", 44100.0, { { 0.8f, 0.1f }, { 0.8f, 0.1f } });
    const auto disappearingWav = writeWavFileFloat32 (dir / "vanishing_sample.wav", 44100.0, { { 0.5f }, { 0.5f } });

    // 1. Current-version (v2) state serialization & restoration
    juce::MemoryBlock v2Blob;
    {
        auto procSrc = std::make_unique<WaveLensProcessor>();
        procSrc->prepareToPlay (44100.0, 256);
        juce::String err;
        check (procSrc->loadSampleFile (0, preWav, err), "State-v2: load PRE 1", err.toStdString());
        check (procSrc->loadSampleFile (3, mainWav, err), "State-v2: load MAIN 1", err.toStdString());

        setParam (*procSrc, ids::preRollMs, 65.0f);
        setParam (*procSrc, ids::layerDelay (0), -12.5f);
        setParam (*procSrc, ids::layerDelay (3), 18.0f);
        setParam (*procSrc, ids::layerGain (0), 0.75f);
        setParam (*procSrc, ids::layerMute (1), 1.0f);
        setParam (*procSrc, ids::triggerMode, 1.0f);

        procSrc->getStateInformation (v2Blob);
        auto xml = TestXmlProcessorHelper::decodeXml (v2Blob);
        check (xml != nullptr
            && xml->getIntAttribute ("wavelensStateVersion", 0) == wavelens::state::kCurrentStateVersion
            && xml->getStringAttribute ("samplePath1") == preWav.getFullPathName()
            && xml->getStringAttribute ("samplePath4") == mainWav.getFullPathName(),
               "State-v2: getStateInformation writes version tag and sample paths");
    }

    {
        auto procDst = std::make_unique<WaveLensProcessor>();
        procDst->prepareToPlay (44100.0, 256);
        procDst->setStateInformation (v2Blob.getData(), (int) v2Blob.getSize());

        check (std::abs (getParamPlain (*procDst, ids::preRollMs) - 65.0f) < 0.05f,
               "State-v2: preRollMs restored");
        check (std::abs (getParamPlain (*procDst, ids::layerDelay (0)) - (-12.5f)) < 0.05f,
               "State-v2: PRE delay NOT double-migrated");
        check (std::abs (getParamPlain (*procDst, ids::layerDelay (3)) - 18.0f) < 0.05f,
               "State-v2: MAIN delay restored");
        check (std::abs (getParamPlain (*procDst, ids::layerGain (0)) - 0.75f) < 0.01f,
               "State-v2: layer gain restored");
        check (getParamPlain (*procDst, ids::layerMute (1)) > 0.5f,
               "State-v2: layer mute restored");
        check (procDst->connectedMask.load() == ((1 << 0) | (1 << 3)),
               "State-v2: connectedMask restored");
        check (procDst->missingSampleMask.load() == 0 && ! procDst->isLayerSampleMissing (0) && ! procDst->isLayerSampleMissing (3),
               "State-v2: no false missing-sample flags");
        check (procDst->getSampleName (0) == "state_pre0" && procDst->getSampleName (3) == "state_main3",
               "State-v2: sample display names restored");
    }

    // 2. Older version (v1 and versionless legacy) migration via real setStateInformation
    for (int mode = 0; mode < 2; ++mode)
    {
        juce::MemoryBlock legacyBlob;
        {
            auto procLegacy = std::make_unique<WaveLensProcessor>();
            procLegacy->prepareToPlay (44100.0, 256);
            juce::String err;
            procLegacy->loadSampleFile (0, preWav, err);

            setParam (*procLegacy, ids::preRollMs, 50.0f);
            setParam (*procLegacy, ids::layerDelay (0), -50.0f);
            setParam (*procLegacy, ids::layerDelay (1), -35.0f);
            setParam (*procLegacy, ids::layerDelay (2), -75.0f);
            setParam (*procLegacy, ids::layerDelay (3), 12.5f);
            setParam (*procLegacy, ids::layerDelay (4), -20.0f);
            procLegacy->getStateInformation (legacyBlob);
        }

        auto xml = TestXmlProcessorHelper::decodeXml (legacyBlob);
        check (xml != nullptr, "State-v1: decoded serialized XML");
        if (xml != nullptr)
        {
            if (mode == 0)
                xml->setAttribute ("wavelensStateVersion", 1);
            else
                xml->removeAttribute ("wavelensStateVersion");

            legacyBlob.reset();
            TestXmlProcessorHelper::encodeXml (*xml, legacyBlob);
        }

        auto procMigrated = std::make_unique<WaveLensProcessor>();
        procMigrated->prepareToPlay (44100.0, 256);
        procMigrated->setStateInformation (legacyBlob.getData(), (int) legacyBlob.getSize());

        check (std::abs (getParamPlain (*procMigrated, ids::layerDelay (0)) - 0.0f) < 0.05f,
               "State-v1: PRE 1 (-50 ms) migrated by +50 ms to 0.0 ms");
        check (std::abs (getParamPlain (*procMigrated, ids::layerDelay (1)) - 15.0f) < 0.05f,
               "State-v1: PRE 2 (-35 ms) migrated by +50 ms to +15.0 ms");
        check (std::abs (getParamPlain (*procMigrated, ids::layerDelay (2)) - (-25.0f)) < 0.05f,
               "State-v1: PRE 3 (-75 ms) migrated by +50 ms to -25.0 ms");
        check (std::abs (getParamPlain (*procMigrated, ids::layerDelay (3)) - 12.5f) < 0.05f,
               "State-v1: MAIN 1 delay left untouched");
        check (std::abs (getParamPlain (*procMigrated, ids::layerDelay (4)) - (-20.0f)) < 0.05f,
               "State-v1: MAIN 2 delay left untouched");
        check (procMigrated->connectedMask.load() == (1 << 0),
               "State-v1: sample reloaded during migration");

        // Re-saving migrated processor state writes v2 and must not re-migrate on reload
        juce::MemoryBlock resavedBlob;
        procMigrated->getStateInformation (resavedBlob);
        auto procReloaded = std::make_unique<WaveLensProcessor>();
        procReloaded->prepareToPlay (44100.0, 256);
        procReloaded->setStateInformation (resavedBlob.getData(), (int) resavedBlob.getSize());
        check (std::abs (getParamPlain (*procReloaded, ids::layerDelay (0)) - 0.0f) < 0.05f
            && std::abs (getParamPlain (*procReloaded, ids::layerDelay (1)) - 15.0f) < 0.05f
            && std::abs (getParamPlain (*procReloaded, ids::layerDelay (2)) - (-25.0f)) < 0.05f,
               "State-v1->v2: re-save locks version 2 and prevents double-migration");
    }

    // 3. Missing sample paths are preserved (never silently discarded) and indicator stays accurate
    juce::MemoryBlock missingBlob;
    {
        auto procMissingSrc = std::make_unique<WaveLensProcessor>();
        procMissingSrc->prepareToPlay (44100.0, 256);
        juce::String err;
        check (procMissingSrc->loadSampleFile (0, preWav, err), "State-Missing: load present sample");
        check (procMissingSrc->loadSampleFile (1, disappearingWav, err), "State-Missing: load temp sample");
        procMissingSrc->getStateInformation (missingBlob);
    }

    // Remove the file from disk before restoring state
    disappearingWav.deleteFile();
    check (! disappearingWav.existsAsFile(), "State-Missing: deleted target sample from disk");

    juce::MemoryBlock resavedMissingBlob;
    {
        auto procMissingDst = std::make_unique<WaveLensProcessor>();
        procMissingDst->prepareToPlay (44100.0, 256);
        procMissingDst->setStateInformation (missingBlob.getData(), (int) missingBlob.getSize());

        check (! procMissingDst->isLayerSampleMissing (0), "State-Missing: existing layer 0 not flagged missing");
        check (procMissingDst->isLayerSampleMissing (1), "State-Missing: deleted layer 1 flagged missing");
        check (procMissingDst->missingSampleMask.load() == (1 << 1),
               "State-Missing: missingSampleMask marks exact missing layer bit",
               "mask=" + std::to_string (procMissingDst->missingSampleMask.load()));
        check (procMissingDst->getMissingSampleFileName (1) == "vanishing_sample",
               "State-Missing: getMissingSampleFileName returns deleted stem");
        check (procMissingDst->connectedMask.load() == (1 << 0),
               "State-Missing: only existing sample remains connected");

        // Re-save without modifying layers -> samplePath2 MUST still be preserved in XML
        procMissingDst->getStateInformation (resavedMissingBlob);
        auto resavedXml = TestXmlProcessorHelper::decodeXml (resavedMissingBlob);
        check (resavedXml != nullptr
            && resavedXml->getStringAttribute ("samplePath1") == preWav.getFullPathName()
            && resavedXml->getStringAttribute ("samplePath2") == disappearingWav.getFullPathName(),
               "State-Missing: re-saving state preserves missing samplePath rather than discarding it");
    }

    // Recreate the file at the preserved path and confirm reloading restores both layers cleanly
    writeWavFileFloat32 (dir / "vanishing_sample.wav", 44100.0, { { 0.5f }, { 0.5f } });
    {
        auto procRecovered = std::make_unique<WaveLensProcessor>();
        procRecovered->prepareToPlay (44100.0, 256);
        procRecovered->setStateInformation (resavedMissingBlob.getData(), (int) resavedMissingBlob.getSize());
        check (! procRecovered->isLayerSampleMissing (1) && procRecovered->missingSampleMask.load() == 0,
               "State-Missing: missing indicator clears once preserved sample path returns on disk");
        check (procRecovered->connectedMask.load() == ((1 << 0) | (1 << 1)),
               "State-Missing: preserved path auto-reloads when file is restored");
    }
}

void testProcessorEditorLifecycle()
{
    WaveLensProcessor proc;
    proc.prepareToPlay (44100.0, 256);
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    check (editor != nullptr, "Editor: createEditor returns non-null WaveLensEditor");
    if (editor != nullptr)
    {
        check (editor->getWidth() == 1024 && editor->getHeight() == 768,
               "Editor: default Normal preset is 1024x768");
        editor->setSize (800, 600);
        check (editor->getWidth() == 800 && editor->getHeight() == 600,
               "Editor: accepts the native SVG 800x600 size");
        editor->setSize (1920, 1440);
        check (editor->getWidth() == 1920 && editor->getHeight() == 1440,
               "Editor: accepts a larger 4:3 editor size");
    }
}

void runAllProcessorTests (const fs::path& dir)
{
    testRealJuceAiffLoading (dir);
    testProcessorMonitorOversizedBlockAndDisabledBuses (dir);
    testRealProcessorStateSerializationAndMigration (dir);
    testProcessorEditorLifecycle();
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const fs::path tempDir = fs::temp_directory_path() / "spectral_multiple_processor_tests";
    fs::create_directories (tempDir);

    runAllProcessorTests (tempDir);

    fs::remove_all (tempDir);

    if (gFailures == 0)
    {
        std::cout << "[PASS] All WaveLensProcessor AIFF, monitor integration, and state migration tests passed.\n";
        return 0;
    }

    std::cerr << "[FAIL] " << gFailures << " processor check(s) failed.\n";
    return 1;
}
