#include "PluginEditor.h"

namespace ui = wavelens::ui;

LayerPadStripComponent::PadCell::PadCell (WaveLensProcessor& processor,
                                          int layerIndex,
                                          std::function<void()> selectCallback)
    : layer (juce::jlimit (0, 5, layerIndex)),
      accent (ui::layerColour (layer))
{
    selectButton.setButtonText ("PAD " + juce::String (layer + 1));
    const auto sampleName = processor.getSampleName (layer);
    selectButton.setTooltip ("Select " + ui::layerFullName (layer)
                             + (sampleName.isNotEmpty() ? ": " + sampleName : ": Empty"));
    selectButton.setClickingTogglesState (true);
    selectButton.onClick = [callback = std::move (selectCallback)] { if (callback) callback(); };
    selectButton.setColour (juce::TextButton::buttonColourId, ui::kControlBg);
    selectButton.setColour (juce::TextButton::textColourOffId, ui::kMainText);
    selectButton.setColour (juce::TextButton::textColourOnId, accent.brighter (0.25f));
    addAndMakeVisible (selectButton);

    for (auto* button : { &muteButton, &soloButton })
    {
        button->setClickingTogglesState (true);
        button->setColour (juce::TextButton::buttonColourId, ui::kMainBg);
        button->setColour (juce::TextButton::textColourOffId, ui::kSecondaryText);
        button->setColour (juce::TextButton::textColourOnId, accent.brighter (0.25f));
        button->getProperties().set ("accentColour", (juce::int64) accent.getARGB());
        addAndMakeVisible (*button);
    }

    muteButton.setTooltip ("Mute " + ui::layerFullName (layer));
    soloButton.setTooltip ("Solo " + ui::layerFullName (layer));
    muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, ids::layerMute (layer), muteButton);
    soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, ids::layerSolo (layer), soloButton);
}

void LayerPadStripComponent::PadCell::setSelected (bool selected)
{
    isSelected = selected;
    selectButton.setToggleState (selected, juce::dontSendNotification);
    repaint();
}

void LayerPadStripComponent::PadCell::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (isSelected ? accent.withAlpha (0.16f) : ui::kPanelBg);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (isSelected ? accent.withAlpha (0.95f) : ui::kBorder);
    g.drawRoundedRectangle (b, 4.0f, isSelected ? 1.4f : 1.0f);
    g.setColour (accent.withAlpha (isSelected ? 0.95f : 0.55f));
    g.fillRoundedRectangle (b.getX() + 4.0f, b.getY() + 5.0f, 3.0f, b.getHeight() - 10.0f, 1.5f);
}

void LayerPadStripComponent::PadCell::resized()
{
    auto r = getLocalBounds().reduced (8, 4);
    const int buttonW = juce::jlimit (22, 30, juce::roundToInt (r.getHeight() * 0.78f));
    soloButton.setBounds (r.removeFromRight (buttonW));
    r.removeFromRight (3);
    muteButton.setBounds (r.removeFromRight (buttonW));
    r.removeFromRight (5);
    selectButton.setBounds (r);
}

LayerPadStripComponent::LayerPadStripComponent (WaveLensProcessor& processor)
{
    for (int i = 0; i < 6; ++i)
    {
        pads[(size_t) i] = std::make_unique<PadCell> (processor, i, [this, i]
        {
            if (onLayerSelected)
                onLayerSelected (i);
        });
        addAndMakeVisible (*pads[(size_t) i]);
    }
    setSelectedLayers (0, 3);
}

void LayerPadStripComponent::setSelectedLayers (int snapbackLayer, int transientLayer)
{
    const int snap = juce::jlimit (0, 2, snapbackLayer);
    const int trans = juce::jlimit (3, 5, transientLayer);
    for (int i = 0; i < 6; ++i)
        pads[(size_t) i]->setSelected (i == snap || i == trans);
}

void LayerPadStripComponent::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (ui::kHeaderBg);
    g.fillRect (b);
    g.setColour (ui::kBorder);
    g.drawHorizontalLine (0, 0.0f, b.getRight());
    g.drawVerticalLine (getWidth() / 2, 5.0f, b.getBottom() - 5.0f);
}

void LayerPadStripComponent::resized()
{
    auto r = getLocalBounds().reduced (8, 5);
    constexpr int gap = 7;
    const int cellW = (r.getWidth() - gap * 5) / 6;
    for (int i = 0; i < 6; ++i)
    {
        pads[(size_t) i]->setBounds (r.removeFromLeft (cellW));
        if (i != 5)
            r.removeFromLeft (gap);
    }
}

WaveLensEditor::WaveLensEditor (WaveLensProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      header (p),
      masterWaveform (p),
      controlBar (p),
      triggerPanel (p),
      snapbackGroup (p, 0),
      transientGroup (p, 1),
      padStrip (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (header);
    addAndMakeVisible (masterWaveform);
    addAndMakeVisible (controlBar);
    addChildComponent (triggerPanel);
    addAndMakeVisible (snapbackGroup);
    addAndMakeVisible (transientGroup);
    addAndMakeVisible (padStrip);

    padStrip.onLayerSelected = [this] (int layer)
    {
        if (layer < 3)
            snapbackGroup.selectSlot (layer);
        else
            transientGroup.selectSlot (layer - 3);
    };

    header.onResetScope = [this]
    {
        masterWaveform.setMasterOverlayActive (false);
        masterWaveform.setOutputMonitorActive (false);
        masterWaveform.setGridMode (true);
        controlBar.setOutputActive (false);
        controlBar.setGridMode (true);
        controlBar.setDetailActive (false);
        masterWaveform.repaint();
    };
    header.onResetZoom  = [this]
    {
        masterWaveform.resetZoom();
        controlBar.setDetailActive (false);
    };
    header.onBpmChanged = [this] (double newBpm)
    {
        masterWaveform.setBpm (newBpm);
    };
    header.onSampleStateChanged = [this]
    {
        snapbackGroup.refreshState();
        transientGroup.refreshState();
        masterWaveform.repaint();
    };

    masterWaveform.onMasterToggled = [this] (bool masterActive)
    {
        if (masterActive)
            controlBar.setOutputActive (false);
    };

    masterWaveform.onZoomChanged = [this] (float z)
    {
        controlBar.setDetailActive (z > 1.05f);
        controlBar.setZoomLevel (z);
    };

    controlBar.onTriggerPanelToggled = [this] (bool open)
    {
        triggerPanelOpen = open;
        triggerPanel.setVisible (open);
        resized();
        repaint();
    };
    controlBar.onDetailToggled = [this] (bool active)
    {
        if (active)
            masterWaveform.focusTrigger (4.0f);
        else
            masterWaveform.resetZoom();
    };
    controlBar.onZoomStep = [this] (float factor)
    {
        masterWaveform.zoomBy (factor);
    };
    controlBar.onZoomReset = [this]
    {
        masterWaveform.resetZoom();
    };
    triggerPanel.onZoomBarsSelected = [this] (double barsCount)
    {
        masterWaveform.setZoomBars (barsCount);
    };
    controlBar.onGridModeChanged = [this] (bool grid)
    {
        masterWaveform.setGridMode (grid);
    };
    controlBar.onOutputToggled = [this] (bool outputActive)
    {
        masterWaveform.setOutputMonitorActive (outputActive);
    };

    auto syncSelectedPair = [this] (int)
    {
        masterWaveform.setSelectedPair (snapbackGroup.getSelectedLayer(),
                                        transientGroup.getSelectedLayer());
        padStrip.setSelectedLayers (snapbackGroup.getSelectedLayer(),
                                    transientGroup.getSelectedLayer());
    };
    snapbackGroup.onSelectionChanged  = syncSelectedPair;
    transientGroup.onSelectionChanged = syncSelectedPair;

    auto onSamplesUpdated = [this] { masterWaveform.repaint(); };
    snapbackGroup.onSampleStateChanged  = onSamplesUpdated;
    transientGroup.onSampleStateChanged = onSamplesUpdated;

    masterWaveform.setSelectedPair (snapbackGroup.getSelectedLayer(),
                                    transientGroup.getSelectedLayer());
    padStrip.setSelectedLayers (snapbackGroup.getSelectedLayer(),
                                transientGroup.getSelectedLayer());

    // Restore the timeline span from the persisted bars parameter (both the
    // drawer combo and the scope default to 1 bar, so sync once at startup).
    masterWaveform.setZoomBars (barsFromIndex (
        juce::roundToInt (proc.apvts.getRawParameterValue (ids::bars)->load())));

    setResizable (true, true);
    setResizeLimits (1020, 620, 1920, 1200);
    setSize (1280, 760);

    startTimerHz (10);
}

WaveLensEditor::~WaveLensEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void WaveLensEditor::timerCallback()
{
    proc.collectSampleGarbage();
    snapbackGroup.refreshState();
    transientGroup.refreshState();
}

void WaveLensEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::kMainBg);
}

void WaveLensEditor::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (48));

    auto body = area.reduced (12, 8);
    padStrip.setBounds (body.removeFromBottom (42));
    body.removeFromBottom (7);

    // Keep the waveform dominant while reserving enough vertical room for the
    // sample editors and the six-pad selector at the bottom.
    const int oscH = juce::jlimit (178, 286,
        juce::roundToInt ((float) body.getHeight() * 0.37f));
    masterWaveform.setBounds (body.removeFromTop (oscH));
    body.removeFromTop (6);

    controlBar.setBounds (body.removeFromTop (36));

    if (triggerPanelOpen)
    {
        body.removeFromTop (6);
        triggerPanel.setBounds (body.removeFromTop (38));
    }

    body.removeFromTop (10);

    const int gap = 12;
    const int groupW = (body.getWidth() - gap) / 2;
    snapbackGroup.setBounds (body.removeFromLeft (groupW));
    body.removeFromLeft (gap);
    transientGroup.setBounds (body);
}
