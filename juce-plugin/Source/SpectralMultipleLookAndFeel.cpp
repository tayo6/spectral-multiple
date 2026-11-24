#include "SpectralMultipleLookAndFeel.h"

namespace ui = wavelens::ui;

SpectralMultipleLookAndFeel::SpectralMultipleLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, ui::kMainBg);

    setColour (juce::ComboBox::backgroundColourId, ui::kControlBg);
    setColour (juce::ComboBox::textColourId, ui::kMainText);
    setColour (juce::ComboBox::outlineColourId, ui::kBorder);
    setColour (juce::ComboBox::arrowColourId, ui::kSecondaryText);

    setColour (juce::PopupMenu::backgroundColourId, ui::kPanelBg);
    setColour (juce::PopupMenu::textColourId, ui::kMainText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::kHoverBg);
    setColour (juce::PopupMenu::highlightedTextColourId, ui::kMainText);
    setColour (juce::PopupMenu::headerTextColourId, ui::kPurpleAccent);

    setColour (juce::TextButton::buttonColourId, ui::kControlBg);
    setColour (juce::TextButton::buttonOnColourId, ui::kSelectedAccent.withAlpha (0.24f));
    setColour (juce::TextButton::textColourOffId, ui::kSecondaryText);
    setColour (juce::TextButton::textColourOnId, ui::kMainText);

    setColour (juce::Label::textColourId, ui::kMainText);
    setColour (juce::Slider::textBoxTextColourId, ui::kMainText);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, ui::kSelectedAccent.withAlpha (0.35f));

    setColour (juce::TextEditor::backgroundColourId, ui::kMainBg);
    setColour (juce::TextEditor::textColourId, ui::kMainText);
    setColour (juce::TextEditor::outlineColourId, ui::kPurpleAccent);
    setColour (juce::TextEditor::focusedOutlineColourId, ui::kPurpleAccent);
}

void SpectralMultipleLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                                    float sliderPos, float rotaryStartAngle,
                                                    float rotaryEndAngle, juce::Slider& slider)
{
    const float diameter = (float) juce::jmin (width, height) - 4.0f;
    if (diameter < 8.0f)
        return;

    const float radius = diameter * 0.5f;
    const float cx = (float) x + (float) width * 0.5f;
    const float cy = (float) y + (float) height * 0.5f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const bool isBipolar = static_cast<bool> (slider.getProperties().getWithDefault ("bipolar", false));
    const float zeroAngle = rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle);
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

    // Outer recessed track
    juce::Path bgArc;
    bgArc.addCentredArc (cx, cy, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (ui::kBorder.brighter (0.12f));
    g.strokePath (bgArc, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Active value arc
    const float arcStart = isBipolar ? juce::jmin (zeroAngle, angle) : rotaryStartAngle;
    const float arcEnd   = isBipolar ? juce::jmax (zeroAngle, angle) : angle;
    if (std::abs (arcEnd - arcStart) > 0.01f)
    {
        juce::Path valArc;
        valArc.addCentredArc (cx, cy, radius, radius, 0.0f, arcStart, arcEnd, true);
        g.setColour (slider.isEnabled() ? accent : ui::kDisabledText);
        g.strokePath (valArc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Inner dark cap
    const float innerR = juce::jmax (3.5f, radius - 3.6f);
    g.setColour (ui::kControlBg);
    g.fillEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f);
    g.setColour (ui::kBorder);
    g.drawEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f, 1.0f);

    // Pointer line
    const float px0 = cx + (innerR * 0.25f) * std::sin (angle);
    const float py0 = cy - (innerR * 0.25f) * std::cos (angle);
    const float px1 = cx + (innerR - 1.5f) * std::sin (angle);
    const float py1 = cy - (innerR - 1.5f) * std::cos (angle);
    g.setColour (slider.isEnabled() ? ui::kMainText : ui::kDisabledText);
    g.drawLine (px0, py0, px1, py1, 1.8f);
}

void SpectralMultipleLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                                    float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                                    juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto accent = slider.findColour (juce::Slider::trackColourId).isTransparent()
        ? ui::kPurpleAccent
        : slider.findColour (juce::Slider::trackColourId);

    if (style == juce::Slider::LinearVertical)
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const float trackW = 5.0f;
        const float cx = b.getCentreX();
        const float top = b.getY() + 6.0f;
        const float bot = b.getBottom() - 6.0f;
        const float trackH = juce::jmax (8.0f, bot - top);

        // Scale ticks
        g.setColour (ui::kBorder);
        for (int i = 0; i <= 4; ++i)
        {
            const float ty = top + trackH * ((float) i / 4.0f);
            g.drawHorizontalLine (juce::roundToInt (ty), cx - 8.5f, cx - 4.5f);
            g.drawHorizontalLine (juce::roundToInt (ty), cx + 4.5f, cx + 8.5f);
        }

        // Groove
        juce::Rectangle<float> groove (cx - trackW * 0.5f, top, trackW, trackH);
        g.setColour (ui::kMainBg);
        g.fillRoundedRectangle (groove, 2.0f);
        g.setColour (ui::kBorder);
        g.drawRoundedRectangle (groove, 2.0f, 1.0f);

        // Fill
        const float clampedPos = juce::jlimit (top, bot, sliderPos);
        juce::Rectangle<float> fillRect (cx - trackW * 0.5f + 0.5f, clampedPos,
                                         trackW - 1.0f, bot - clampedPos);
        if (fillRect.getHeight() > 0.5f)
        {
            g.setColour (accent.withAlpha (0.85f));
            g.fillRoundedRectangle (fillRect, 1.5f);
        }

        // Compact Snapback fader cap
        juce::Rectangle<float> cap (cx - 9.0f, clampedPos - 4.5f, 18.0f, 9.0f);
        g.setColour (ui::kHoverBg.brighter (0.14f));
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (accent.withAlpha (0.9f));
        g.drawRoundedRectangle (cap, 2.0f, 1.0f);
        g.setColour (ui::kMainText);
        g.drawHorizontalLine (juce::roundToInt (clampedPos), cap.getX() + 2.5f, cap.getRight() - 2.5f);
        return;
    }

    // Horizontal dark bar slider
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float trackH = 4.0f;
    const float cy = b.getCentreY();
    const float left = b.getX() + 4.0f;
    const float right = b.getRight() - 4.0f;

    juce::Rectangle<float> track (left, cy - trackH * 0.5f, juce::jmax (8.0f, right - left), trackH);
    g.setColour (ui::kMainBg);
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (ui::kBorder);
    g.drawRoundedRectangle (track, 2.0f, 1.0f);

    const float clampedX = juce::jlimit (track.getX(), track.getRight(), sliderPos);
    g.setColour (accent);
    g.fillRoundedRectangle (track.withWidth (juce::jmax (2.0f, clampedX - track.getX())), 2.0f);

    // Thumb handle
    juce::Rectangle<float> thumb (clampedX - 3.5f, cy - 6.0f, 7.0f, 12.0f);
    g.setColour (ui::kMainText);
    g.fillRoundedRectangle (thumb, 2.0f);
    g.setColour (accent.darker (0.25f));
    g.drawRoundedRectangle (thumb, 2.0f, 1.0f);
}

void SpectralMultipleLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                        const juce::Colour& /*backgroundColour*/,
                                                        bool shouldDrawButtonAsHighlighted,
                                                        bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool isToggleOn = button.getToggleState();

    if (static_cast<bool> (button.getProperties().getWithDefault ("isFlatSegment", false)))
    {
        auto fill = isToggleOn ? ui::kSelectedAccent.withAlpha (0.24f) : ui::kControlBg;
        if (shouldDrawButtonAsDown) fill = fill.brighter (0.14f);
        else if (shouldDrawButtonAsHighlighted) fill = isToggleOn ? fill.brighter (0.08f) : ui::kHoverBg;

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (isToggleOn ? ui::kPurpleAccent : ui::kBorder);
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
        return;
    }

    const juce::var accentVar = button.getProperties().getWithDefault ("accentColour", juce::var());
    const juce::Colour customAccent = accentVar.isInt() || accentVar.isInt64()
        ? juce::Colour ((juce::uint32) (juce::int64) accentVar)
        : ui::kPurpleAccent;

    juce::Colour fill = isToggleOn ? customAccent.withAlpha (0.20f) : ui::kControlBg;
    if (! button.isEnabled())
        fill = ui::kPanelBg;
    else if (shouldDrawButtonAsDown)
        fill = fill.brighter (0.16f);
    else if (shouldDrawButtonAsHighlighted)
        fill = isToggleOn ? customAccent.withAlpha (0.28f) : ui::kHoverBg;

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 3.5f);

    g.setColour (! button.isEnabled() ? ui::kBorder.withAlpha (0.45f)
                                      : (isToggleOn ? customAccent.withAlpha (0.85f) : ui::kBorder));
    g.drawRoundedRectangle (bounds, 3.5f, 1.0f);
}

void SpectralMultipleLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                                  bool shouldDrawButtonAsHighlighted,
                                                  bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    const auto font = getTextButtonFont (button, button.getHeight());
    g.setFont (font);

    const bool active = button.getToggleState();
    auto col = ! button.isEnabled()
        ? ui::kDisabledText
        : (active ? button.findColour (juce::TextButton::textColourOnId)
                  : button.findColour (juce::TextButton::textColourOffId));

    g.setColour (col);
    const int xIndent = juce::jmin (4, button.proportionOfWidth (0.2f));
    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().reduced (xIndent, 1),
                      juce::Justification::centred, 1);
}

void SpectralMultipleLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                                int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                                                juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (isButtonDown ? ui::kHoverBg : ui::kControlBg);
    g.fillRoundedRectangle (bounds, 3.5f);
    g.setColour (box.isEnabled() ? ui::kBorder : ui::kBorder.withAlpha (0.45f));
    g.drawRoundedRectangle (bounds, 3.5f, 1.0f);

    // Compact chevron on the right
    const float arrowX = (float) width - 14.0f;
    const float arrowY = (float) height * 0.5f - 1.5f;
    juce::Path chevron;
    chevron.startNewSubPath (arrowX, arrowY);
    chevron.lineTo (arrowX + 3.5f, arrowY + 3.5f);
    chevron.lineTo (arrowX + 7.0f, arrowY);

    g.setColour (box.isEnabled() ? ui::kSecondaryText : ui::kDisabledText);
    g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void SpectralMultipleLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (ui::kPanelBg);
    g.fillRect (b);
    g.setColour (ui::kBorder);
    g.drawRect (b, 1.0f);
}

void SpectralMultipleLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                    bool isSeparator, bool isActive, bool isHighlighted,
                                                    bool isTicked, bool hasSubMenu,
                                                    const juce::String& text, const juce::String& shortcutKeyText,
                                                    const juce::Drawable* /*icon*/, const juce::Colour* textColourToUse)
{
    if (isSeparator)
    {
        auto r = area.reduced (8, 0);
        g.setColour (ui::kBorder);
        g.drawHorizontalLine (r.getCentreY(), (float) r.getX(), (float) r.getRight());
        return;
    }

    auto r = area.reduced (3, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (ui::kSelectedAccent.withAlpha (0.24f));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (ui::kPurpleAccent.withAlpha (0.65f));
        g.drawRoundedRectangle (r.toFloat(), 3.0f, 1.0f);
    }

    auto textArea = r.reduced (8, 0);
    if (isTicked)
    {
        g.setColour (ui::kPurpleAccent);
        g.fillEllipse ((float) textArea.getX(), (float) textArea.getCentreY() - 3.0f, 6.0f, 6.0f);
    }
    textArea.removeFromLeft (12);

    g.setFont (getPopupMenuFont());
    const auto col = textColourToUse != nullptr
        ? *textColourToUse
        : (! isActive ? ui::kDisabledText : (isTicked || isHighlighted ? ui::kMainText : ui::kSecondaryText));
    g.setColour (col);
    g.drawFittedText (text, textArea, juce::Justification::centredLeft, 1);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (ui::kSecondaryText);
        g.drawText (shortcutKeyText, textArea, juce::Justification::centredRight, false);
    }
    if (hasSubMenu)
    {
        const float sx = (float) r.getRight() - 10.0f;
        const float sy = (float) r.getCentreY();
        juce::Path tri;
        tri.addTriangle (sx, sy - 3.0f, sx, sy + 3.0f, sx + 4.0f, sy);
        g.setColour (ui::kSecondaryText);
        g.fillPath (tri);
    }
}

juce::Font SpectralMultipleLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return juce::Font (juce::jlimit (9.5f, 11.5f, (float) box.getHeight() * 0.42f));
}

juce::Font SpectralMultipleLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::jlimit (9.5f, 11.5f, (float) buttonHeight * 0.42f), juce::Font::bold);
}

juce::Font SpectralMultipleLookAndFeel::getPopupMenuFont()
{
    return juce::Font (11.5f);
}
