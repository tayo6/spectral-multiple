#pragma once

#include "PluginProcessor.h"
#include "SpectralMultipleLookAndFeel.h"
#include <functional>

/**
    Compact dark sample selector + prev/next arrows + custom dark browser popup.
    Supports file chooser browsing, clearing, quick folder sibling cycling, and drag-and-drop.
*/
class SampleBrowserComponent final : public juce::Component
{
public:
    explicit SampleBrowserComponent (WaveLensProcessor& processor);

    void setLayer (int layerIndex) noexcept;
    void refreshDisplay();

    std::function<void()> onBrowseRequested;
    std::function<void()> onClearRequested;
    std::function<void (int delta)> onStepSlotRequested;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void openBrowserMenu();

    WaveLensProcessor& proc;
    int currentLayer = 0;

    juce::TextButton prevBtn { "<" };
    juce::TextButton selectorBtn { "Select sample..." };
    juce::TextButton nextBtn { ">" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleBrowserComponent)
};
