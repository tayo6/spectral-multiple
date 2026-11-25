#include "SampleGroupComponent.h"

namespace ui = wavelens::ui;

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

    soloBtn.setClickingTogglesState (true);
    soloBtn.setTooltip ("Solo all three layers in this bank");
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
    for (int slot = 0; slot < 3; ++slot)
        allBankSolo = allBankSolo
            && proc.apvts.getRawParameterValue (ids::layerSolo (baseLayer + slot))->load() > 0.5f;
    soloBtn.setToggleState (allBankSolo, juce::dontSendNotification);

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

    // Left accent marker
    g.setColour (activeAccent);
    g.fillRoundedRectangle (hdr.getX() + 6.0f, hdr.getCentreY() - 7.0f, 3.0f, 14.0f, 1.5f);

    // Section heading (bank accent) immediately preceding the three selector circles
    const juce::String title = (group == 0) ? "SNAPBACK" : "TRANSIENT";
    g.setColour (snap::bankAccentHi (group * 3));
    g.setFont (juce::Font (13.5f, juce::Font::bold));
    g.drawText (title,
                headerBounds.getX() + 14, headerBounds.getY(),
                92, headerBounds.getHeight(),
                juce::Justification::centredLeft);
}

void SampleGroupComponent::resized()
{
    auto area = getLocalBounds();
    headerBounds = area.removeFromTop (38);

    auto hdr = headerBounds.reduced (10, 7);
    hdr.removeFromLeft (98); // Section heading space ("SNAPBACK" / "TRANSIENT")

    // Layer selection is handled by the six footer pads.
    // Right side of section header: bank Solo, audition, sample browser and clear.
    clearBtn.setBounds (hdr.removeFromRight (22));
    hdr.removeFromRight (4);

    const int browserW = juce::jlimit (100, 250, hdr.getWidth() - 88);
    sampleBrowser.setBounds (hdr.removeFromRight (browserW));
    hdr.removeFromRight (6);

    auditionBtn.setBounds (hdr.removeFromRight (22));
    hdr.removeFromRight (4);
    soloBtn.setBounds (hdr.removeFromRight (22));

    editorBounds = area.reduced (8, 6);
    for (auto& ed : editors)
        ed->setBounds (editorBounds);
}
