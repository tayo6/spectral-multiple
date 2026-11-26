#include "PluginEditor.h"

namespace ui = wavelens::ui;

//==============================================================================
// LayerPadStripComponent - the six drum pads from SVG section [06].
// Each pad is a wire-box with a coloured trigger cap, "PAD n" label and
// small M / S square buttons on the right.
//==============================================================================
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
    selectButton.getProperties().set ("isPadTrigger", true);
    selectButton.getProperties().set ("accentColour", (juce::int64) accent.getARGB());
    addAndMakeVisible (selectButton);

    for (auto* button : { &muteButton, &soloButton })
    {
        button->setClickingTogglesState (true);
        button->getProperties().set ("accentColour", (juce::int64) accent.getARGB());
        button->setColour (juce::TextButton::textColourOffId, ui::kSecondaryText);
        button->setColour (juce::TextButton::textColourOnId, accent);
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
    // SVG pad card: #1c1a24 fill + #2c2a38 stroke (radius 2)
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff1c1a24));
    g.fillRoundedRectangle (b, 2.0f);
    g.setColour (isSelected ? accent : juce::Colour (0xff2c2a38));
    g.drawRoundedRectangle (b, 2.0f, isSelected ? 1.4f : 1.0f);
}

void LayerPadStripComponent::PadCell::resized()
{
    // SVG pad internals (pad card is 116x26): 16x16 trigger cap at (5,5)
    // drawn by the LookAndFeel inside the full-pad select button, M at
    // x=91, S at x=111 (both 16x16).
    const float w = (float) getWidth();
    const float h = (float) getHeight();
    const float ux = w / 116.0f;
    const float uy = h / 26.0f;

    soloButton.setBounds (juce::roundToInt (95.0f * ux), juce::roundToInt (5.0f * uy),
                          juce::roundToInt (16.0f * ux), juce::roundToInt (16.0f * uy));
    muteButton.setBounds (juce::roundToInt (75.0f * ux), juce::roundToInt (5.0f * uy),
                          juce::roundToInt (16.0f * ux), juce::roundToInt (16.0f * uy));
    selectButton.setBounds (getLocalBounds());
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
    // SVG [06]: #16141d footer with 1.5px #0d0b10 top separator and a solid
    // 2px #110e16 centre divider (x=399..401 of 800).
    const auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff16141d));
    g.fillRect (b);
    g.setColour (snap::tileTrack);
    g.drawHorizontalLine (0, 0.0f, b.getRight());
    g.setColour (ui::kMainBg);
    const float divX = getWidth() * 0.5f;
    g.fillRect (juce::Rectangle<float> (divX - 1.0f, 0.0f, 2.0f, b.getHeight()));
}

void LayerPadStripComponent::resized()
{
    // SVG [06] exact geometry on an 800x42 footer: pads are 116x26 at
    // y=568 (10 units below the footer top), snapback pads at x 16/142/268,
    // transient pads at x 416/542/668 (10-unit gaps).
    const float sx = (float) getWidth() / 800.0f;
    const float sy = (float) getHeight() / 42.0f;
    const float padX[] = { 16.0f, 142.0f, 268.0f, 416.0f, 542.0f, 668.0f };
    for (int i = 0; i < 6; ++i)
    {
        pads[(size_t) i]->setBounds (juce::roundToInt (padX[i] * sx),
                                     juce::roundToInt (10.0f * sy),
                                     juce::roundToInt (116.0f * sx),
                                     juce::roundToInt (26.0f * sy));
    }
}

//==============================================================================
WaveformResizeHandle::WaveformResizeHandle()
{
    setName ("Waveform resize handle");
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void WaveformResizeHandle::paint (juce::Graphics& g)
{
    // Invisible splitter grip on the toolbar/panel boundary: only a faint
    // hover highlight; the divider itself comes from the editor paint.
    if (isMouseOver())
    {
        auto bounds = getLocalBounds().toFloat();
        const float centreY = bounds.getCentreY();
        const float gripW = 34.0f;
        auto grip = juce::Rectangle<float> (bounds.getCentreX() - gripW * 0.5f,
                                            centreY - 2.0f, gripW, 4.0f);
        g.setColour (ui::kSecondaryText.withAlpha (0.35f));
        g.fillRoundedRectangle (grip, 2.0f);
    }
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
            waveformUserSet = true;
            waveformHeight = dragStartWaveformHeight + deltaY;
            resized();
        }
    };
    resizeHandle.onDoubleClick = [this]
    {
        waveformUserSet = false; // return to the SVG-proportional layout
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
    // Root canvas: SVG plugin background #110e16
    g.fillAll (ui::kMainBg);

    // SVG "Center Panel Divider": 2px #110e16 gap between the two layer
    // panels (x 399..401 of 800, spanning the panel band). The groups are
    // laid out with a 2px gap so this paints the divider through it.
    if (! editorsCollapsed && snapbackGroup.isVisible())
    {
        const float divX = getWidth() * 0.5f;
        g.setColour (ui::kMainBg);
        g.fillRect (juce::Rectangle<float> (divX - 1.0f, (float) snapbackGroup.getY(),
                                            2.0f, (float) snapbackGroup.getHeight()));
    }
}

void WaveLensEditor::setControlBarBounds (int toolbarTop, int toolbarH)
{
    // SVG: the COLLAPSE tab rises 12 units above the toolbar strip
    // (tab y 330 vs strip 342). Give the bar that overhang so the trapezoid
    // is not clipped by the component bounds; the bar paints only the strip
    // and passes clicks through outside the tab.
    const int overhang = proportionOfHeight (12.0f / 600.0f);
    controlBar.setBounds (0, toolbarTop - overhang, getWidth(), toolbarH + overhang);
    controlBar.setCollapsedHint (editorsCollapsed);
}

void WaveLensEditor::resized()
{
    auto area = getLocalBounds();
    if (area.getHeight() < 200)
        return; // not laid out yet (initial 0-size callback)

    // Exact SVG [800x600] band structure, scaled to the window:
    //   [01] header 44 | [02] visualizer 298 | [03] toolbar 28
    //   [04]/[05] panels 186 | [06] footer 42  (no gaps between bands)
    const int headerH = proportionOfHeight (44.0f / 600.0f);
    const int footerH = proportionOfHeight (42.0f / 600.0f);
    const int toolbarH = juce::jmax (24, proportionOfHeight (28.0f / 600.0f));
    const int panelH = proportionOfHeight (186.0f / 600.0f);

    header.setBounds (area.removeFromTop (headerH));
    padStrip.setBounds (area.removeFromBottom (footerH));

    const int triggerPanelSpace = triggerPanelOpen ? 44 : 0; // 6 px gap + 38 px panel

    triggerPanel.setVisible (triggerPanelOpen);
    snapbackGroup.setVisible (! editorsCollapsed);
    transientGroup.setVisible (! editorsCollapsed);
    resizeHandle.setVisible (! editorsCollapsed);

    if (editorsCollapsed)
    {
        // In COLLAPSE mode the scope takes all vertical space not needed by
        // the optional Trigger Settings drawer. The six pads remain visible
        // so every layer can still be selected.
        if (triggerPanelOpen)
        {
            masterWaveform.setBounds (area.removeFromTop (
                juce::jmax (120, area.getHeight() - 6 - 38)));
            area.removeFromTop (6);
            triggerPanel.setBounds (area.removeFromTop (38));
        }
        else
        {
            masterWaveform.setBounds (area);
        }
        setControlBarBounds (masterWaveform.getBottom(), toolbarH);
        return;
    }

    // Default scope height follows the SVG proportion unless the user has
    // dragged the splitter (waveformUserSet latches on the first drag).
    constexpr int minimumBankHeight = 150;
    constexpr int minimumScopeHeight = 150;
    const int defaultScopeHeight = area.getHeight() - toolbarH - panelH - triggerPanelSpace;
    if (! waveformUserSet)
        waveformHeight = defaultScopeHeight;

    const int maximumScopeHeight = juce::jmax (minimumScopeHeight,
                                               area.getHeight() - toolbarH
                                                   - triggerPanelSpace - minimumBankHeight);
    waveformHeight = juce::jlimit (minimumScopeHeight, maximumScopeHeight, waveformHeight);

    masterWaveform.setBounds (area.removeFromTop (waveformHeight));
    setControlBarBounds (area.getY(), toolbarH);
    area.removeFromTop (toolbarH); // the strip consumes its band (bar bounds extend 12 units above it)

    if (triggerPanelOpen)
    {
        area.removeFromTop (6);
        triggerPanel.setBounds (area.removeFromTop (38));
    }

    // Splitter grip floats on the toolbar/panel boundary; the panels still
    // receive the full remaining height (SVG bands are flush).
    const int remaining = area.getHeight();
    resizeHandle.setBounds (area.getX(), area.getY() - 3, area.getWidth(), 8);

    // SVG panel divider: panels are half width each with a 2px #110e16
    // gap between them (the editor paint fills that gap as the divider).
    const int divW = juce::jmax (2, proportionOfWidth (2.0f / 800.0f));
    const int groupW = (area.getWidth() - divW + 1) / 2;
    snapbackGroup.setBounds (area.removeFromLeft (groupW));
    area.removeFromLeft (divW);
    transientGroup.setBounds (area);
    juce::ignoreUnused (remaining);
}
