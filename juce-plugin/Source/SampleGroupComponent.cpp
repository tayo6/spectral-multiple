#include "SampleGroupComponent.h"

namespace ui = wavelens::ui;

void SampleGroupComponent::BankSoloButton::paintButton (juce::Graphics& g,
                                                         bool highlighted,
                                                         bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const float uiScale = (float) getHeight() / 18.0f;
    const bool active = getToggleState();
    auto fill = active ? accent.withAlpha (0.18f) : ui::kControlBg;
    if (down) fill = fill.brighter (0.15f);
    else if (highlighted) fill = fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (b, 3.5f * uiScale);
    g.setColour (active ? accent.withAlpha (0.9f) : ui::kBorder);
    g.drawRoundedRectangle (b, 3.5f * uiScale, 1.0f * uiScale);

    const float cx = b.getCentreX();
    const float cy = b.getCentreY() + 1.0f * uiScale;
    const float radius = juce::jmin (6.0f * uiScale, b.getHeight() * 0.30f);
    juce::Path band;
    band.addCentredArc (cx, cy, radius, radius, 0.0f,
                        juce::MathConstants<float>::pi,
                        juce::MathConstants<float>::twoPi, true);
    g.setColour (active ? accent.brighter (0.2f) : ui::kSecondaryText);
    g.strokePath (band, juce::PathStrokeType (1.4f * uiScale, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    g.fillRoundedRectangle (cx - radius - 1.0f * uiScale, cy - 1.0f * uiScale,
                            2.2f * uiScale, 5.0f * uiScale, 1.0f * uiScale);
    g.fillRoundedRectangle (cx + radius - 1.2f * uiScale, cy - 1.0f * uiScale,
                            2.2f * uiScale, 5.0f * uiScale, 1.0f * uiScale);
}

void SampleGroupComponent::BankPowerButton::paintButton (juce::Graphics& g,
                                                          bool highlighted,
                                                          bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const float uiScale = (float) getHeight() / 18.0f;
    auto fill = bankEnabled ? accent.withAlpha (0.18f) : ui::kControlBg;
    if (down) fill = fill.brighter (0.15f);
    else if (highlighted) fill = fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (b, 3.5f * uiScale);
    g.setColour (bankEnabled ? accent.withAlpha (0.9f) : ui::kBorder);
    g.drawRoundedRectangle (b, 3.5f * uiScale, 1.0f * uiScale);

    const float cx = b.getCentreX();
    const float cy = b.getCentreY();
    const float radius = juce::jmin (6.0f * uiScale, b.getHeight() * 0.27f);
    g.setColour (bankEnabled ? accent.brighter (0.25f) : ui::kDisabledText);
    juce::Path arc;
    arc.addCentredArc (cx, cy + 1.0f * uiScale, radius, radius, 0.0f,
                       0.48f, juce::MathConstants<float>::twoPi - 0.48f, true);
    g.strokePath (arc, juce::PathStrokeType (1.5f * uiScale, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    g.drawLine (cx, cy - radius - 2.0f * uiScale,
                cx, cy - 0.5f * uiScale, 1.5f * uiScale);
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

    soloBtn.setClickingTogglesState (true);
    soloBtn.setAccentColour (ui::layerColour (baseLayer));
    soloBtn.getProperties().set ("accentColour", (juce::int64) ui::layerColour (baseLayer).getARGB());
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
    addChildComponent (auditionBtn);

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
    addChildComponent (clearBtn);

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
        juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.aac;*.mp4;*.3gp;*.opus");

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
            || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".ogg") || f.endsWithIgnoreCase (".mp3")
            || f.endsWithIgnoreCase (".m4a") || f.endsWithIgnoreCase (".aac") || f.endsWithIgnoreCase (".mp4")
            || f.endsWithIgnoreCase (".3gp") || f.endsWithIgnoreCase (".opus"))
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

    g.setColour (ui::kPanelBg);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (dragOver ? activeAccent : ui::kBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, dragOver ? 1.6f : 1.0f);

    // Subtle section header strip
    auto hdr = headerBounds.toFloat();
    g.setColour (ui::kHeaderBg.withAlpha (0.70f));
    g.fillRoundedRectangle (hdr.getX(), hdr.getY(), hdr.getWidth(), hdr.getHeight(), 5.0f);
    g.setColour (ui::kBorder);
    g.drawHorizontalLine (headerBounds.getBottom(), (float) headerBounds.getX(), (float) headerBounds.getRight());

    // Section heading matches the SVG's text anchor at x=38.
    const juce::String title = (group == 0) ? "SNAPBACK" : "TRANSIENT";
    const float titleFontScale = (float) getHeight() / 186.0f;
    const float titleXScale = (float) getWidth() / 399.0f;
    g.setColour (snap::bankAccentHi (group * 3));
    g.setFont (juce::Font (11.0f * titleFontScale, juce::Font::bold));
    g.drawText (title,
                juce::roundToInt (38.0f * titleXScale), headerBounds.getY(),
                juce::roundToInt (110.0f * titleXScale), headerBounds.getHeight(),
                juce::Justification::centredLeft);
}

void SampleGroupComponent::resized()
{
    const float sx = (float) getWidth() / 399.0f;
    const float sy = (float) getHeight() / 186.0f;
    auto scaleX = [sx] (float value) { return juce::roundToInt (value * sx); };
    auto scaleY = [sy] (float value) { return juce::roundToInt (value * sy); };

    // SVG bank header: power at left, title, and headphone-style solo at right.
    headerBounds = { 0, 0, getWidth(), scaleY (28) };
    bankPowerBtn.setBounds (scaleX (16), scaleY (5), scaleX (18), scaleY (18));
    soloBtn.setBounds (getWidth() - scaleX (34), scaleY (4), scaleX (18), scaleY (18));

    // SVG sample area is x=16, y=28, w=368, h=152 within each bank.
    editorBounds = { scaleX (16), scaleY (28), scaleX (368), scaleY (152) };
    for (auto& ed : editors)
        ed->setBounds (editorBounds);

    // Sample name and slot navigation live in the waveform header, not the bank header.
    const auto sampleHeader = editors[(size_t) selectedSlot]->getSampleHeaderBounds();
    sampleBrowser.setBounds (editorBounds.getX() + sampleHeader.getX(),
                             editorBounds.getY() + sampleHeader.getY(),
                             sampleHeader.getWidth(), sampleHeader.getHeight());
}
