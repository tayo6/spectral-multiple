#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <cmath>
#include <vector>

namespace juce::dsp
{
struct ProcessSpec
{
    double sampleRate = 44100.0;
    juce::uint32 maximumBlockSize = 512;
    juce::uint32 numChannels = 2;
};

enum class LinkwitzRileyFilterType
{
    lowpass,
    highpass
};

// 4th-order Linkwitz-Riley (two cascaded 2nd-order Butterworth biquads per channel).
template <typename SampleType>
class LinkwitzRileyFilter
{
public:
    void prepare (const ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
        channels = std::max<size_t> (1, spec.numChannels);
        s1.assign (channels, BiquadState {});
        s2.assign (channels, BiquadState {});
        updateCoefficients();
        reset();
    }

    void setType (LinkwitzRileyFilterType t) noexcept
    {
        filterType = t;
        updateCoefficients();
    }

    void setCutoffFrequency (SampleType freqHz) noexcept
    {
        cutoff = static_cast<double> (freqHz);
        updateCoefficients();
    }

    void reset() noexcept
    {
        for (auto& st : s1) st = {};
        for (auto& st : s2) st = {};
    }

    SampleType processSample (int channel, SampleType input) noexcept
    {
        if (channel < 0 || static_cast<size_t> (channel) >= channels)
            return input;

        const double y1 = runBiquad (static_cast<double> (input), s1[static_cast<size_t> (channel)]);
        const double y2 = runBiquad (y1, s2[static_cast<size_t> (channel)]);
        return static_cast<SampleType> (y2);
    }

private:
    struct BiquadState { double z1 = 0.0, z2 = 0.0; };

    void updateCoefficients() noexcept
    {
        const double sr = sampleRate > 1.0 ? sampleRate : 44100.0;
        const double fc = std::max (1.0, std::min (cutoff, sr * 0.49));
        const double w0 = 2.0 * 3.14159265358979323846 * fc / sr;
        const double cosW = std::cos (w0);
        const double sinW = std::sin (w0);
        const double alpha = sinW / std::sqrt (2.0); // Butterworth Q = 1 / sqrt(2)

        const double a0 = 1.0 + alpha;
        if (filterType == LinkwitzRileyFilterType::lowpass)
        {
            b0 = ((1.0 - cosW) * 0.5) / a0;
            b1 = (1.0 - cosW) / a0;
            b2 = b0;
        }
        else
        {
            b0 = ((1.0 + cosW) * 0.5) / a0;
            b1 = -(1.0 + cosW) / a0;
            b2 = b0;
        }
        a1 = (-2.0 * cosW) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    double runBiquad (double x, BiquadState& st) const noexcept
    {
        const double y = b0 * x + st.z1;
        st.z1 = b1 * x - a1 * y + st.z2;
        st.z2 = b2 * x - a2 * y;
        return y;
    }

    double sampleRate = 44100.0;
    double cutoff = 1000.0;
    size_t channels = 2;
    LinkwitzRileyFilterType filterType = LinkwitzRileyFilterType::lowpass;
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    std::vector<BiquadState> s1, s2;
};
} // namespace juce::dsp
