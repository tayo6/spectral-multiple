#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "SnapbackTheme.h"
#include <array>

/**
    UI colour aliases for the SNAPBACK flat near-black theme.
    All values come from snap:: (SnapbackTheme.h) - do not define new raw
    colours here; extend SnapbackTheme.h instead so the palette stays in one place.
*/
namespace wavelens::ui
{
    inline const juce::Colour kMainBg         { snap::bg };
    inline const juce::Colour kHeaderBg       { snap::panelHi };
    inline const juce::Colour kPanelBg        { snap::panel };
    inline const juce::Colour kControlBg      { snap::tile };
    inline const juce::Colour kHoverBg        { juce::Colour (0xff23232b) };
    inline const juce::Colour kBorder         { snap::border };
    inline const juce::Colour kElevatedBg     { snap::panelHi };
    inline const juce::Colour kInsetBg        { snap::inset };
    inline const juce::Colour kMainText       { snap::text };
    inline const juce::Colour kSecondaryText  { snap::muted };
    inline const juce::Colour kDisabledText   { snap::disabled };
    inline const juce::Colour kTileLabel      { snap::tileLabel };
    inline const juce::Colour kOnAccentText   { snap::onAccent };
    inline const juce::Colour kNeutralWave    { snap::wave };
    inline const juce::Colour kGridLine       { snap::grid };
    inline const juce::Colour kPurpleAccent   { snap::snap };     // generic accent: SNAPBACK blue
    inline const juce::Colour kSelectedAccent { snap::snapHi };   // bright bank highlight
    inline const juce::Colour kCyanAccent     { snap::snap };     // trigger indicator accent
    inline const juce::Colour kInputWave      { snap::wave };
    inline const juce::Colour kMasterWave     { snap::snapHi };
    inline const juce::Colour kWarningAmber   { snap::warn };

    inline juce::Colour layerColour (int layer) noexcept
    {
        return snap::signalColour (layer);
    }

    inline juce::Colour bankAccent (int layer) noexcept
    {
        return snap::bankAccent (layer);
    }

    inline juce::Colour bankAccentHi (int layer) noexcept
    {
        return snap::bankAccentHi (layer);
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

private:
    void drawTileSlider (juce::Graphics&, juce::Rectangle<float> tile, juce::Slider&,
                         float proportion);
};
