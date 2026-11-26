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
    // Six footer pads follow the SVG coordinates, including the wider 32 px
    // separation between the three SNAPBACK pads and three TRANSIENT pads.
    const float sx = (float) getWidth() / 800.0f;
    const float sy = (float) getHeight() / 42.0f;
    static constexpr int padX[] = { 16, 142, 268, 416, 542, 668 };
    for (int i = 0; i < 6; ++i)
        pads[(size_t) i]->setBounds (juce::roundToInt ((float) padX[i] * sx),
                                     juce::roundToInt (10.0f * sy),
                                     juce::roundToInt (116.0f * sx),
                                     juce::roundToInt (26.0f * sy));
}

//==============================================================================
WaveformResizeHandle::WaveformResizeHandle()
{
    setName ("Waveform resize handle");
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void WaveformResizeHandle::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float centreY = bounds.getCentreY();
    g.setColour (ui::kBorder.withAlpha (0.70f));
    g.drawHorizontalLine (juce::roundToInt (centreY), 8.0f, bounds.getRight() - 8.0f);

    const float gripW = 34.0f;
    const float gripH = 4.0f;
    auto grip = juce::Rectangle<float> (bounds.getCentreX() - gripW * 0.5f,
                                        centreY - gripH * 0.5f, gripW, gripH);
    g.setColour (ui::kSecondaryText.withAlpha (0.75f));
    g.fillRoundedRectangle (grip, 2.0f);
}

void WaveformResizeHandle::mouseDown (const juce::MouseEvent& event)
{
    dragStartScreenY = event.getScreenPosition().y;
    if (onDragStarted)
        onDragStarted();
}

void WaveformResizeHandle::mouseDrag (const juce::MouseEvent& event)
{
    if (onDragDelta)
        onDragDelta (event.getScreenPosition().y - dragStartScreenY);
}

void WaveformResizeHandle::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDoubleClick)
        onDoubleClick();
}

//==============================================================================
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
    addAndMakeVisible (resizeHandle);
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
    header.onOpenTriggerSettings = [this]
    {
        triggerPanelOpen = ! triggerPanelOpen;
        triggerPanel.setVisible (triggerPanelOpen);
        resized();
        repaint();
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
    controlBar.onCollapseToggled = [this] (bool collapsed)
    {
        editorsCollapsed = collapsed;
        snapbackGroup.setVisible (! collapsed);
        transientGroup.setVisible (! collapsed);
        resizeHandle.setVisible (! collapsed);
        resized();
        repaint();
    };

    resizeHandle.onDragStarted = [this]
    {
        dragStartWaveformHeight = waveformHeight;
    };
    resizeHandle.onDragDelta = [this] (int deltaY)
    {
        if (dragStartWaveformHeight > 0)
        {
            waveformHeight = dragStartWaveformHeight + deltaY;
            resized();
        }
    };
    resizeHandle.onDoubleClick = [this]
    {
        waveformHeight = -1;
        resized();
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
    // The outer editor follows the supplied 800 x 600 SVG coordinate system.
    // All primary regions use independent X/Y scaling so the editor remains
    // responsive to the supported DAW window sizes.
    const float sx = (float) getWidth() / 800.0f;
    const float sy = (float) getHeight() / 600.0f;
    auto sxRound = [sx] (float value) { return juce::roundToInt (value * sx); };
    auto syRound = [sy] (float value) { return juce::roundToInt (value * sy); };

    const int headerH = syRound (44.0f);
    const int footerTop = syRound (558.0f);
    const int footerH = getHeight() - footerTop;
    const int visualizerY = headerH;
    const int toolbarH = syRound (42.0f);       // 30 px strip + 12 px trapezoid rise
    const int toolbarOverlap = syRound (12.0f);
    const int defaultWaveformH = syRound (298.0f);

    header.setBounds (0, 0, getWidth(), headerH);
    padStrip.setBounds (0, footerTop, getWidth(), footerH);

    triggerPanel.setVisible (triggerPanelOpen);
    snapbackGroup.setVisible (! editorsCollapsed);
    transientGroup.setVisible (! editorsCollapsed);
    resizeHandle.setVisible (! editorsCollapsed);

    if (! editorsCollapsed)
    {
        constexpr float minimumBankHeightSvg = 150.0f;
        constexpr float minimumScopeHeightSvg = 150.0f;
        const int minimumScopeH = syRound (minimumScopeHeightSvg);
        const int minimumBankH = syRound (minimumBankHeightSvg);
        const int maximumWaveformH = juce::jmax (minimumScopeH,
            footerTop - visualizerY - toolbarH + toolbarOverlap - minimumBankH);

        if (waveformHeight < 0)
            waveformHeight = defaultWaveformH;
        waveformHeight = juce::jlimit (minimumScopeH, maximumWaveformH, waveformHeight);
    }


    const int activeWaveformHeight = editorsCollapsed
        ? footerTop - visualizerY - toolbarH + toolbarOverlap
        : waveformHeight;
    const int waveformBottom = visualizerY + activeWaveformHeight;
    const int toolbarY = waveformBottom - toolbarOverlap;
    const int bankTop = toolbarY + toolbarH;

    masterWaveform.setBounds (0, visualizerY, getWidth(), activeWaveformHeight);
    controlBar.setBounds (0, toolbarY, getWidth(), toolbarH);

    // Trigger settings are a temporary overlay, not a reflow of the SVG canvas.
    if (triggerPanelOpen)
    {
        const int panelH = syRound (38.0f);
        const int panelW = sxRound (620.0f);
        triggerPanel.setBounds ((getWidth() - panelW) / 2,
                                juce::jmax (visualizerY + syRound (8.0f), toolbarY - panelH - syRound (6.0f)),
                                panelW, panelH);
        triggerPanel.toFront (false);
    }

    if (! editorsCollapsed)
    {
        // The draggable splitter overlays the top edge of the bank panels; it
        // does not steal vertical space from the SVG's 186 px bank region.
        resizeHandle.setBounds (0, bankTop - syRound (4.0f), getWidth(), syRound (8.0f));

        const int snapW = sxRound (399.0f);
        const int transientX = sxRound (401.0f);
        snapbackGroup.setBounds (0, bankTop, snapW, footerTop - bankTop);
        transientGroup.setBounds (transientX, bankTop,
                                  getWidth() - transientX, footerTop - bankTop);
    }
}
