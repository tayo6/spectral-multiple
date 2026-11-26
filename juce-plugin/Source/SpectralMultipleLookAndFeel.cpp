#include "SpectralMultipleLookAndFeel.h"

namespace ui = wavelens::ui;

SpectralMultipleLookAndFeel::SpectralMultipleLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, ui::kMainBg);

    setColour (juce::ComboBox::backgroundColourId, ui::kControlBg);
    setColour (juce::ComboBox::textColourId, ui::kMainText);
    setColour (juce::ComboBox::outlineColourId, ui::kBorder);
    setColour (juce::ComboBox::arrowColourId, ui::kSecondaryText);

    setColour (juce::PopupMenu::backgroundColourId, snap::inset);
    setColour (juce::PopupMenu::textColourId, ui::kMainText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, snap::tile);
    setColour (juce::PopupMenu::highlightedTextColourId, ui::kMainText);
    setColour (juce::PopupMenu::headerTextColourId, ui::kSecondaryText);

    setColour (juce::TextButton::buttonColourId, ui::kControlBg);
    setColour (juce::TextButton::buttonOnColourId, ui::kControlBg);
    setColour (juce::TextButton::textColourOffId, ui::kSecondaryText);
    setColour (juce::TextButton::textColourOnId, ui::kMainText);

    setColour (juce::Label::textColourId, ui::kMainText);
    setColour (juce::Slider::textBoxTextColourId, ui::kMainText);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, ui::kSelectedAccent.withAlpha (0.35f));

    setColour (juce::TextEditor::backgroundColourId, snap::inset);
    setColour (juce::TextEditor::textColourId, ui::kMainText);
    setColour (juce::TextEditor::outlineColourId, ui::kBorder);
    setColour (juce::TextEditor::focusedOutlineColourId, ui::kSelectedAccent);
}

void SpectralMultipleLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                                    float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                                    juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto accent = slider.findColour (juce::Slider::trackColourId).isTransparent()
        ? ui::kPurpleAccent
        : slider.findColour (juce::Slider::trackColourId);
    const bool hovered = slider.isMouseOverOrDragging();

    // Compact value card (SVG "ui-label" + "ui-val" on a #17151d tile).
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
        // SVG "Volume" fader: #0d0b10 2.2px round-cap track, accent active
        // span from the thumb down, #e6e4ed r=3 thumb.
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const float trackX = b.getCentreX();
        const float top = b.getY() + 2.0f;
        const float bot = b.getBottom() - 2.0f;

        g.setColour (snap::tileTrack);
        g.drawLine (trackX, top, trackX, bot, 2.2f);

        const float clampedPos = juce::jlimit (top + 1.5f, bot - 1.5f, sliderPos);
        if (bot - clampedPos > 0.5f)
        {
            g.setColour (accent);
            g.drawLine (trackX, clampedPos, trackX, bot, 2.2f);
        }

        g.setColour (snap::thumb);
        g.fillEllipse (trackX - 3.0f, clampedPos - 3.0f, 6.0f, 6.0f);
        if (hovered)
        {
            g.setColour (accent.withAlpha (0.35f));
            g.drawEllipse (trackX - 5.0f, clampedPos - 5.0f, 10.0f, 10.0f, 1.0f);
        }
        return;
    }

    if (static_cast<bool> (slider.getProperties().getWithDefault ("inlineBar", false)))
    {
        auto bar = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (0.5f);
        g.setColour (hovered ? ui::kHoverBg : ui::kControlBg);
        g.fillRoundedRectangle (bar, 2.0f);

        const float innerL = bar.getX() + 1.0f;
        const float innerR = bar.getRight() - 1.0f;
        const float innerW = juce::jmax (4.0f, innerR - innerL);
        const float prop = (float) slider.valueToProportionOfLength (slider.getValue());
        const float valX = innerL + prop * innerW;
        const bool isBipolar = static_cast<bool> (slider.getProperties().getWithDefault ("bipolar", false));

        g.setColour (snap::tileTrack);
        g.fillRect (bar.getX() + 1.0f, bar.getY() + bar.getHeight() - 3.0f, bar.getWidth() - 2.0f, 2.2f);
        g.setColour (accent);
        if (isBipolar)
        {
            const float midX = innerL + innerW * 0.5f;
            g.fillRect (juce::jmin (midX, valX), bar.getY() + bar.getHeight() - 3.0f,
                        std::abs (valX - midX), 2.2f);
        }
        else
        {
            g.fillRect (innerL, bar.getY() + bar.getHeight() - 3.0f, valX - innerL, 2.2f);
        }

        g.setColour (ui::kBorder);
        g.drawRoundedRectangle (bar, 2.0f, 1.0f);

        auto textBox = bar.reduced (6.0f, 0.0f);
        g.setFont (juce::Font (9.5f));
        g.setColour (ui::kSecondaryText);
        g.drawFittedText (slider.getName(), textBox.toNearestInt(), juce::Justification::centredLeft, 1);

        g.setFont (juce::Font (10.5f));
        g.setColour (slider.isEnabled() ? ui::kMainText : ui::kDisabledText);
        g.drawFittedText (slider.getTextFromValue (slider.getValue()),
                          textBox.toNearestInt(), juce::Justification::centredRight, 1);
        return;
    }

    // Standard slim horizontal slider with external TextBoxRight.
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float trackY = b.getCentreY();
    g.setColour (snap::tileTrack);
    g.drawLine (b.getX() + 2.0f, trackY, b.getRight() - 2.0f, trackY, 2.2f);

    const bool isBipolar = static_cast<bool> (slider.getProperties().getWithDefault ("bipolar", false));
    const float clampedX = juce::jlimit (b.getX() + 2.0f, b.getRight() - 2.0f, sliderPos);
    g.setColour (accent);
    if (isBipolar)
    {
        const float midX = b.getCentreX();
        g.drawLine (juce::jmin (midX, clampedX), trackY, juce::jmax (midX, clampedX), trackY, 2.2f);
    }
    else
    {
        g.drawLine (b.getX() + 2.0f, trackY, clampedX, trackY, 2.2f);
    }

    g.setColour (snap::thumb);
    g.fillEllipse (clampedX - 3.0f, trackY - 3.0f, 6.0f, 6.0f);
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
    const bool hasTrack = static_cast<bool> (slider.getProperties().getWithDefault ("hasTrack", true));

    // SVG card: #17151d fill, subtle highlight when hovered.
    g.setColour (hovered ? ui::kHoverBg : ui::kControlBg);
    g.fillRoundedRectangle (tile, 2.0f);

    auto inner = tile.reduced (8.0f, 4.0f);
    const auto label = slider.getProperties().getWithDefault ("tileLabel", slider.getName()).toString();
    const auto valueText = slider.getTextFromValue (slider.getValue());

    auto shrinkToFit = [] (const juce::String& t, float baseHeight, float maxWidth) -> juce::Font
    {
        auto f = juce::Font (baseHeight);
        while (f.getStringWidthFloat (t) > maxWidth - 1.0f && f.getHeight() > 6.0f)
            f = f.withHeight (f.getHeight() - 0.5f);
        return f;
    };

    // Grey label (ui-label) top-left, white value (ui-val) below.
    auto labelArea = inner.removeFromTop (13).toNearestInt();
    g.setColour (enabled ? ui::kTileLabel : ui::kDisabledText);
    g.setFont (shrinkToFit (label, 10.0f, inner.getWidth()));
    if (label.isNotEmpty())
        g.drawText (label, labelArea, juce::Justification::centredLeft, false);

    auto valueArea = inner.removeFromTop (15).toNearestInt();
    g.setColour (enabled ? ui::kMainText : ui::kDisabledText);
    g.setFont (shrinkToFit (valueText, 11.5f, inner.getWidth()));
    g.drawText (valueText, valueArea, juce::Justification::centredLeft, false);

    // Cards in the colour SVG show values only (no track); the 2.2 px
    // round-cap track appears only when explicitly requested.
    if (! hasTrack)
        return;

    const float trackY = tile.getBottom() - 8.0f;
    const float trackX0 = tile.getX() + 12.0f;
    const float trackX1 = tile.getRight() - 12.0f;
    g.setColour (snap::tileTrack);
    g.drawLine (trackX0, trackY, trackX1, trackY, 2.2f);

    const float px = trackX0 + proportion * (trackX1 - trackX0);
    g.setColour (accent.withAlpha (enabled ? 0.95f : 0.35f));
    if (isBipolar)
    {
        const float midX = (trackX0 + trackX1) * 0.5f;
        g.drawLine (juce::jmin (midX, px), trackY, juce::jmax (midX, px), trackY, 2.2f);
    }
    else
    {
        g.drawLine (trackX0, trackY, px, trackY, 2.2f);
    }

    g.setColour (snap::thumb);
    g.fillEllipse (px - 3.0f, trackY - 3.0f, 6.0f, 6.0f);
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

    // Trapezoid tab (COLLAPSE): #25232d fill + #363342 stroke, label above
    // and a chevron below (SVG #7d72b3; flips up when latched on).
    if (static_cast<bool> (button.getProperties().getWithDefault ("isTrapezoid", false)))
    {
        const auto trap = snap::trapezoidPath (bounds, bounds.getWidth() * 0.27f);
        g.setColour (juce::Colour (0xff25232d));
        g.fillPath (trap);
        g.setColour (juce::Colour (0xff363342));
        g.strokePath (trap, juce::PathStrokeType (1.0f));

        const float cx = bounds.getCentreX();
        const float cy = bounds.getBottom() - 11.0f;
        g.setColour (isToggleOn ? juce::Colour (0xffa8a3b8) : juce::Colour (0xff7d72b3));
        juce::Path chev;
        if (isToggleOn)
        {
            chev.startNewSubPath (cx - 5.0f, cy + 4.5f);
            chev.lineTo (cx, cy);
            chev.lineTo (cx + 5.0f, cy + 4.5f);
        }
        else
        {
            chev.startNewSubPath (cx - 5.0f, cy);
            chev.lineTo (cx, cy + 4.5f);
            chev.lineTo (cx + 5.0f, cy);
        }
        g.strokePath (chev, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        return;
    }

    // Headphone solo icon (panel headers).
    if (static_cast<bool> (button.getProperties().getWithDefault ("isHeadphone", false)))
    {
        const float cx = bounds.getCentreX();
        const float top = bounds.getY() + 2.0f;
        const float bandR = juce::jmin (6.0f, bounds.getWidth() * 0.28f);
        const float bandY = top + bandR;

        const juce::Colour stroke = isToggleOn ? customAccent : snap::icon;
        g.setColour (stroke);
        juce::Path band;
        band.startNewSubPath (cx - bandR, bandY + 3.0f);
        band.addCentredArc (cx, bandY + 3.0f, bandR, bandR, 0.0f,
                            juce::MathConstants<float>::pi,
                            juce::MathConstants<float>::twoPi, false);
        g.strokePath (band, juce::PathStrokeType (1.35f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        g.fillRoundedRectangle (cx - bandR - 1.1f, bandY + 1.0f, 2.2f, 4.5f, 1.0f);
        g.fillRoundedRectangle (cx + bandR - 1.1f, bandY + 1.0f, 2.2f, 4.5f, 1.0f);
        return;
    }

    // Drum-pad trigger cap: SVG cap is 16x16 units at (5,5) of the
    // 116x26 pad; fill #232a4d (blue) / #2e2147 (purple) + accent stroke.
    if (static_cast<bool> (button.getProperties().getWithDefault ("isPadTrigger", false)))
    {
        const float ux = bounds.getWidth() / 116.0f;
        const float uy = bounds.getHeight() / 26.0f;
        auto capRect = juce::Rectangle<float> (bounds.getX() + 5.0f * ux,
                                               bounds.getY() + 5.0f * uy,
                                               16.0f * ux, 16.0f * uy);
        const bool active = isToggleOn || shouldDrawButtonAsDown;
        // The pad accent may be the trace colour (#8597ff/#be8aff) or the
        // bank accent - treat the violet family as the TRANSIENT bank.
        const bool purpleBank = customAccent == snap::tr || customAccent == snap::trHi
                             || customAccent == snap::trTrace;
        const auto capFill = purpleBank ? juce::Colour (0xff2e2147) : juce::Colour (0xff232a4d);
        g.setColour (active ? capFill.brighter (0.14f) : capFill);
        g.fillRoundedRectangle (capRect, 2.0f);
        g.setColour (active ? customAccent.brighter (0.1f) : customAccent);
        g.drawRoundedRectangle (capRect, 2.0f, 1.1f);
        return;
    }

    // Preset stepper arrows: plain grey chevrons, no box.
    if (static_cast<bool> (button.getProperties().getWithDefault ("isArrow", false)))
    {
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        g.setColour (shouldDrawButtonAsHighlighted ? ui::kMainText : snap::icon);
        juce::Path chev;
        const float d = 5.0f;
        if (button.getName().contains ("<") || button.getButtonText().contains ("<"))
        {
            chev.startNewSubPath (cx + d * 0.4f, cy - d);
            chev.lineTo (cx - d * 0.6f, cy);
            chev.lineTo (cx + d * 0.4f, cy + d);
        }
        else
        {
            chev.startNewSubPath (cx - d * 0.4f, cy - d);
            chev.lineTo (cx + d * 0.6f, cy);
            chev.lineTo (cx - d * 0.4f, cy + d);
        }
        g.strokePath (chev, juce::PathStrokeType (1.35f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        return;
    }

    // Bare power icon button (Limiter in the toolbar).
    if (static_cast<bool> (button.getProperties().getWithDefault ("isPowerIcon", false)))
    {
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY() + 1.0f;
        const float r = juce::jmin (5.0f, bounds.getHeight() * 0.30f);
        g.setColour (isToggleOn ? customAccent : snap::icon);
        juce::Path arc;
        arc.addCentredArc (cx, cy, r, r, 0.0f, 0.48f,
                           juce::MathConstants<float>::twoPi - 0.48f, true);
        g.strokePath (arc, juce::PathStrokeType (1.25f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        g.drawLine (cx, cy - r - 2.0f, cx, cy - 0.5f, 1.25f);
        return;
    }

    // Phase-invert icon (Shift card): circle with a diagonal slash.
    if (static_cast<bool> (button.getProperties().getWithDefault ("isPhaseIcon", false)))
    {
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float r = juce::jmin (4.5f, bounds.getWidth() * 0.30f);
        g.setColour (isToggleOn ? customAccent : snap::icon);
        g.drawEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r, 1.35f);
        g.drawLine (cx - r * 0.72f, cy + r * 0.72f, cx + r * 0.72f, cy - r * 0.72f, 1.35f);
        return;
    }

    // Transparent text-only button (sample name between the chevrons).
    if (static_cast<bool> (button.getProperties().getWithDefault ("isTransparentText", false)))
    {
        if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
        {
            g.setColour (customAccent.withAlpha (0.12f));
            g.fillRoundedRectangle (bounds, 2.0f);
        }
        return;
    }

    if (static_cast<bool> (button.getProperties().getWithDefault ("isFlatSegment", false)))
    {
        g.setColour (isToggleOn ? customAccent.withAlpha (0.16f)
                                : (shouldDrawButtonAsHighlighted ? ui::kHoverBg : ui::kControlBg));
        g.fillRoundedRectangle (bounds, 2.0f);
        g.setColour (isToggleOn ? customAccent : ui::kBorder);
        g.drawRoundedRectangle (bounds, 2.0f, 1.0f);
        return;
    }

    // Standard dark button: #131118 fill + #2b2836 stroke (pad M/S squares),
    // accent-tinted when latched on.
    juce::Colour fill = isToggleOn ? customAccent.withAlpha (0.20f) : snap::inset;
    if (! button.isEnabled())
        fill = snap::inset;
    else if (shouldDrawButtonAsDown)
        fill = customAccent.withAlpha (0.30f);
    else if (shouldDrawButtonAsHighlighted)
        fill = isToggleOn ? customAccent.withAlpha (0.26f) : ui::kHoverBg;

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 2.0f);

    g.setColour (! button.isEnabled() ? ui::kBorder.withAlpha (0.45f)
                                      : (isToggleOn ? customAccent : juce::Colour (0xff2b2836)));
    g.drawRoundedRectangle (bounds, 2.0f, 0.9f);
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

    // Pad label "PAD n": starts at SVG x=27 of the 116-unit pad.
    if (static_cast<bool> (button.getProperties().getWithDefault ("isPadTrigger", false)))
    {
        const float ux = (float) button.getWidth() / 116.0f;
        g.drawFittedText (button.getButtonText(),
                          button.getLocalBounds().withLeft (juce::roundToInt (27.0f * ux))
                                .withTrimmedRight (juce::roundToInt (72.0f * ux)),
                          juce::Justification::centredLeft, 1);
        return;
    }

    // Sample name (SVG x=28 of the 226 box): left-aligned after the prev
    // chevron, vertically centred.
    if (static_cast<bool> (button.getProperties().getWithDefault ("isTransparentText", false)))
    {
        g.drawFittedText (button.getButtonText(),
                          button.getLocalBounds().reduced (4, 1),
                          juce::Justification::centredLeft, 1);
        return;
    }

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
    g.fillRoundedRectangle (bounds, 2.0f);
    g.setColour (box.isEnabled() ? ui::kBorder : ui::kBorder.withAlpha (0.45f));
    g.drawRoundedRectangle (bounds, 2.0f, 1.0f);

    const float arrowX = (float) width - 14.0f;
    const float arrowY = (float) height * 0.5f - 1.5f;
    juce::Path chevron;
    chevron.startNewSubPath (arrowX, arrowY);
    chevron.lineTo (arrowX + 3.5f, arrowY + 3.5f);
    chevron.lineTo (arrowX + 7.0f, arrowY);

    g.setColour (box.isEnabled() ? ui::kSecondaryText : ui::kDisabledText);
    g.strokePath (chevron, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void SpectralMultipleLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (snap::inset);
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
        g.setColour (juce::Colour (0xff2b2836));
        g.drawHorizontalLine (r.getCentreY(), (float) r.getX(), (float) r.getRight());
        return;
    }

    auto r = area.reduced (3, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (snap::tile);
        g.fillRoundedRectangle (r.toFloat(), 2.0f);
        g.setColour (ui::kSelectedAccent.withAlpha (0.30f));
        g.drawRoundedRectangle (r.toFloat(), 2.0f, 1.0f);
    }

    auto textArea = r.reduced (8, 0);
    if (isTicked)
    {
        // FL-style radio dot: filled circle, gold when highlighted
        g.setColour (isHighlighted ? snap::flPoint : ui::kSelectedAccent);
        g.fillEllipse ((float) textArea.getX() + 2.0f, (float) textArea.getCentreY() - 2.5f, 5.0f, 5.0f);
    }
    textArea.removeFromLeft (14);

    g.setFont (getPopupMenuFont());
    const auto col = textColourToUse != nullptr
        ? *textColourToUse
        : (! isActive ? ui::kDisabledText
                      : (isTicked || isHighlighted ? ui::kMainText : juce::Colour (0xffc9c4d6)));
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

juce::Slider::SliderLayout SpectralMultipleLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    auto layout = LookAndFeel_V4::getSliderLayout (slider);

    // The custom drawing paints the #e6e4ed thumb centred on the track, so
    // vertical faders (SVG "Volume" card) must keep the full component
    // bounds - the default layout shrinks them by the thumb radius. Tile
    // cards and inline bars draw their own full-bleed card, so keep their
    // bounds too.
    if (slider.getSliderStyle() == juce::Slider::LinearVertical
        && slider.getTextBoxPosition() == juce::Slider::NoTextBox)
        layout.sliderBounds = slider.getLocalBounds();
    else if (static_cast<bool> (slider.getProperties().getWithDefault ("isTile", false))
             || static_cast<bool> (slider.getProperties().getWithDefault ("inlineBar", false)))
        layout.sliderBounds = slider.getLocalBounds();

    return layout;
}

juce::Font SpectralMultipleLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::jlimit (8.5f, 11.0f, (float) buttonHeight * 0.45f), juce::Font::bold);
}

juce::Font SpectralMultipleLookAndFeel::getPopupMenuFont()
{
    return juce::Font (11.5f);
}
