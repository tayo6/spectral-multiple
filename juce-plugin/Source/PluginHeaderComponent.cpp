#include "PluginHeaderComponent.h"

namespace ui = wavelens::ui;

void PluginHeaderComponent::IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const float uiScale = (float) getHeight() / 20.0f;
    const bool active = getToggleState();

    juce::Colour bg = active ? ui::kSelectedAccent.withAlpha (0.20f) : ui::kControlBg;
    if (down)
        bg = bg.brighter (0.16f);
    else if (highlighted)
        bg = active ? ui::kSelectedAccent.withAlpha (0.28f) : ui::kHoverBg;

    g.setColour (bg);
    g.fillRoundedRectangle (b, 4.0f * uiScale);
    g.setColour (active ? ui::kPurpleAccent.withAlpha (0.85f) : ui::kBorder);
    g.drawRoundedRectangle (b, 4.0f * uiScale, 1.0f * uiScale);

    const float cx = b.getCentreX();
    const float cy = b.getCentreY();
    const juce::Colour strokeCol = active ? ui::kPurpleAccent.brighter (0.2f)
                                          : (highlighted ? ui::kMainText : ui::kSecondaryText);

    g.setColour (strokeCol);

    if (kind == Kind::Power)
    {
        const float r = 5.5f * uiScale;
        juce::Path arc;
        arc.addCentredArc (cx, cy + 0.5f * uiScale, r, r, 0.0f,
                           0.55f, juce::MathConstants<float>::twoPi - 0.55f, true);
        g.strokePath (arc, juce::PathStrokeType (1.7f * uiScale, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        g.drawLine (cx, cy - 6.5f * uiScale, cx, cy - 1.0f * uiScale, 1.7f * uiScale);
    }
    else if (kind == Kind::Menu)
    {
        for (int i = -1; i <= 1; ++i)
        {
            const float y = cy + (float) i * 4.0f * uiScale;
            g.drawLine (cx - 6.0f * uiScale, y, cx + 6.0f * uiScale, y, 1.5f * uiScale);
        }
    }
    else // Heart
    {
        juce::Path heart;
        heart.startNewSubPath (cx, cy + 5.2f * uiScale);
        heart.cubicTo (cx - 7.5f * uiScale, cy + 0.8f * uiScale,
                       cx - 6.5f * uiScale, cy - 5.5f * uiScale,
                       cx, cy - 2.2f * uiScale);
        heart.cubicTo (cx + 6.5f * uiScale, cy - 5.5f * uiScale,
                       cx + 7.5f * uiScale, cy + 0.8f * uiScale,
                       cx, cy + 5.2f * uiScale);
        heart.closeSubPath();

        if (active)
            g.fillPath (heart);
        else
            g.strokePath (heart, juce::PathStrokeType (1.35f * uiScale,
                                                       juce::PathStrokeType::curved,
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
    addAndMakeVisible (prevPresetBtn);

    nextPresetBtn.setTooltip ("Next preset");
    nextPresetBtn.onClick = [this] { stepPreset (1); };
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
    menu.addItem (4, "Trigger Settings");
    juce::PopupMenu editorSizes;
    editorSizes.addItem (30, "Extra Large — SVG reference (800 × 600)", true, currentEditorSizePreset == 0);
    editorSizes.addItem (31, "Large — Fit available screen", true, currentEditorSizePreset == 1);
    editorSizes.addItem (32, "Normal — 1024 × 768", true, currentEditorSizePreset == 2);
    menu.addSubMenu ("Editor Size", editorSizes);
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
            case 4:
                if (safeThis->onOpenTriggerSettings) safeThis->onOpenTriggerSettings();
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
            case 30:
                safeThis->setEditorSizePreset (0);
                if (safeThis->onEditorSizeSelected) safeThis->onEditorSizeSelected (0);
                break;
            case 31:
                safeThis->setEditorSizePreset (1);
                if (safeThis->onEditorSizeSelected) safeThis->onEditorSizeSelected (1);
                break;
            case 32:
                safeThis->setEditorSizePreset (2);
                if (safeThis->onEditorSizeSelected) safeThis->onEditorSizeSelected (2);
                break;
            default:
                break;
        }
    });
}

void PluginHeaderComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (ui::kHeaderBg);
    g.fillRect (bounds);
    g.setColour (ui::kBorder);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // Centered WAVESTACK wordmark, matching the supplied coloured SVG identity.
    g.setGradientFill (juce::ColourGradient (
        juce::Colour::fromRGB (0xa8, 0x88, 0xeb), 0.0f, 0.0f,
        juce::Colour::fromRGB (0x8b, 0x9c, 0xf6), (float) getWidth(), 0.0f, false));
    const float uiScale = (float) getWidth() / 800.0f;
    g.setFont (juce::Font (19.0f * uiScale, juce::Font::bold));
    g.drawText ("W A V E S T A C K",
                getLocalBounds(), juce::Justification::centred);

    // Compact BPM readout between the - / + buttons
    if (! bpmValueBounds.isEmpty())
    {
        g.setColour (ui::kMainText);
        g.setFont (juce::Font (11.0f * uiScale, juce::Font::bold));
        g.drawText (juce::String ((int) currentBpm) + " BPM",
                    bpmValueBounds, juce::Justification::centred);
    }

    if (! powerBtn.getToggleState())
    {
        g.setColour (ui::kWarningAmber.withAlpha (0.85f));
        g.setFont (juce::Font (10.0f * uiScale, juce::Font::bold));
        const int labelX = juce::jmax (resetBtn.getRight() + juce::roundToInt (8.0f * uiScale),
                                       menuBtn.getRight() + juce::roundToInt (8.0f * uiScale));
        g.drawFittedText ("BYPASSED",
                          juce::Rectangle<int> (labelX, 0,
                                                juce::jmax (1, getWidth() - labelX - juce::roundToInt (8.0f * uiScale)),
                                                getHeight()),
                          juce::Justification::centredLeft, 1);
    }
}

void PluginHeaderComponent::resized()
{
    const float sx = (float) getWidth() / 800.0f;
    const float sy = (float) getHeight() / 44.0f;
    auto px = [sx] (float v) { return juce::roundToInt (v * sx); };
    auto py = [sy] (float v) { return juce::roundToInt (v * sy); };

    powerBtn.setBounds (px (19), py (12), px (22), py (20));
    menuBtn.setBounds (px (49), py (12), px (24), py (20));
    resetBtn.setBounds ({});
    bpmDownBtn.setBounds ({});
    bpmUpBtn.setBounds ({});
    bpmValueBounds = {};

    favBtn.setBounds (px (582), py (10), px (24), py (24));
    presetBox.setBounds (px (610), py (8), px (132), py (28));
    prevPresetBtn.setBounds (px (748), py (10), px (20), py (24));
    nextPresetBtn.setBounds (px (776), py (10), px (20), py (24));
}
