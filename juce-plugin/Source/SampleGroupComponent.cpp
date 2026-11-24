#include "SampleGroupComponent.h"

namespace ui = wavelens::ui;

void SampleGroupComponent::LayerPowerButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);

    juce::Colour bg = layerOn ? accent.withAlpha (0.20f) : ui::kControlBg;
    if (down)
        bg = bg.brighter (0.16f);
    else if (highlighted)
        bg = layerOn ? accent.withAlpha (0.28f) : ui::kHoverBg;

    g.setColour (bg);
    g.fillRoundedRectangle (b, 3.5f);
    g.setColour (layerOn ? accent.withAlpha (0.85f) : ui::kBorder);
    g.drawRoundedRectangle (b, 3.5f, 1.0f);

    const float cx = b.getCentreX();
    const float cy = b.getCentreY();
    const float r = 4.6f;

    g.setColour (layerOn ? accent.brighter (0.22f) : ui::kDisabledText);
    juce::Path arc;
    arc.addCentredArc (cx, cy + 0.5f, r, r, 0.0f, 0.55f, juce::MathConstants<float>::twoPi - 0.55f, true);
    g.strokePath (arc, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine (cx, cy - 5.5f, cx, cy - 0.8f, 1.5f);
}

//==============================================================================
SampleGroupComponent::SampleGroupComponent (WaveLensProcessor& processor, int groupIndex)
    : proc (processor),
      group (juce::jlimit (0, 1, groupIndex)),
      baseLayer (group * 3),
      sampleBrowser (processor)
{
    const juce::String sectionName = (group == 0) ? "SNAPBACK" : "TRANSIENT";

    for (int i = 0; i < 3; ++i)
    {
        const int layerIdx = baseLayer + i;
        const auto col = ui::layerColour (layerIdx);

        dots[(size_t) i] = std::make_unique<SampleSelectorDot> (
            i, col, sectionName + " sample " + juce::String (i + 1));
        dots[(size_t) i]->onSelect = [this] (int slot) { selectSlot (slot); };
        addAndMakeVisible (*dots[(size_t) i]);

        editors[(size_t) i] = std::make_unique<LayerEnvelopeComponent> (proc, layerIdx, col);
        addChildComponent (*editors[(size_t) i]);
    }

    enableBtn.onClick = [this]
    {
        const int activeLayer = getSelectedLayer();
        if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (ids::layerMute (activeLayer))))
        {
            const bool currentlyMuted = param->getValue() > 0.5f;
            param->beginChangeGesture();
            param->setValueNotifyingHost (currentlyMuted ? 0.0f : 1.0f);
            param->endChangeGesture();
        }
        refreshState();
    };
    addAndMakeVisible (enableBtn);

    soloBtn.setClickingTogglesState (true);
    soloBtn.setTooltip ("Solo active layer");
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
        dots[(size_t) i]->setSelected (i == selectedSlot);
        editors[(size_t) i]->setVisible (i == selectedSlot);
    }

    rebindActiveLayerToggles();
    sampleBrowser.setLayer (activeLayer);
    refreshState();

    if (onSelectionChanged)
        onSelectionChanged (activeLayer);

    repaint();
}

void SampleGroupComponent::rebindActiveLayerToggles()
{
    const int activeLayer = getSelectedLayer();
    const auto accent = ui::layerColour (activeLayer);

    soloAtt.reset();
    soloBtn.getProperties().set ("accentColour", (juce::int64) accent.getARGB());
    soloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, ids::layerSolo (activeLayer), soloBtn);

    enableBtn.setAccentColour (accent);
}

void SampleGroupComponent::refreshState()
{
    const int mask = proc.connectedMask.load (std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i)
    {
        const int layerIdx = baseLayer + i;
        dots[(size_t) i]->setHasSample ((mask & (1 << layerIdx)) != 0);
    }

    const int activeLayer = getSelectedLayer();
    const bool loaded = (mask & (1 << activeLayer)) != 0;
    const bool muted = proc.apvts.getRawParameterValue (ids::layerMute (activeLayer))->load() > 0.5f;

    enableBtn.setLayerEnabled (! muted);
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

    // Section heading immediately preceding the three selector circles
    const juce::String title = (group == 0) ? "SNAPBACK" : "TRANSIENT";
    g.setColour (ui::kMainText);
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

    // Three small coloured circular selectors right beside the heading
    for (int i = 0; i < 3; ++i)
    {
        dots[(size_t) i]->setBounds (hdr.removeFromLeft (20).withSizeKeepingCentre (18, 18));
        hdr.removeFromLeft (2);
    }

    // Right side of section header: Enable, S, >, SampleBrowser, x
    clearBtn.setBounds (hdr.removeFromRight (22));
    hdr.removeFromRight (4);

    const int browserW = juce::jlimit (100, 210, hdr.getWidth() - 114);
    sampleBrowser.setBounds (hdr.removeFromRight (browserW));
    hdr.removeFromRight (6);

    auditionBtn.setBounds (hdr.removeFromRight (22));
    hdr.removeFromRight (4);
    soloBtn.setBounds (hdr.removeFromRight (22));
    hdr.removeFromRight (4);
    enableBtn.setBounds (hdr.removeFromRight (24));

    editorBounds = area.reduced (8, 6);
    for (auto& ed : editors)
        ed->setBounds (editorBounds);
}
