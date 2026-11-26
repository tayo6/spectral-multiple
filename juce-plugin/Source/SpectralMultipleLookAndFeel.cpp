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

void SpectralMultipleLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                                    float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                                    juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto accent = slider.findColour (juce::Slider::trackColourId).isTransparent()
        ? ui::kPurpleAccent
        : slider.findColour (juce::Slider::trackColourId);
    const bool hovered = slider.isMouseOverOrDragging();
    const bool isBipolar = static_cast<bool> (slider.getProperties().getWithDefault ("bipolar", false));

    // The compact Pitch control lives in the waveform readout strip. Keep its
    // title, value and live track in separate vertical zones.
    if (static_cast<bool> (slider.getProperties().getWithDefault ("compactPitchReadout", false)))
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const float scale = juce::jmax (0.55f, bounds.getHeight() / 20.0f);
        const float proportion = (float) slider.valueToProportionOfLength (slider.getValue());
        auto titleArea = juce::Rectangle<int> (juce::roundToInt (2.0f * scale), 0,
                                               juce::roundToInt (26.0f * scale),
                                               juce::roundToInt (11.0f * scale));
        auto valueArea = juce::Rectangle<int> (titleArea.getRight(),
                                               0,
                                               juce::jmax (1, width - titleArea.getRight() - juce::roundToInt (2.0f * scale)),
                                               juce::roundToInt (11.0f * scale));
        g.setColour (ui::kSecondaryText);
        g.setFont (juce::Font (7.8f * scale, juce::Font::bold));
        g.drawFittedText ("Pitch", titleArea, juce::Justification::centredLeft, 1);
        g.setColour (slider.isEnabled() ? ui::kMainText : ui::kDisabledText);
        g.setFont (juce::Font (8.8f * scale, juce::Font::bold));
        g.drawFittedText (slider.getTextFromValue (slider.getValue()), valueArea,
                          juce::Justification::centredRight, 1);

        const float trackX = bounds.getX() + 2.0f * scale;
        const float trackW = juce::jmax (4.0f, bounds.getWidth() - 4.0f * scale);
        const float trackY = bounds.getBottom() - 3.0f * scale;
        g.setColour (snap::tileTrack);
        g.drawLine (trackX, trackY, trackX + trackW, trackY, 1.2f * scale);
        const float thumbX = trackX + juce::jlimit (0.0f, 1.0f, proportion) * trackW;
        g.setColour (slider.isEnabled() ? accent : ui::kDisabledText);
        g.fillEllipse (thumbX - 2.0f * scale, trackY - 2.0f * scale,
                       4.0f * scale, 4.0f * scale);
        return;
    }

    // Compact SNAPBACK parameter tile: dark rounded tile, grey label, white
    // value, thin accent track near the bottom (bipolar fills from centre).
    if (static_cast<bool> (slider.getProperties().getWithDefault ("isTile", false)))
    {
        const float proportion = (float) slider.valueToProportionOfLength (slider.getValue());
        drawTileSlider (g, juce::Rectangle<float> ((float) x, (float) y,
                                                   (float) width, (float) height),
                        slider, juce::jlimit (0.0f, 1.0f, proportion));
        return;
    }

    if (style == juce::Slider::LinearVertical)
    {
        // SVG fader: a fine vertical guide, accent active range and a circular
        // thumb. The title and dB value are drawn by LayerEnvelopeComponent.
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y,
                                                     (float) width, (float) height);
        const float scale = juce::jmax (0.55f, bounds.getHeight() / 36.0f);
        const float cx = bounds.getCentreX();
        const float top = bounds.getY();
        const float bottom = bounds.getBottom();
        const float pos = juce::jlimit (top + 0.5f * scale,
                                        bottom - 0.5f * scale, sliderPos);

        g.setColour (ui::kSecondaryText.withAlpha (0.58f));
        for (float dashY = top; dashY < bottom; dashY += 7.0f * scale)
            g.drawLine (cx, dashY, cx, juce::jmin (bottom, dashY + 4.0f * scale),
                        0.8f * scale);

        if (pos < bottom)
        {
            g.setColour (accent);
            g.drawLine (cx, pos, cx, bottom, 1.3f * scale);
        }

        const float radius = 3.0f * scale;
        g.setColour (ui::kMainText);
        g.fillEllipse (cx - radius, pos - radius, 2.0f * radius, 2.0f * radius);
        g.setColour (ui::kBorder.brighter (0.25f));
        g.drawEllipse (cx - radius, pos - radius, 2.0f * radius, 2.0f * radius,
                       1.2f * scale);
        return;
    }

    if (static_cast<bool> (slider.getProperties().getWithDefault ("inlineBar", false)))
    {
        auto bar = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (0.5f);
        g.setColour (hovered ? ui::kHoverBg.darker (0.25f) : snap::inset);
        g.fillRoundedRectangle (bar, 3.0f);

        const float innerL = bar.getX() + 1.0f;
        const float innerR = bar.getRight() - 1.0f;
        const float innerW = juce::jmax (4.0f, innerR - innerL);
        const float prop = (float) slider.valueToProportionOfLength (slider.getValue());
        const float valX = innerL + prop * innerW;

        if (isBipolar)
        {
            const float midX = innerL + innerW * 0.5f;
            g.setColour (ui::kBorder.withAlpha (0.7f));
            g.drawVerticalLine (juce::roundToInt (midX), bar.getY() + 3.0f, bar.getBottom() - 3.0f);

            const float fX = juce::jmin (midX, valX);
            const float fW = std::abs (valX - midX);
            if (fW > 0.5f)
            {
                g.setColour (accent.withAlpha (hovered ? 0.32f : 0.22f));
                g.fillRect (fX, bar.getY() + 1.0f, fW, bar.getHeight() - 2.0f);
            }
        }
        else
        {
            const float fW = valX - innerL;
            if (fW > 0.5f)
            {
                g.setColour (accent.withAlpha (hovered ? 0.30f : 0.20f));
                g.fillRoundedRectangle (innerL, bar.getY() + 1.0f, fW, bar.getHeight() - 2.0f, 2.0f);
            }
        }

        // Crisp Snapback value indicator edge
        g.setColour (slider.isEnabled() ? accent : ui::kDisabledText);
        g.fillRect (juce::jlimit (innerL, innerR - 2.0f, valX - 1.0f),
                    bar.getY() + 1.0f, 2.0f, bar.getHeight() - 2.0f);

        g.setColour (hovered ? accent.withAlpha (0.75f) : ui::kBorder);
        g.drawRoundedRectangle (bar, 3.0f, 1.0f);

        // Label (left) + Value readout (right) inside the bar
        auto textBox = bar.reduced (6.0f, 0.0f);
        g.setFont (juce::Font (9.0f, juce::Font::bold));
        g.setColour (ui::kSecondaryText);
        g.drawFittedText (slider.getName(), textBox.toNearestInt(), juce::Justification::centredLeft, 1);

        g.setFont (juce::Font (10.0f, juce::Font::bold));
        g.setColour (slider.isEnabled() ? ui::kMainText : ui::kDisabledText);
        g.drawFittedText (slider.getTextFromValue (slider.getValue()),
                          textBox.toNearestInt(), juce::Justification::centredRight, 1);
        return;
    }

    // Standard slim flat Snapback horizontal fill bar (with external TextBoxRight)
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float barH = juce::jmin (14.0f, b.getHeight() - 4.0f);
    juce::Rectangle<float> track (b.getX() + 2.0f, b.getCentreY() - barH * 0.5f,
                                  juce::jmax (10.0f, b.getWidth() - 4.0f), barH);
    g.setColour (snap::inset);
    g.fillRoundedRectangle (track, 3.0f);

    const float clampedX = juce::jlimit (track.getX() + 1.0f, track.getRight() - 1.0f, sliderPos);
    const float fillW = clampedX - track.getX();
    if (fillW > 0.5f)
    {
        g.setColour (accent.withAlpha (hovered ? 0.35f : 0.25f));
        g.fillRoundedRectangle (track.getX() + 1.0f, track.getY() + 1.0f,
                                juce::jmax (1.0f, fillW - 1.0f), track.getHeight() - 2.0f, 2.0f);
        g.setColour (accent);
        g.fillRect (juce::jlimit (track.getX() + 1.0f, track.getRight() - 3.0f, clampedX - 1.0f),
                    track.getY() + 1.0f, 2.0f, track.getHeight() - 2.0f);
    }

    g.setColour (hovered ? accent.withAlpha (0.7f) : ui::kBorder);
    g.drawRoundedRectangle (track, 3.0f, 1.0f);
}

void SpectralMultipleLookAndFeel::drawTileSlider (juce::Graphics& g, juce::Rectangle<float> tile,
                                                  juce::Slider& slider, float proportion)
{
    const bool enabled = slider.isEnabled();
    const bool hovered = slider.isMouseOverOrDragging();
    const auto accent = slider.findColour (juce::Slider::trackColourId).isTransparent()
        ? ui::kPurpleAccent
        : slider.findColour (juce::Slider::trackColourId);
    const bool isBipolar = static_cast<bool> (slider.getProperties().getWithDefault ("bipolar", false));
    const float scale = juce::jmax (0.55f, tile.getHeight() / 36.0f);

    g.setColour (hovered ? ui::kHoverBg : ui::kControlBg);
    g.fillRoundedRectangle (tile, 2.0f * scale);
    g.setColour (enabled ? ui::kBorder : ui::kBorder.withAlpha (0.45f));
    g.drawRoundedRectangle (tile.reduced (0.5f * scale), 2.0f * scale, 1.0f * scale);

    const auto label = slider.getProperties().getWithDefault ("tileLabel", slider.getName()).toString();
    const auto valueText = slider.getTextFromValue (slider.getValue());
    const float textW = juce::jmax (10.0f * scale, tile.getWidth() - 14.0f * scale);

    // Preserve the SVG's title/value rhythm; reserve a separate bottom row for the track.
    auto shrinkToFit = [scale] (const juce::String& text, float baseHeight, float maxWidth) -> juce::Font
    {
        auto font = juce::Font (baseHeight * scale, juce::Font::bold);
        const float minimumHeight = 6.0f * scale;
        while (font.getStringWidthFloat (text) > maxWidth - 1.0f * scale
               && font.getHeight() > minimumHeight)
            font = font.withHeight (font.getHeight() - 0.5f * scale);
        return font;
    };

    const float textLeft = tile.getX() + 7.0f * scale;
    const float labelTop = tile.getY() + 3.0f * scale;
    auto labelArea = juce::Rectangle<float> (textLeft, labelTop,
                                             textW, 11.0f * scale);
    g.setColour (enabled ? ui::kTileLabel : ui::kDisabledText);
    g.setFont (shrinkToFit (label, 9.0f, textW));
    if (label.isNotEmpty())
        g.drawText (label, labelArea.toNearestInt(), juce::Justification::centredLeft, false);

    auto valueArea = juce::Rectangle<float> (textLeft, tile.getY() + 16.0f * scale,
                                             textW, 13.0f * scale);
    g.setColour (enabled ? ui::kMainText : ui::kDisabledText);
    g.setFont (shrinkToFit (valueText, 10.5f, textW));
    g.drawText (valueText, valueArea.toNearestInt(), juce::Justification::centredLeft, false);

    // Track is isolated below the value so it cannot strike through the readout.
    const float trackY = tile.getBottom() - 4.5f * scale;
    auto track = juce::Rectangle<float> (tile.getX() + 7.0f * scale, trackY,
                                         juce::jmax (4.0f * scale, tile.getWidth() - 14.0f * scale),
                                         1.25f * scale);
    g.setColour (snap::tileTrack);
    g.fillRoundedRectangle (track, 0.7f * scale);

    if (isBipolar)
    {
        g.setColour (ui::kBorder.brighter (0.30f));
        g.drawLine (track.getCentreX(), trackY - 2.0f * scale,
                    track.getCentreX(), trackY + 3.0f * scale, 0.85f * scale);
    }

    const float px = track.getX() + juce::jlimit (0.0f, 1.0f, proportion) * track.getWidth();
    juce::Rectangle<float> fill;
    if (isBipolar)
        fill = juce::Rectangle<float> (juce::jmin (px, track.getCentreX()), trackY,
                                       std::abs (px - track.getCentreX()), track.getHeight());
    else
        fill = track.withWidth (juce::jmax (0.0f, px - track.getX()));
    if (fill.getWidth() > 0.5f * scale)
    {
        g.setColour (accent.withAlpha (enabled ? 0.95f : 0.35f));
        g.fillRoundedRectangle (fill, 0.7f * scale);
    }

    const float thumbW = 2.6f * scale;
    const float thumbH = 6.0f * scale;
    g.setColour (enabled ? ui::kMainText : ui::kDisabledText);
    g.fillRoundedRectangle (px - thumbW * 0.5f, trackY - (thumbH - track.getHeight()) * 0.5f,
                            thumbW, thumbH, 1.0f * scale);
}

void SpectralMultipleLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                        const juce::Colour& /*backgroundColour*/,
                                                        bool shouldDrawButtonAsHighlighted,
                                                        bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool isToggleOn = button.getToggleState();

    const juce::var accentVar = button.getProperties().getWithDefault ("accentColour", juce::var());
    const juce::Colour customAccent = accentVar.isInt() || accentVar.isInt64()
        ? juce::Colour ((juce::uint32) (juce::int64) accentVar)
        : ui::kPurpleAccent;

    // SVG COLLAPSE tab: narrow top edge, wider bottom edge.
    if (static_cast<bool> (button.getProperties().getWithDefault ("isTrapezoid", false)))
    {
        const float inset = bounds.getWidth() * 0.20f;
        juce::Path trap;
        trap.startNewSubPath (bounds.getX() + inset, bounds.getY());
        trap.lineTo (bounds.getRight() - inset, bounds.getY());
        trap.lineTo (bounds.getRight(), bounds.getBottom());
        trap.lineTo (bounds.getX(), bounds.getBottom());
        trap.closeSubPath();

        juce::Colour fill = isToggleOn ? customAccent.brighter (0.12f) : customAccent;
        if (! button.isEnabled())
            fill = ui::kControlBg;
        else if (shouldDrawButtonAsDown)
            fill = fill.darker (0.18f);
        else if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter (0.10f);

        g.setColour (fill);
        g.fillPath (trap);
        if (! button.isEnabled())
        {
            g.setColour (ui::kBorder.withAlpha (0.45f));
            g.strokePath (trap, juce::PathStrokeType (1.0f));
        }
        return;
    }

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
    return juce::Font (juce::jlimit (8.0f, 28.0f, (float) box.getHeight() * 0.42f));
}

juce::Font SpectralMultipleLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::jlimit (8.0f, 28.0f, (float) buttonHeight * 0.42f), juce::Font::bold);
}

juce::Font SpectralMultipleLookAndFeel::getPopupMenuFont()
{
    return juce::Font (11.5f);
}
