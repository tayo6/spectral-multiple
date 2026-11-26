#include "PluginHeaderComponent.h"

namespace ui = wavelens::ui;

void PluginHeaderComponent::IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat();
    const bool active = getToggleState();
    // ui-icon grey per the SVG; hover brightens, power uses amber when bypassed
    const auto stroke = down ? ui::kSelectedAccent
                     : (highlighted ? juce::Colour (0xffc9c4d6) : snap::icon);
    g.setColour (stroke);

    if (kind == Kind::Power)
    {
        // SVG: "M 26.2,17.2 A 6.2,6.2 0 1,0 33.8,17.2" + vertical stem
        const float r = b.getHeight() * 0.30f;
        const float cx = b.getCentreX();
        const float cy = b.getCentreY() + 1.0f;
        juce::Path arc;
        arc.addCentredArc (cx, cy, r, r, 0.0f, 0.48f, juce::MathConstants<float>::twoPi - 0.48f, true);
        g.strokePath (arc, juce::PathStrokeType (1.35f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        g.drawLine (cx, cy - r - 2.2f, cx, cy - 0.5f, 1.35f);
    }
    else if (kind == Kind::Menu)
    {
        // SVG hamburger: three short horizontal strokes
        for (int i = -1; i <= 1; ++i)
        {
            const float y = b.getCentreY() + (float) i * 5.0f;
            g.drawLine (b.getX() + 5.0f, y, b.getRight() - 5.0f, y, 1.35f);
        }
    }
    else // Heart (favourite) - SVG small heart path
    {
        juce::Path heart;
        const float cx = b.getCentreX();
        const float cy = b.getCentreY();
        heart.startNewSubPath (cx, cy + 5.2f);
        heart.cubicTo (cx - 7.5f, cy + 0.8f, cx - 6.5f, cy - 5.5f, cx, cy - 2.2f);
        heart.cubicTo (cx + 6.5f, cy - 5.5f, cx + 7.5f, cy + 0.8f, cx, cy + 5.2f);
        heart.closeSubPath();

        if (active)
        {
            g.setColour (snap::flPoint);
            g.fillPath (heart);
        }
        else
            g.strokePath (heart, juce::PathStrokeType (1.35f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
    }
}

//==============================================================================
PluginHeaderComponent::PluginHeaderComponent (WaveLensProcessor& processor)
    : proc (processor)
{
    powerBtn.setToggleState (proc.isPluginEnabled(), juce::dontSendNotification);
    powerBtn.onClick = [this]
    {
        proc.setPluginEnabled (powerBtn.getToggleState());
        repaint();
    };
    addAndMakeVisible (powerBtn);

    menuBtn.onClick = [this] { openMainMenu(); };
    addAndMakeVisible (menuBtn);

    favBtn.onClick = [this]
    {
        const int idx = juce::jlimit (0, (int) favouritePresets.size() - 1, presetBox.getSelectedItemIndex());
        favouritePresets[(size_t) idx] = favBtn.getToggleState();
    };
    addAndMakeVisible (favBtn);

    prevPresetBtn.setTooltip ("Previous preset");
    prevPresetBtn.onClick = [this] { stepPreset (-1); };
    prevPresetBtn.getProperties().set ("isArrow", true);
    prevPresetBtn.setColour (juce::TextButton::textColourOffId, ui::kInk);
    addAndMakeVisible (prevPresetBtn);

    nextPresetBtn.setTooltip ("Next preset");
    nextPresetBtn.onClick = [this] { stepPreset (1); };
    nextPresetBtn.getProperties().set ("isArrow", true);
    nextPresetBtn.setColour (juce::TextButton::textColourOffId, ui::kInk);
    addAndMakeVisible (nextPresetBtn);

    resetBtn.setTooltip ("Reset scope view and zoom (samples and parameters are untouched)");
    resetBtn.onClick = [this]
    {
        if (onResetScope) onResetScope();
        if (onResetZoom) onResetZoom();
    };
    addAndMakeVisible (resetBtn);

    bpmDownBtn.setTooltip ("Decrease BPM by 1");
    bpmDownBtn.onClick = [this] { setBpm (currentBpm - 1.0); };
    addAndMakeVisible (bpmDownBtn);

    bpmUpBtn.setTooltip ("Increase BPM by 1");
    bpmUpBtn.onClick = [this] { setBpm (currentBpm + 1.0); };
    addAndMakeVisible (bpmUpBtn);

    presetBox.addItemList ({
        "Init / Clean Layers",
        "Tight 35ms Snapback",
        "Layered Drum Smack",
        "Wide Reverse Swell",
        "Sub Transient Punch",
        "Cyber Metallic Stack"
    }, 1);
    presetBox.setSelectedId (1, juce::dontSendNotification);
    presetBox.setTooltip ("Select factory parameter preset (preserves loaded samples)");
    presetBox.setJustificationType (juce::Justification::centredLeft);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0)
        {
            favBtn.setToggleState (favouritePresets[(size_t) juce::jlimit (0, 5, idx)], juce::dontSendNotification);
            applyPreset (idx);
        }
    };
    addAndMakeVisible (presetBox);
    favBtn.setToggleState (favouritePresets[0], juce::dontSendNotification);

    // The SVG header keeps RESET/BPM controls hidden inside the menu; keep
    // them as functional but invisible overlays so behaviour is unchanged.
    resetBtn.setVisible (false);
    bpmDownBtn.setVisible (false);
    bpmUpBtn.setVisible (false);
}

void PluginHeaderComponent::setBpm (double nextBpm)
{
    currentBpm = juce::jlimit (40.0, 240.0, nextBpm);
    if (onBpmChanged)
        onBpmChanged (currentBpm);
    repaint();
}

void PluginHeaderComponent::stepPreset (int delta)
{
    const int count = presetBox.getNumItems();
    if (count <= 0)
        return;
    const int nextIdx = (presetBox.getSelectedItemIndex() + delta + count) % count;
    presetBox.setSelectedItemIndex (nextIdx, juce::sendNotification);
}

void PluginHeaderComponent::applyPreset (int presetIndex)
{
    auto setParam = [this] (const juce::String& id, float plainVal)
    {
        if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id)))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (plainVal));
            param->endChangeGesture();
        }
    };

    switch (presetIndex)
    {
        case 0: // Init / Clean Layers
            setParam (ids::preRollMs, 50.0f);
            for (int l = 0; l < 6; ++l)
            {
                setParam (ids::layerGain (l), 1.0f);
                setParam (ids::layerPan (l), 0.0f);
                setParam (ids::layerWidth (l), 1.0f);
                setParam (ids::layerPitch (l), 0.0f);
                setParam (ids::layerDelay (l), 0.0f);
                setParam (ids::layerHighPass (l), 20.0f);
                setParam (ids::layerLowPass (l), 20000.0f);
                setParam (ids::layerReverse (l), 0.0f);
                setParam (ids::layerPolarity (l), 0.0f);
                setParam (ids::layerPogo (l), 0.0f);
                setParam (ids::layerEnvEnabled (l), 0.0f);
                setParam (ids::layerTrimStart (l), 0.0f);
                setParam (ids::layerTrimEnd (l), 1.0f);
                setParam (ids::layerFadeIn (l), 0.0f);
                setParam (ids::layerFadeOut (l), 0.0f);
            }
            break;

        case 1: // Tight 35ms Snapback
            setParam (ids::preRollMs, 35.0f);
            for (int l = 0; l < 3; ++l)
            {
                setParam (ids::layerEnvEnabled (l), 1.0f);
                setParam (ids::layerAttack (l), 8.0f);
                setParam (ids::layerDecay (l), 45.0f);
                setParam (ids::layerSustain (l), 0.3f);
                setParam (ids::layerRelease (l), 25.0f);
                setParam (ids::layerHighPass (l), 180.0f);
                setParam (ids::layerDelay (l), (float) l * -4.0f);
            }
            for (int l = 3; l < 6; ++l)
            {
                setParam (ids::layerEnvEnabled (l), 1.0f);
                setParam (ids::layerAttack (l), 1.0f);
                setParam (ids::layerDecay (l), 140.0f);
                setParam (ids::layerSustain (l), 0.65f);
                setParam (ids::layerRelease (l), 90.0f);
                setParam (ids::layerDelay (l), 0.0f);
            }
            break;

        case 2: // Layered Drum Smack
            setParam (ids::preRollMs, 45.0f);
            setParam (ids::layerPan (1), -0.35f);
            setParam (ids::layerPan (2), 0.35f);
            setParam (ids::layerPan (4), -0.25f);
            setParam (ids::layerPan (5), 0.25f);
            for (int l = 0; l < 6; ++l)
            {
                setParam (ids::layerGain (l), 1.15f);
                setParam (ids::layerEnvEnabled (l), 0.0f);
                setParam (ids::layerFadeOut (l), 35.0f);
            }
            break;

        case 3: // Wide Reverse Swell
            setParam (ids::preRollMs, 85.0f);
            for (int l = 0; l < 3; ++l)
            {
                setParam (ids::layerReverse (l), 1.0f);
                setParam (ids::layerFadeIn (l), 40.0f);
                setParam (ids::layerFadeOut (l), 12.0f);
                setParam (ids::layerPan (l), l == 0 ? 0.0f : (l == 1 ? -0.6f : 0.6f));
                setParam (ids::layerHighPass (l), 140.0f);
            }
            for (int l = 3; l < 6; ++l)
            {
                setParam (ids::layerReverse (l), 0.0f);
                setParam (ids::layerFadeIn (l), 0.0f);
            }
            break;

        case 4: // Sub Transient Punch
            setParam (ids::preRollMs, 30.0f);
            setParam (ids::layerLowPass (3), 1800.0f);
            setParam (ids::layerPogo (3), 0.55f);
            setParam (ids::layerPitch (3), -3.0f);
            setParam (ids::layerHighPass (4), 350.0f);
            setParam (ids::layerHighPass (5), 1000.0f);
            break;

        case 5: // Cyber Metallic Stack
            setParam (ids::preRollMs, 60.0f);
            setParam (ids::layerPitch (0), 7.0f);
            setParam (ids::layerPitch (1), 12.0f);
            setParam (ids::layerPitch (2), -5.0f);
            setParam (ids::layerPogo (4), -0.45f);
            setParam (ids::layerPogo (5), 0.65f);
            break;

        default:
            break;
    }

    if (onSampleStateChanged)
        onSampleStateChanged();
}

void PluginHeaderComponent::openMainMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("SPECTRAL MULTIPLE");
    menu.addItem (1, "Fire Manual Trigger");
    menu.addItem (2, "Reset Scope & Meters");
    menu.addItem (3, "Reset Timeline Zoom (1.0x)");
    menu.addSeparator();
    menu.addSectionHeader ("TIMELINE TEMPO (" + juce::String ((int) currentBpm) + " BPM)");
    menu.addItem (10, "Set 90 BPM");
    menu.addItem (11, "Set 120 BPM");
    menu.addItem (12, "Set 140 BPM");
    menu.addItem (13, "Set 172 BPM");
    menu.addItem (14, "Decrease BPM (-5)");
    menu.addItem (15, "Increase BPM (+5)");
    menu.addSeparator();
    menu.addItem (20, "Clear All Loaded Samples");

    auto options = juce::PopupMenu::Options()
                       .withTargetComponent (&menuBtn)
                       .withMinimumWidth (200)
                       .withStandardItemHeight (24);
    if (auto* top = getTopLevelComponent())
        options = options.withParentComponent (top);

    juce::Component::SafePointer<PluginHeaderComponent> safeThis (this);
    menu.showMenuAsync (options, [safeThis] (int id)
    {
        if (safeThis == nullptr || id == 0)
            return;
        switch (id)
        {
            case 1: safeThis->proc.requestTrigger(); break;
            case 2:
                safeThis->proc.requestReset();
                if (safeThis->onResetScope) safeThis->onResetScope();
                break;
            case 3:
                if (safeThis->onResetZoom) safeThis->onResetZoom();
                break;
            case 10: safeThis->setBpm (90.0); break;
            case 11: safeThis->setBpm (120.0); break;
            case 12: safeThis->setBpm (140.0); break;
            case 13: safeThis->setBpm (172.0); break;
            case 14: safeThis->setBpm (safeThis->currentBpm - 5.0); break;
            case 15: safeThis->setBpm (safeThis->currentBpm + 5.0); break;
            case 20:
                for (int l = 0; l < WaveLensProcessor::kMaxSignals; ++l)
                    safeThis->proc.clearSample (l);
                if (safeThis->onSampleStateChanged)
                    safeThis->onSampleStateChanged();
                break;
            default:
                break;
        }
    });
}

void PluginHeaderComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    // SVG [01]: header shares the plugin background #110e16, no border line
    g.setColour (ui::kMainBg);
    g.fillRect (bounds);

    // ---- WAVESTACK wordmark: hand-drawn letters from the SVG paths, filled
    // with the horizontal a888eb -> 9e92f2 -> 8b9cf6 logo gradient.
    // SVG logo box is 226x22 at (287,11) on an 800px canvas -> centred 28%.
    const float logoW = getWidth() * 0.2825f;
    const float logoH = getHeight() * 0.5f;
    const float logoX = (getWidth() - logoW) * 0.5f;
    const float logoY = (getHeight() - logoH) * 0.5f;

    // The SVG letters span x=291..509 (218 units of the 226-wide box) and
    // y=14..30 (16 units of the 22 box). Scale those unit coordinates.
    const float sx = logoW / 226.0f;
    const float sy = logoH / 22.0f;
    const auto map = [&] (float x, float y)
    {
        return juce::Point<float> (logoX + (x - 287.0f) * sx,
                                   logoY + (y - 11.0f) * sy);
    };

    juce::Path letter;

    // W  (291..317, 14..30)
    letter.startNewSubPath (map (291, 14));
    letter.lineTo (map (296, 14));
    letter.lineTo (map (299.2f, 24));
    letter.lineTo (map (302.2f, 14));
    letter.lineTo (map (305.8f, 14));
    letter.lineTo (map (308.8f, 24));
    letter.lineTo (map (312, 14));
    letter.lineTo (map (317, 14));
    letter.lineTo (map (312, 30));
    letter.lineTo (map (307.5f, 30));
    letter.lineTo (map (304, 19.8f));
    letter.lineTo (map (300.5f, 30));
    letter.lineTo (map (296, 30));
    letter.closeSubPath();

    // A  (321..341)
    letter.startNewSubPath (map (321, 30));
    letter.lineTo (map (326.5f, 14));
    letter.lineTo (map (335.5f, 14));
    letter.lineTo (map (341, 30));
    letter.lineTo (map (336, 30));
    letter.lineTo (map (334.7f, 26.2f));
    letter.lineTo (map (327.3f, 26.2f));
    letter.lineTo (map (326, 30));
    letter.closeSubPath();
    letter.startNewSubPath (map (328.4f, 22.8f));
    letter.lineTo (map (333.6f, 22.8f));
    letter.lineTo (map (331.9f, 17.4f));
    letter.lineTo (map (330.1f, 17.4f));
    letter.closeSubPath();

    // V  (345..365)
    letter.startNewSubPath (map (345, 14));
    letter.lineTo (map (350, 14));
    letter.lineTo (map (355, 25.6f));
    letter.lineTo (map (360, 14));
    letter.lineTo (map (365, 14));
    letter.lineTo (map (358.8f, 30));
    letter.lineTo (map (351.2f, 30));
    letter.closeSubPath();

    // E  (369..389)
    letter.startNewSubPath (map (389, 17.5f));
    letter.lineTo (map (374.2f, 17.5f));
    letter.lineTo (map (374.2f, 20.3f));
    letter.lineTo (map (386.5f, 20.3f));
    letter.lineTo (map (386.5f, 23.7f));
    letter.lineTo (map (374.2f, 23.7f));
    letter.lineTo (map (374.2f, 26.5f));
    letter.lineTo (map (389, 26.5f));
    letter.lineTo (map (389, 30));
    letter.lineTo (map (372, 30));
    letter.quadraticTo (map (369, 30), map (369, 27));
    letter.lineTo (map (369, 17));
    letter.quadraticTo (map (369, 14), map (372, 14));
    letter.lineTo (map (389, 14));
    letter.closeSubPath();

    // S  (393..413)
    letter.startNewSubPath (map (413, 17.5f));
    letter.lineTo (map (398, 17.5f));
    letter.lineTo (map (398, 20.2f));
    letter.lineTo (map (410, 20.2f));
    letter.quadraticTo (map (413, 20.2f), map (413, 23));
    letter.lineTo (map (413, 27));
    letter.quadraticTo (map (413, 30), map (410, 30));
    letter.lineTo (map (393, 30));
    letter.lineTo (map (393, 26.5f));
    letter.lineTo (map (408, 26.5f));
    letter.lineTo (map (408, 23.8f));
    letter.lineTo (map (396, 23.8f));
    letter.quadraticTo (map (393, 23.8f), map (393, 21));
    letter.lineTo (map (393, 17));
    letter.quadraticTo (map (393, 14), map (396, 14));
    letter.lineTo (map (413, 14));
    letter.closeSubPath();

    // T  (417..437)
    letter.startNewSubPath (map (417, 14));
    letter.lineTo (map (437, 14));
    letter.lineTo (map (437, 17.6f));
    letter.lineTo (map (429.6f, 17.6f));
    letter.lineTo (map (429.6f, 30));
    letter.lineTo (map (424.4f, 30));
    letter.lineTo (map (424.4f, 17.6f));
    letter.lineTo (map (417, 17.6f));
    letter.closeSubPath();

    // A2 (441..461)
    letter.startNewSubPath (map (441, 30));
    letter.lineTo (map (446.5f, 14));
    letter.lineTo (map (455.5f, 14));
    letter.lineTo (map (461, 30));
    letter.lineTo (map (456, 30));
    letter.lineTo (map (454.7f, 26.2f));
    letter.lineTo (map (447.3f, 26.2f));
    letter.lineTo (map (446, 30));
    letter.closeSubPath();
    letter.startNewSubPath (map (448.4f, 22.8f));
    letter.lineTo (map (453.6f, 22.8f));
    letter.lineTo (map (451.9f, 17.4f));
    letter.lineTo (map (450.1f, 17.4f));
    letter.closeSubPath();

    // C  (465..485)
    letter.startNewSubPath (map (485, 17.6f));
    letter.lineTo (map (470.2f, 17.6f));
    letter.lineTo (map (470.2f, 26.4f));
    letter.lineTo (map (485, 26.4f));
    letter.lineTo (map (485, 30));
    letter.lineTo (map (468, 30));
    letter.quadraticTo (map (465, 30), map (465, 27));
    letter.lineTo (map (465, 17));
    letter.quadraticTo (map (465, 14), map (468, 14));
    letter.lineTo (map (485, 14));
    letter.closeSubPath();

    // K  (489..509)
    letter.startNewSubPath (map (489, 30));
    letter.lineTo (map (489, 14));
    letter.lineTo (map (494.2f, 14));
    letter.lineTo (map (494.2f, 20.5f));
    letter.lineTo (map (503.2f, 14));
    letter.lineTo (map (509, 14));
    letter.lineTo (map (498.6f, 21.8f));
    letter.lineTo (map (509.4f, 30));
    letter.lineTo (map (503.2f, 30));
    letter.lineTo (map (494.2f, 23));
    letter.lineTo (map (494.2f, 30));
    letter.closeSubPath();

    // Fill with the SVG "logo-gradient": a888eb -> 9e92f2 -> 8b9cf6 across x
    juce::ColourGradient gradient (juce::Colour (0xffa888eb), logoX, logoY,
                                    juce::Colour (0xff8b9cf6), logoX + logoW, logoY, false);
    gradient.addColour (0.5, juce::Colour (0xff9e92f2));
    g.setGradientFill (gradient);
    g.fillPath (letter);

    // BYPASSED warning next to the power button when the plugin is off
    if (! powerBtn.getToggleState())
    {
        g.setColour (ui::kWarningAmber);
        g.setFont (juce::Font (10.0f, juce::Font::bold));
        g.drawText ("BYPASSED",
                    juce::jmax (menuBtn.getRight() + 8, 0), 0, 68, getHeight(),
                    juce::Justification::centredLeft);
    }
}

void PluginHeaderComponent::resized()
{
    // SVG [01] geometry on an 800x44 header (scaled to width):
    //   power 22x20 @19, menu 24x20 @49
    //   logo centred
    //   heart @593, preset text @610, prev arrow @751, next arrow @776
    // The header band scales y by height/44 (56px at 760), so x and y use
    // separate factors - uniform scaling pushed the icons ~8px too low.
    const float sx = (float) getWidth() / 800.0f;
    const float sy = (float) getHeight() / 44.0f;
    auto at = [sx, sy] (float x, float y, float w, float h)
    {
        return juce::Rectangle<int> (juce::roundToInt (x * sx), juce::roundToInt (y * sy),
                                     juce::roundToInt (w * sx), juce::roundToInt (h * sy));
    };

    powerBtn.setBounds (at (19, 12, 22, 20));
    menuBtn.setBounds (at (49, 12, 24, 20));

    // Right cluster: next / prev arrows flank the preset name + heart
    // (SVG chevrons centred at y=22, heart cy=22.85, name baseline y=26).
    nextPresetBtn.setBounds (at (770, 15, 20, 14));
    prevPresetBtn.setBounds (at (748, 15, 20, 14));
    presetBox.setBounds (at (604, 10, 140, 24));
    favBtn.setBounds (at (585, 12, 22, 20));
}
