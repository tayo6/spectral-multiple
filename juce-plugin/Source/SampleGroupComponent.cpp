#include "SampleGroupComponent.h"

namespace ui = wavelens::ui;

void SampleGroupComponent::BankPowerButton::paintButton (juce::Graphics& g,
                                                          bool highlighted,
                                                          bool down)
{
    // SVG power icon: open circle arc + stem, ink stroke, accent when on.
    const float cx = (float) getWidth() * 0.5f;
    const float cy = (float) getHeight() * 0.5f + 1.0f;
    const float radius = juce::jmin (5.5f, (float) getHeight() * 0.27f);
    g.setColour (bankEnabled ? accent : ui::kDisabledText);
    juce::Path arc;
    arc.addCentredArc (cx, cy, radius, radius, 0.0f,
                       0.48f, juce::MathConstants<float>::twoPi - 0.48f, true);
    g.strokePath (arc, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    g.drawLine (cx, cy - radius - 2.0f, cx, cy - 0.5f, 1.3f);
    juce::ignoreUnused (highlighted, down);
}

SampleGroupComponent::SampleGroupComponent (WaveLensProcessor& processor, int groupIndex)
    : proc (processor),
      group (juce::jlimit (0, 1, groupIndex)),
      baseLayer (group * 3),
      sampleBrowser (processor)
{
    for (int i = 0; i < 3; ++i)
    {
        const int layerIdx = baseLayer + i;
        const auto col = ui::layerColour (layerIdx);
        editors[(size_t) i] = std::make_unique<LayerEnvelopeComponent> (proc, layerIdx, col);
        addChildComponent (*editors[(size_t) i]);
    }

    bankPowerBtn.setAccentColour (ui::layerColour (baseLayer));
    bankPowerBtn.onClick = [this]
    {
        bool anyLayerEnabled = false;
        for (int slot = 0; slot < 3; ++slot)
            anyLayerEnabled = anyLayerEnabled
                || proc.apvts.getRawParameterValue (ids::layerMute (baseLayer + slot))->load() < 0.5f;

        // Clicking while any layer is active mutes the whole bank; clicking
        // while all are muted enables all three layers again.
        const float nextMute = anyLayerEnabled ? 1.0f : 0.0f;
        for (int slot = 0; slot < 3; ++slot)
        {
            const auto parameterId = ids::layerMute (baseLayer + slot);
            if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (parameterId)))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (nextMute));
                parameter->endChangeGesture();
            }
        }
        refreshState();
    };
    addAndMakeVisible (bankPowerBtn);

    // SVG solo icon: headphone outline (head band + two ear pads)
    soloBtn.setButtonText ("");
    soloBtn.getProperties().set ("isHeadphone", true);
    soloBtn.getProperties().set ("accentColour", (juce::int64) ui::layerColour (baseLayer).getARGB());
    soloBtn.setClickingTogglesState (true);
    soloBtn.setTooltip ("Solo all three layers in this bank");
    soloBtn.onClick = [this]
    {
        const bool shouldSolo = soloBtn.getToggleState();
        for (int slot = 0; slot < 3; ++slot)
        {
            const auto parameterId = ids::layerSolo (baseLayer + slot);
            if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (parameterId)))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (shouldSolo ? 1.0f : 0.0f));
                parameter->endChangeGesture();
            }
        }
        refreshState();
    };
    addAndMakeVisible (soloBtn);

    auditionBtn.setTooltip ("Audition active layer sample");
    auditionBtn.onClick = [this] { proc.requestPreview (getSelectedLayer()); };
    addAndMakeVisible (auditionBtn);

    sampleBrowser.onBrowseRequested = [this] { promptLoadActiveLayer(); };
    sampleBrowser.onClearRequested = [this]
    {
        proc.clearSample (getSelectedLayer());
        refreshState();
        if (onSampleStateChanged)
            onSampleStateChanged();
    };
    sampleBrowser.onStepSlotRequested = [this] (int delta)
    {
        selectSlot ((selectedSlot + delta + 3) % 3);
    };
    addAndMakeVisible (sampleBrowser);

    clearBtn.setTooltip ("Clear active layer sample");
    clearBtn.onClick = [this]
    {
        proc.clearSample (getSelectedLayer());
        refreshState();
        if (onSampleStateChanged)
            onSampleStateChanged();
    };
    addAndMakeVisible (clearBtn);

    // The SVG header only shows power + title + headphone solo; audition and
    // clear stay available (hidden) so existing behaviour is preserved.
    auditionBtn.setVisible (false);
    clearBtn.setVisible (false);

    selectSlot (0);
}

void SampleGroupComponent::selectSlot (int slotIndex)
{
    selectedSlot = juce::jlimit (0, 2, slotIndex);
    const int activeLayer = getSelectedLayer();

    for (int i = 0; i < 3; ++i)
    {
        editors[(size_t) i]->setVisible (i == selectedSlot);
    }

    sampleBrowser.setLayer (activeLayer);
    refreshState();

    if (onSelectionChanged)
        onSelectionChanged (activeLayer);

    repaint();
}

void SampleGroupComponent::refreshState()
{
    const int mask = proc.connectedMask.load (std::memory_order_relaxed);
    const int activeLayer = getSelectedLayer();
    const bool loaded = (mask & (1 << activeLayer)) != 0;
    bool allBankSolo = true;
    bool anyBankLayerEnabled = false;
    for (int slot = 0; slot < 3; ++slot)
    {
        allBankSolo = allBankSolo
            && proc.apvts.getRawParameterValue (ids::layerSolo (baseLayer + slot))->load() > 0.5f;
        anyBankLayerEnabled = anyBankLayerEnabled
            || proc.apvts.getRawParameterValue (ids::layerMute (baseLayer + slot))->load() < 0.5f;
    }
    soloBtn.setToggleState (allBankSolo, juce::dontSendNotification);
    bankPowerBtn.setBankEnabled (anyBankLayerEnabled);

    auditionBtn.setEnabled (loaded);
    clearBtn.setEnabled (loaded);
    sampleBrowser.refreshDisplay();
    repaint();
}

void SampleGroupComponent::promptLoadActiveLayer()
{
    const int targetLayer = getSelectedLayer();
    auto chooser = std::make_shared<juce::FileChooser> (
        "Load sample for " + ui::layerFullName (targetLayer),
        juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    juce::Component::SafePointer<SampleGroupComponent> safeThis (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis, targetLayer, chooser] (const juce::FileChooser& fc)
        {
            if (safeThis == nullptr)
                return;
            const auto file = fc.getResult();
            if (file.existsAsFile())
            {
                juce::String error;
                if (! safeThis->proc.loadSampleFile (targetLayer, file, error))
                    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Sample load failed", error);
                safeThis->refreshState();
                if (safeThis->onSampleStateChanged)
                    safeThis->onSampleStateChanged();
            }
        });
}

void SampleGroupComponent::loadFileIntoActiveLayer (const juce::File& file)
{
    juce::String error;
    if (! proc.loadSampleFile (getSelectedLayer(), file, error))
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Sample load failed", error);
    refreshState();
    if (onSampleStateChanged)
        onSampleStateChanged();
}

bool SampleGroupComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff")
            || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".ogg") || f.endsWithIgnoreCase (".mp3"))
            return true;
    return false;
}

void SampleGroupComponent::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void SampleGroupComponent::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void SampleGroupComponent::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    if (! files.isEmpty())
        loadFileIntoActiveLayer (juce::File (files[0]));
    repaint();
}

void SampleGroupComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto activeAccent = ui::layerColour (getSelectedLayer());

    // SVG [04]/[05]: flat #211f28 panel, no border. The editor-level paint
    // draws the 2 px #110e16 centre divider between the two panels.
    g.setColour (ui::kPanelBg);
    g.fillRect (bounds);
    if (dragOver)
    {
        g.setColour (activeAccent);
        g.drawRect (bounds.reduced (1.0f), 2.0f);
    }

    // Section heading in the bank accent (#8b9cf7 / #aa7ef7), bold,
    // letter-spaced, after the power icon; headphone solo at the far right.
    // SVG [04]/[05]: title starts 38 panel units in (x=38 of 399), so the
    // offset must scale with the panel or it collides with the power icon.
    const juce::String title = (group == 0) ? "SNAPBACK" : "TRANSIENT";
    const float titleSx = (float) getWidth() / 399.0f;
    g.setColour (ui::bankAccentHi (baseLayer));
    g.setFont (juce::Font (juce::jlimit (11.0f, 14.0f, getHeight() * 0.062f), juce::Font::bold));
    g.drawText (title,
                juce::roundToInt (38.0f * titleSx), headerBounds.getY(),
                150, headerBounds.getHeight(),
                juce::Justification::centredLeft);
}

void SampleGroupComponent::resized()
{
    // SVG [04]/[05] geometry inside a 399x186 panel (relative layout):
    //   header row y 372..400: power icon @ (22,378), title @38, headphone @371
    //   sample box x 16..242 (226 wide); cards x 250..384
    auto area = getLocalBounds();
    const float sx = (float) area.getWidth() / 399.0f;   // horizontal scale
    const float sy = (float) area.getHeight() / 186.0f;  // vertical scale
    auto at = [sx, sy] (float x, float y, float w, float h)
    {
        return juce::Rectangle<int> (juce::roundToInt (x * sx), juce::roundToInt ((y - 372.0f) * sy),
                                     juce::roundToInt (w * sx), juce::roundToInt (h * sy));
    };

    headerBounds = area.removeFromTop (juce::roundToInt (28.0f * sy));
    bankPowerBtn.setBounds (at (22, 380, 18, 14));
    soloBtn.setBounds (at (369, 376, 20, 20));

    // SVG sample-name row: the browser sits INSIDE the sample box header
    // ("Reverse Whip > Scraping By" @ x 28..206 + prev/next chevrons).
    sampleBrowser.setBounds (at (16, 402, 226, 20));
    auditionBtn.setBounds (at (0, 0, 0, 0));
    clearBtn.setBounds (at (0, 0, 0, 0));

    // The editor fills everything below the header row.
    editorBounds = area.withTrimmedTop (juce::roundToInt (0.0f * sy))
                          .reduced (juce::roundToInt (0.0f * sx), juce::roundToInt (0.0f * sy));
    for (auto& ed : editors)
        ed->setBounds (editorBounds);
}
