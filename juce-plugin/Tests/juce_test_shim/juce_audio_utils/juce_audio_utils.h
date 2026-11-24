#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace juce
{
using int64 = std::int64_t;
using uint32 = std::uint32_t;
using uint8 = std::uint8_t;

template <typename T>
struct MathConstants
{
    static constexpr T pi = static_cast<T> (3.14159265358979323846);
    static constexpr T twoPi = static_cast<T> (6.28318530717958647692);
};

template <typename T>
inline T jmin (T a, T b) { return a < b ? a : b; }

template <typename T>
inline T jmax (T a, T b) { return a > b ? a : b; }

template <typename T>
inline T jlimit (T lo, T hi, T v)
{
    assert (lo <= hi);
    return v < lo ? lo : (v > hi ? hi : v);
}

inline bool isPositiveAndBelow (int v, int limit) noexcept { return v >= 0 && v < limit; }

template <typename T>
inline int roundToInt (T v) noexcept { return static_cast<int> (std::llround (v)); }

#ifndef jassert
#define jassert(expr) assert(expr)
#endif

#ifndef JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR
#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClassName) \
    ClassName (const ClassName&) = delete; \
    ClassName& operator= (const ClassName&) = delete;
#endif

class String
{
public:
    String() = default;
    String (const char* s) : data (s != nullptr ? s : "") {}
    String (std::string s) : data (std::move (s)) {}

    bool isEmpty() const noexcept { return data.empty(); }
    bool isNotEmpty() const noexcept { return ! data.empty(); }
    void clear() noexcept { data.clear(); }
    const std::string& toStdString() const noexcept { return data; }
    bool operator== (const String& o) const noexcept { return data == o.data; }
    bool operator!= (const String& o) const noexcept { return data != o.data; }

    String operator+ (const String& o) const { return String (data + o.data); }

private:
    std::string data;
};

class CriticalSection
{
public:
    void enter() const { mtx.lock(); }
    void exit() const { mtx.unlock(); }

private:
    mutable std::recursive_mutex mtx;
};

class ScopedLock
{
public:
    explicit ScopedLock (const CriticalSection& csIn) : cs (csIn) { cs.enter(); }
    ~ScopedLock() { cs.exit(); }

private:
    const CriticalSection& cs;
};

class File
{
public:
    File() = default;
    explicit File (std::string p) : path (std::move (p)) {}

    bool existsAsFile() const
    {
        if (path.empty())
            return false;
        std::ifstream f (path, std::ios::binary);
        return f.good();
    }

    String getFullPathName() const { return String (path); }

    String getFileNameWithoutExtension() const
    {
        auto slash = path.find_last_of ("/\\");
        std::string base = (slash == std::string::npos) ? path : path.substr (slash + 1);
        auto dot = base.find_last_of ('.');
        if (dot != std::string::npos)
            base = base.substr (0, dot);
        return String (base);
    }

    const std::string& getPath() const noexcept { return path; }

private:
    std::string path;
};

template <typename SampleType>
class AudioBuffer
{
public:
    AudioBuffer() = default;

    AudioBuffer (int channels, int samples)
    {
        setSize (channels, samples, false, true, false);
    }

    AudioBuffer (SampleType* const* dataToReferTo, int numCh, int numSamplesIn)
    {
        setDataToReferTo (dataToReferTo, numCh, numSamplesIn);
    }

    void setSize (int channels, int samples, bool keepExistingContent = false,
                  bool clearExtraSpace = false, bool avoidRealloc = false)
    {
        (void) avoidRealloc;
        if (! keepExistingContent)
        {
            owned.assign (static_cast<size_t> (std::max (0, channels)),
                          std::vector<SampleType> (static_cast<size_t> (std::max (0, samples)), SampleType (0)));
        }
        else
        {
            owned.resize (static_cast<size_t> (std::max (0, channels)));
            for (auto& ch : owned)
                ch.resize (static_cast<size_t> (std::max (0, samples)),
                           clearExtraSpace ? SampleType (0) : SampleType (0));
        }

        numChannels = std::max (0, channels);
        numSamples = std::max (0, samples);
        ptrs.resize (static_cast<size_t> (numChannels));
        for (int c = 0; c < numChannels; ++c)
            ptrs[static_cast<size_t> (c)] = owned[static_cast<size_t> (c)].data();
    }

    void setDataToReferTo (SampleType* const* dataToReferTo, int numCh, int numSamplesIn)
    {
        numChannels = std::max (0, numCh);
        numSamples = std::max (0, numSamplesIn);
        ptrs.resize (static_cast<size_t> (numChannels));
        for (int c = 0; c < numChannels; ++c)
            ptrs[static_cast<size_t> (c)] = dataToReferTo[c];
    }

    int getNumChannels() const noexcept { return numChannels; }
    int getNumSamples() const noexcept { return numSamples; }

    SampleType getSample (int ch, int idx) const noexcept
    {
        assert (ch >= 0 && ch < numChannels && idx >= 0 && idx < numSamples);
        return ptrs[static_cast<size_t> (ch)][idx];
    }

    void setSample (int ch, int idx, SampleType value) noexcept
    {
        assert (ch >= 0 && ch < numChannels && idx >= 0 && idx < numSamples);
        ptrs[static_cast<size_t> (ch)][idx] = value;
    }

    const SampleType* getReadPointer (int ch) const noexcept { return ptrs[static_cast<size_t> (ch)]; }
    SampleType* getWritePointer (int ch) noexcept { return ptrs[static_cast<size_t> (ch)]; }

    void clear() noexcept
    {
        clear (0, numSamples);
    }

    void clear (int startSample, int count) noexcept
    {
        const int end = std::min (numSamples, startSample + count);
        for (int c = 0; c < numChannels; ++c)
            if (ptrs[static_cast<size_t> (c)] != nullptr && end > startSample)
                std::fill (ptrs[static_cast<size_t> (c)] + startSample,
                           ptrs[static_cast<size_t> (c)] + end, SampleType (0));
    }

    void copyFrom (int destChannel, int destStartSample,
                   const AudioBuffer& source, int sourceChannel, int sourceStartSample,
                   int numSamplesToCopy) noexcept
    {
        for (int i = 0; i < numSamplesToCopy; ++i)
            ptrs[static_cast<size_t> (destChannel)][destStartSample + i]
                = source.getSample (sourceChannel, sourceStartSample + i);
    }

private:
    int numChannels = 0;
    int numSamples = 0;
    std::vector<std::vector<SampleType>> owned;
    std::vector<SampleType*> ptrs;
};

class MidiMessage
{
public:
    static MidiMessage noteOn (int /*channel*/, int noteNumber, uint8 velocity) noexcept
    {
        MidiMessage m;
        m.noteOnFlag = velocity > 0;
        m.note = noteNumber;
        return m;
    }

    bool isNoteOn() const noexcept { return noteOnFlag; }
    int getNoteNumber() const noexcept { return note; }

private:
    bool noteOnFlag = false;
    int note = 60;
};

class MidiBuffer
{
public:
    struct Event
    {
        int samplePosition = 0;
        MidiMessage message;
    };

    void addEvent (const MidiMessage& msg, int samplePosition)
    {
        events.push_back ({ samplePosition, msg });
        std::stable_sort (events.begin(), events.end(),
                          [] (const Event& a, const Event& b) { return a.samplePosition < b.samplePosition; });
    }

    void clear() noexcept { events.clear(); }

    class Iterator
    {
    public:
        explicit Iterator (const MidiBuffer& b) : buf (b) {}

        bool getNextEvent (MidiMessage& result, int& samplePosition) noexcept
        {
            if (index >= buf.events.size())
                return false;
            result = buf.events[index].message;
            samplePosition = buf.events[index].samplePosition;
            ++index;
            return true;
        }

    private:
        const MidiBuffer& buf;
        size_t index = 0;
    };

private:
    std::vector<Event> events;
};

class AudioFormatReader
{
public:
    virtual ~AudioFormatReader() = default;

    double sampleRate = 0.0;
    int64 lengthInSamples = 0;
    unsigned int numChannels = 0;

    virtual bool read (AudioBuffer<float>* buffer, int destStartSample, int numSamples,
                       int64 readerStartSample, bool useReaderLeftChan, bool useReaderRightChan) = 0;
};

class InMemoryReader final : public AudioFormatReader
{
public:
    InMemoryReader (double srIn, int chIn, std::vector<std::vector<float>> pcm)
        : channels (std::move (pcm))
    {
        sampleRate = srIn;
        numChannels = static_cast<unsigned int> (chIn);
        lengthInSamples = channels.empty() ? 0 : static_cast<int64> (channels[0].size());
    }

    bool read (AudioBuffer<float>* buffer, int destStartSample, int numSamples,
               int64 readerStartSample, bool useLeft, bool useRight) override
    {
        if (buffer == nullptr || numChannels == 0)
            return false;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto srcIdx = static_cast<size_t> (readerStartSample + i);
            if (srcIdx >= channels[0].size())
                return false;

            if (useLeft && buffer->getNumChannels() > 0)
                buffer->setSample (0, destStartSample + i, channels[0][srcIdx]);
            if (useRight && buffer->getNumChannels() > 1)
            {
                const size_t rCh = (channels.size() > 1) ? 1 : 0;
                buffer->setSample (1, destStartSample + i, channels[rCh][srcIdx]);
            }
        }
        return true;
    }

private:
    std::vector<std::vector<float>> channels;
};

class AudioFormatManager
{
public:
    void registerBasicFormats() {}

    std::unique_ptr<AudioFormatReader> createReaderFor (const File& file)
    {
        std::ifstream f (file.getPath(), std::ios::binary);
        if (! f.good())
            return nullptr;

        std::vector<uint8> bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
        if (bytes.size() < 12)
            return nullptr;

        auto u16le = [&] (size_t off) -> std::uint16_t {
            return static_cast<std::uint16_t> (bytes[off] | (bytes[off + 1] << 8));
        };
        auto u32le = [&] (size_t off) -> std::uint32_t {
            return static_cast<std::uint32_t> (bytes[off] | (bytes[off + 1] << 8)
                                               | (bytes[off + 2] << 16) | (bytes[off + 3] << 24));
        };
        auto u16be = [&] (size_t off) -> std::uint16_t {
            return static_cast<std::uint16_t> ((bytes[off] << 8) | bytes[off + 1]);
        };
        auto u32be = [&] (size_t off) -> std::uint32_t {
            return static_cast<std::uint32_t> ((bytes[off] << 24) | (bytes[off + 1] << 16)
                                               | (bytes[off + 2] << 8) | bytes[off + 3]);
        };

        // RIFF / WAVE parser
        if (std::memcmp (bytes.data(), "RIFF", 4) == 0 && std::memcmp (bytes.data() + 8, "WAVE", 4) == 0)
        {
            std::uint16_t audioFormat = 0, channels = 0, bitsPerSample = 0;
            std::uint32_t sr = 0;
            size_t dataOffset = 0, dataSize = 0;
            size_t pos = 12;
            while (pos + 8 <= bytes.size())
            {
                const std::uint32_t sz = u32le (pos + 4);
                if (std::memcmp (bytes.data() + pos, "fmt ", 4) == 0 && pos + 8 + 16 <= bytes.size())
                {
                    audioFormat = u16le (pos + 8);
                    channels = u16le (pos + 10);
                    sr = u32le (pos + 12);
                    bitsPerSample = u16le (pos + 22);
                }
                else if (std::memcmp (bytes.data() + pos, "data", 4) == 0)
                {
                    dataOffset = pos + 8;
                    dataSize = std::min (static_cast<size_t> (sz), bytes.size() - dataOffset);
                }
                pos += 8 + sz + (sz & 1u);
            }

            if (channels == 0 || sr == 0 || dataOffset == 0)
                return nullptr;

            const size_t bytesPerSample = bitsPerSample / 8;
            if (bytesPerSample == 0)
                return nullptr;

            const size_t frameCount = dataSize / (channels * bytesPerSample);
            std::vector<std::vector<float>> pcm (channels, std::vector<float> (frameCount, 0.0f));
            for (size_t i = 0; i < frameCount; ++i)
            {
                for (size_t c = 0; c < channels; ++c)
                {
                    const size_t off = dataOffset + (i * channels + c) * bytesPerSample;
                    if (audioFormat == 1 && bitsPerSample == 16)
                    {
                        auto raw = static_cast<std::int16_t> (u16le (off));
                        pcm[c][i] = static_cast<float> (raw) / 32768.0f;
                    }
                    else if (audioFormat == 3 && bitsPerSample == 32)
                    {
                        float val = 0.0f;
                        std::memcpy (&val, bytes.data() + off, 4);
                        pcm[c][i] = val;
                    }
                    else
                    {
                        return nullptr;
                    }
                }
            }
            return std::make_unique<InMemoryReader> (static_cast<double> (sr), channels, std::move (pcm));
        }

        // FORM / AIFF parser
        if (std::memcmp (bytes.data(), "FORM", 4) == 0 && std::memcmp (bytes.data() + 8, "AIFF", 4) == 0)
        {
            std::uint16_t channels = 0, bitsPerSample = 0;
            std::uint32_t sampleFrames = 0;
            double sr = 0.0;
            size_t ssndOffset = 0, ssndSize = 0;
            size_t pos = 12;
            while (pos + 8 <= bytes.size())
            {
                const std::uint32_t sz = u32be (pos + 4);
                if (std::memcmp (bytes.data() + pos, "COMM", 4) == 0 && pos + 8 + 18 <= bytes.size())
                {
                    channels = u16be (pos + 8);
                    sampleFrames = u32be (pos + 10);
                    bitsPerSample = u16be (pos + 14);
                    // 80-bit IEEE 754 extended float sample rate
                    const std::uint16_t exp = u16be (pos + 16);
                    std::uint64_t mant = 0;
                    for (int k = 0; k < 8; ++k)
                        mant = (mant << 8) | bytes[pos + 18 + static_cast<size_t> (k)];
                    const int e = static_cast<int> (exp & 0x7fff) - 16383;
                    sr = std::ldexp (static_cast<double> (mant), e - 63);
                }
                else if (std::memcmp (bytes.data() + pos, "SSND", 4) == 0 && pos + 16 <= bytes.size())
                {
                    const std::uint32_t offsetField = u32be (pos + 8);
                    ssndOffset = pos + 16 + offsetField;
                    ssndSize = std::min (static_cast<size_t> (sz - 8 - offsetField), bytes.size() - ssndOffset);
                }
                pos += 8 + sz + (sz & 1u);
            }

            if (channels == 0 || sr <= 0.0 || ssndOffset == 0 || bitsPerSample != 16)
                return nullptr;

            const size_t frameCount = std::min (static_cast<size_t> (sampleFrames), ssndSize / (channels * 2));
            std::vector<std::vector<float>> pcm (channels, std::vector<float> (frameCount, 0.0f));
            for (size_t i = 0; i < frameCount; ++i)
            {
                for (size_t c = 0; c < channels; ++c)
                {
                    const size_t off = ssndOffset + (i * channels + c) * 2;
                    auto raw = static_cast<std::int16_t> (u16be (off));
                    pcm[c][i] = static_cast<float> (raw) / 32768.0f;
                }
            }
            return std::make_unique<InMemoryReader> (sr, channels, std::move (pcm));
        }

        return nullptr;
    }
};
} // namespace juce
