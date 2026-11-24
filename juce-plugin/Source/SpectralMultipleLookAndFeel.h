#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>

namespace wavelens::ui
{
    inline const juce::Colour kMainBg         { 0xff0b0a10 };
    inline const juce::Colour kHeaderBg       { 0xff09090d };
    inline const juce::Colour kPanelBg        { 0xff15141c };
    inline const juce::Colour kControlBg      { 0xff1c1a24 };
    inline const juce::Colour kHoverBg        { 0xff24212e };
    inline const juce::Colour kBorder         { 0xff302c3a };
    inline const juce::Colour kMainText       { 0xfff0edf5 };
    inline const juce::Colour kSecondaryText  { 0xff9692a3 };
    inline const juce::Colour kDisabledText   { 0xff55515f };
    inline const juce::Colour kPurpleAccent   { 0xffb477ff };
    inline const juce::Colour kSelectedAccent { 0xffa66bfa };
    inline const juce::Colour kCyanAccent     { 0xff43d5d0 };
    inline const juce::Colour kInputWave      { 0xffa2a0aa };
    inline const juce::Colour kMasterWave     { 0xff48d6d0 };
    inline const juce::Colour kWarningAmber   { 0xffffb86c };

    inline juce::Colour layerColour (int layer) noexcept
    {
        static constexpr std::array<juce::uint32, 6> kPalette {
            0xffa78bfa, // SNAPBACK 1
            0xff8b5cf6, // SNAPBACK 2
            0xff6366f1, // SNAPBACK 3
            0xfff472b6, // TRANSIENT 1
            0xffe879f9, // TRANSIENT 2
            0xff22d3ee  // TRANSIENT 3
        };
        return juce::Colour (kPalette[(size_t) juce::jlimit (0, 5, layer)]);
    }

    inline juce::String layerShortName (int layer)
    {
        if (layer < 3)
            return "SB " + juce::String (layer + 1);
        return "TR " + juce::String (layer - 2);
    }

    inline juce::String layerFullName (int layer)
    {
        if (layer < 3)
            return "Snapback " + juce::String (layer + 1);
        return "Transient " + juce::String (layer - 2);
    }
}

//==============================================================================
class SpectralMultipleLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SpectralMultipleLookAndFeel();

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColourToUse) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
};
