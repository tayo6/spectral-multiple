#pragma once

#include "PluginProcessor.h"
#include "SpectralMultipleLookAndFeel.h"
#include <array>
#include <functional>

class PluginHeaderComponent final : public juce::Component
{
public:
    explicit PluginHeaderComponent (WaveLensProcessor& processor);

    std::function<void()> onResetScope;
    std::function<void()> onResetZoom;
    std::function<void (double newBpm)> onBpmChanged;
    std::function<void()> onSampleStateChanged;

    double getBpm() const noexcept { return currentBpm; }
    void setBpm (double nextBpm);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class IconButton final : public juce::Button
    {
    public:
        enum class Kind { Power, Menu, Heart };

        IconButton (Kind k, juce::String tip)
            : juce::Button ("Icon"), kind (k)
        {
            setTooltip (std::move (tip));
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            if (kind == Kind::Power || kind == Kind::Heart)
                setClickingTogglesState (true);
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

    private:
        Kind kind;
    };

    void applyPreset (int presetIndex);
    void stepPreset (int delta);
    void openMainMenu();

    WaveLensProcessor& proc;
    double currentBpm = 120.0;
    std::array<bool, 6> favouritePresets { false, true, false, false, true, false };

    IconButton powerBtn { IconButton::Kind::Power, "Bypass / Power toggle" };
    IconButton menuBtn  { IconButton::Kind::Menu,  "Spectral Multiple menu" };
    IconButton favBtn   { IconButton::Kind::Heart, "Favourite current preset" };
    juce::TextButton resetBtn { "RESET" };
    juce::TextButton prevPresetBtn { "<" };
    juce::ComboBox presetBox;
    juce::TextButton nextPresetBtn { ">" };
    juce::TextButton bpmDownBtn { "-" };
    juce::TextButton bpmUpBtn { "+" };
    juce::Rectangle<int> bpmValueBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHeaderComponent)
};
