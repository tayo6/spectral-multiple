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
    const float uiScale = (float) getHeight() / 26.0f;
    g.setColour (isSelected ? accent.withAlpha (0.16f) : ui::kPanelBg);
    g.fillRoundedRectangle (b, 4.0f * uiScale);
    g.setColour (isSelected ? accent.withAlpha (0.95f) : ui::kBorder);
    g.drawRoundedRectangle (b, 4.0f * uiScale, (isSelected ? 1.4f : 1.0f) * uiScale);
    g.setColour (accent.withAlpha (isSelected ? 0.95f : 0.55f));
    g.fillRoundedRectangle (b.getX() + 4.0f * uiScale, b.getY() + 5.0f * uiScale,
                            3.0f * uiScale, b.getHeight() - 10.0f * uiScale, 1.5f * uiScale);
}

void LayerPadStripComponent::PadCell::resized()
{
    const float uiScale = (float) getHeight() / 26.0f;
    auto r = getLocalBounds().reduced (juce::roundToInt (5.0f * uiScale),
                                       juce::roundToInt (5.0f * uiScale));
    const int buttonW = juce::jmax (1, juce::roundToInt (16.0f * uiScale));
    soloButton.setBounds (r.removeFromRight (buttonW));
    r.removeFromRight (juce::roundToInt (4.0f * uiScale));
    muteButton.setBounds (r.removeFromRight (buttonW));
    r.removeFromRight (juce::roundToInt (5.0f * uiScale));
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
    const float uiScale = (float) getHeight() / 42.0f;
    g.drawHorizontalLine (0, 0.0f, b.getRight());
    g.drawVerticalLine (getWidth() / 2, 5.0f * uiScale, b.getBottom() - 5.0f * uiScale);
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
    header.onEditorSizeSelected = [this] (int preset)
    {
        applyEditorSizePreset (preset);
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
        dragStartWaveformHeightSvg = waveformHeightSvg;
    };
    resizeHandle.onDragDelta = [this] (int deltaY)
    {
        const float scale = juce::jmin ((float) getWidth() / 800.0f,
                                        (float) getHeight() / 600.0f);
        if (scale > 0.001f)
        {
            waveformHeightSvg = dragStartWaveformHeightSvg + (float) deltaY / scale;
            resized();
        }
    };
    resizeHandle.onDoubleClick = [this]
    {
        waveformHeightSvg = 298.0f; // Restore the SVG's 298 px visualizer region.
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

    // Both sample banks share one horizontal viewport so the waveform start
    // and trim context remain aligned while zooming either bank.
    snapbackGroup.onViewportChanged = [this] (float start, float zoom)
    {
        transientGroup.setViewport (start, zoom);
    };
    transientGroup.onViewportChanged = [this] (float start, float zoom)
    {
        snapbackGroup.setViewport (start, zoom);
    };

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
    applyEditorSizePreset (2); // Normal working view: 1024 x 768

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

    // SVG panel divider between the SNAPBACK and TRANSIENT banks.
    if (! editorsCollapsed)
    {
        const float scale = juce::jmin ((float) getWidth() / 800.0f,
                                        (float) getHeight() / 600.0f);
        const int canvasW = juce::roundToInt (800.0f * scale);
        const int canvasH = juce::roundToInt (600.0f * scale);
        const int offsetX = (getWidth() - canvasW) / 2;
        const int offsetY = (getHeight() - canvasH) / 2;
        g.setColour (ui::kBorder);
        g.drawVerticalLine (offsetX + juce::roundToInt (400.0f * scale),
                            (float) (offsetY + juce::roundToInt (372.0f * scale)),
                            (float) (offsetY + juce::roundToInt (558.0f * scale)));
    }
}

juce::Rectangle<int> WaveLensEditor::getLargeEditorSize() const
{
    auto& displays = juce::Desktop::getInstance().getDisplays();
    auto* display = displays.getDisplayForRect (getScreenBounds());
    if (display == nullptr)
        display = displays.getPrimaryDisplay();

    juce::Rectangle<int> available (0, 0, 1280, 800);
    if (display != nullptr)
        available = display->userArea;

    // Display::userArea already excludes operating-system desktop bars;
    // use the full available work area for the Large preset.
    int width = available.getWidth();
    int height = juce::roundToInt ((float) width * 0.75f);
    if (height > available.getHeight())
    {
        height = available.getHeight();
        width = juce::roundToInt ((float) height * (4.0f / 3.0f));
    }

    // Keep the exact 4:3 layout ratio and the editor's supported minimum size.
    width = juce::jmax (800, (width / 4) * 4);
    height = juce::jmax (600, (width / 4) * 3);
    return { width, height };
}

void WaveLensEditor::updateResizeConstraints()
{
    const auto largeSize = getLargeEditorSize();
    const int maxWidth = juce::jlimit (1024, 4096, largeSize.getWidth());
    const int maxHeight = juce::jlimit (768, 3072, largeSize.getHeight());
    setResizeLimits (800, 600, maxWidth, maxHeight);
    if (auto* resizeLimits = getConstrainer())
        resizeLimits->setFixedAspectRatio (4.0 / 3.0);
}

void WaveLensEditor::applyEditorSizePreset (int preset)
{
    currentEditorSizePreset = juce::jlimit (0, 2, preset);
    header.setEditorSizePreset (currentEditorSizePreset);
    updateResizeConstraints();

    juce::Rectangle<int> target;
    switch (currentEditorSizePreset)
    {
        case 0: target = { 800, 600 }; break; // SVG reference canvas
        case 1: target = getLargeEditorSize(); break;
        case 2:
        default: target = { 1024, 768 }; break;
    }

    setSize (target.getWidth(), target.getHeight());
}

void WaveLensEditor::resized()
{
    // Use the SVG as a fixed 800 x 600 coordinate plane. The same scale is
    // applied in both directions; if a host supplies a non-4:3 rectangle,
    // centre the intact design instead of stretching text and controls.
    const float scale = juce::jmin ((float) getWidth() / 800.0f,
                                    (float) getHeight() / 600.0f);
    if (scale <= 0.001f)
        return;

    const int canvasW = juce::roundToInt (800.0f * scale);
    const int canvasH = juce::roundToInt (600.0f * scale);
    const int offsetX = (getWidth() - canvasW) / 2;
    const int offsetY = (getHeight() - canvasH) / 2;
    auto px = [scale, offsetX] (float value) { return offsetX + juce::roundToInt (value * scale); };
    auto py = [scale, offsetY] (float value) { return offsetY + juce::roundToInt (value * scale); };
    auto ps = [scale] (float value) { return juce::roundToInt (value * scale); };

    const int headerH = ps (44.0f);
    const int footerTop = py (558.0f);
    const int footerBottom = py (600.0f);
    const int footerH = footerBottom - footerTop;
    const int visualizerY = py (44.0f);
    const int toolbarH = ps (42.0f);       // 30 px strip + 12 px trapezoid rise
    const int toolbarOverlap = ps (12.0f);

    header.setBounds (offsetX, offsetY, canvasW, headerH);
    padStrip.setBounds (offsetX, footerTop, canvasW, footerH);

    triggerPanel.setVisible (triggerPanelOpen);
    snapbackGroup.setVisible (! editorsCollapsed);
    transientGroup.setVisible (! editorsCollapsed);
    resizeHandle.setVisible (! editorsCollapsed);

    if (! editorsCollapsed)
    {
        constexpr float minimumBankHeightSvg = 150.0f;
        constexpr float minimumScopeHeightSvg = 150.0f;
        const int minimumScopeH = ps (minimumScopeHeightSvg);
        const int minimumBankH = ps (minimumBankHeightSvg);
        const int maximumWaveformH = juce::jmax (minimumScopeH,
            footerTop - visualizerY - toolbarH + toolbarOverlap - minimumBankH);
        const float maximumWaveformSvg = (float) maximumWaveformH / scale;
        waveformHeightSvg = juce::jlimit (minimumScopeHeightSvg, maximumWaveformSvg, waveformHeightSvg);
    }

    const int activeWaveformHeight = editorsCollapsed
        ? footerTop - visualizerY - toolbarH + toolbarOverlap
        : ps (waveformHeightSvg);
    const int waveformBottom = visualizerY + activeWaveformHeight;
    const int toolbarY = waveformBottom - toolbarOverlap;
    const int bankTop = toolbarY + toolbarH;

    masterWaveform.setBounds (offsetX, visualizerY, canvasW, activeWaveformHeight);
    controlBar.setBounds (offsetX, toolbarY, canvasW, toolbarH);

    // Trigger settings remain an overlay; they do not reflow the SVG canvas.
    if (triggerPanelOpen)
    {
        const int panelH = ps (38.0f);
        const int panelW = ps (720.0f);
        triggerPanel.setBounds (offsetX + (canvasW - panelW) / 2,
                                juce::jmax (visualizerY + ps (8.0f), toolbarY - panelH - ps (6.0f)),
                                panelW, panelH);
        triggerPanel.toFront (false);
    }

    if (! editorsCollapsed)
    {
        // The splitter overlays the top edge of the two SVG bank panels.
        resizeHandle.setBounds (offsetX, bankTop - ps (4.0f), canvasW, ps (8.0f));
        resizeHandle.toFront (false);

        const int snapRight = px (399.0f);
        const int transientX = px (401.0f);
        snapbackGroup.setBounds (offsetX, bankTop, snapRight - offsetX, footerTop - bankTop);
        transientGroup.setBounds (transientX, bankTop,
                                  px (800.0f) - transientX, footerTop - bankTop);
    }
}
