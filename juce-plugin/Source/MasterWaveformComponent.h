#pragma once

#include "PluginProcessor.h"
#include "SpectralMultipleLookAndFeel.h"
#include "MasterButton.h"

/**
    Main oscilloscope & timeline canvas spanning the upper width of the plugin.
    Hosts the global Master (M) toggle and renders:
      - Master (M) active: all six Snapback & Transient sample waveforms overlaid in their assigned colours.
      - Master (M) inactive: currently selected Snapback + Transient waveforms cleanly focused.
      - Output mode active: live summed Master Output (#48D6D0) oscilloscope / FFT spectrum + Meter.
*/
class MasterWaveformComponent final : public juce::Component, private juce::Timer
{
public:
    explicit MasterWaveformComponent (WaveLensProcessor& processor);
    ~MasterWaveformComponent() override;

    void setSelectedPair (int snapbackLayer, int transientLayer) noexcept;
    void setBpm (double newBpm) noexcept;
    void setGridMode (bool useGrid) noexcept;
    bool isGridMode() const noexcept { return gridMode; }

    void setZoomBars (double barsCount) noexcept;
    double getZoomBars() const noexcept { return windowBars; }

    void setMasterOverlayActive (bool active);
    bool isMasterOverlayActive() const noexcept { return masterBtn.getToggleState(); }

    void setOutputMonitorActive (bool active);
    bool isOutputMonitorActive() const noexcept { return outputMonitorMode; }

    void zoomBy (float factor, float anchorNorm = 0.5f) noexcept;
    void resetZoom() noexcept;
    void focusTrigger (float zoomIfUnzoomed = 4.0f) noexcept;
    float getZoomFactor() const noexcept { return zoomFactor; }

    std::function<void (bool masterActive)> onMasterToggled;
    std::function<void (float zoomFactor)> onZoomChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void clampViewport (float nextStart, float nextZoom) noexcept;
    float normToX (float norm, float plotX, float plotW) const noexcept;
    float xToNorm (float x, float plotX, float plotW) const noexcept;

    void paintTimelineWaveforms (juce::Graphics&, juce::Rectangle<float> plot, juce::Rectangle<float> ruler);
    void paintLiveOutputMonitor (juce::Graphics&, juce::Rectangle<float> area);
    void paintLiveSpectrum (juce::Graphics&, juce::Rectangle<float> area);

    WaveLensProcessor& proc;
    MasterButton masterBtn;

    int selectedSnapback = 0;
    int selectedTransient = 3;
    double bpm = 120.0;
    double windowBars = 1.0;
    bool gridMode = true;
    bool outputMonitorMode = false;

    float zoomFactor = 1.0f;
    float viewStartNorm = 0.0f;
    float dragStartX = 0.0f;
    float dragStartView = 0.0f;

    uint32_t lastObservedTriggerCount = 0;
    float triggerFlashAlpha = 0.0f;

    std::atomic<float>* pView = nullptr;
    std::atomic<float>* pCapture = nullptr;
    std::atomic<float>* pBars = nullptr;
    std::atomic<float>* pTimeMs = nullptr;
    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pHold = nullptr;
    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pTriggerMode = nullptr;
    std::atomic<float>* pPreRollMs = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterWaveformComponent)
};
