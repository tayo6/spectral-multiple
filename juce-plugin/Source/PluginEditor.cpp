#include "PluginEditor.h"

namespace ui = wavelens::ui;

WaveLensEditor::WaveLensEditor (WaveLensProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      header (p),
      masterWaveform (p),
      controlBar (p),
      triggerPanel (p),
      snapbackGroup (p, 0),
      transientGroup (p, 1)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (header);
    addAndMakeVisible (masterWaveform);
    addAndMakeVisible (controlBar);
    addChildComponent (triggerPanel);
    addAndMakeVisible (snapbackGroup);
    addAndMakeVisible (transientGroup);

    header.onResetScope = [this] { masterWaveform.repaint(); };
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
    controlBar.onZoomBarsSelected = [this] (double barsCount)
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
    };
    snapbackGroup.onSelectionChanged  = syncSelectedPair;
    transientGroup.onSelectionChanged = syncSelectedPair;

    auto onSamplesUpdated = [this] { masterWaveform.repaint(); };
    snapbackGroup.onSampleStateChanged  = onSamplesUpdated;
    transientGroup.onSampleStateChanged = onSamplesUpdated;

    masterWaveform.setSelectedPair (snapbackGroup.getSelectedLayer(),
                                    transientGroup.getSelectedLayer());

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

    auto body = area.reduced (12, 10);

    // Responsive oscilloscope height (~250 px at default 1280x760 window)
    const int oscH = juce::jlimit (178, 280, juce::roundToInt ((float) body.getHeight() * 0.36f));
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
