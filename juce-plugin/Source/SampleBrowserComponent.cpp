#include "SampleBrowserComponent.h"

SampleBrowserComponent::SampleBrowserComponent (WaveLensProcessor& processor)
    : proc (processor)
{
    prevBtn.setTooltip ("Select previous sample slot in this section");
    prevBtn.onClick = [this]
    {
        if (onStepSlotRequested)
            onStepSlotRequested (-1);
    };
    addAndMakeVisible (prevBtn);

    nextBtn.setTooltip ("Select next sample slot in this section");
    nextBtn.onClick = [this]
    {
        if (onStepSlotRequested)
            onStepSlotRequested (1);
    };
    addAndMakeVisible (nextBtn);

    selectorBtn.setTooltip ("Click to browse or manage audio sample");
    selectorBtn.onClick = [this] { openBrowserMenu(); };
    addAndMakeVisible (selectorBtn);

    refreshDisplay();
}

void SampleBrowserComponent::setLayer (int layerIndex) noexcept
{
    currentLayer = juce::jlimit (0, WaveLensProcessor::kMaxSignals - 1, layerIndex);
    refreshDisplay();
}

void SampleBrowserComponent::refreshDisplay()
{
    namespace ui = wavelens::ui;

    const auto name = proc.getSampleName (currentLayer);
    const bool missing = proc.isLayerSampleMissing (currentLayer);
    const auto col = ui::layerColour (currentLayer);

    if (missing)
    {
        selectorBtn.setButtonText ("Missing: " + proc.getMissingSampleFileName (currentLayer));
        selectorBtn.setColour (juce::TextButton::textColourOffId, ui::kWarningAmber);
    }
    else if (name.isNotEmpty())
    {
        selectorBtn.setButtonText (name);
        selectorBtn.setColour (juce::TextButton::textColourOffId, ui::kMainText);
    }
    else
    {
        selectorBtn.setButtonText ("Load Sample...");
        selectorBtn.setColour (juce::TextButton::textColourOffId, ui::kSecondaryText);
    }

    selectorBtn.getProperties().set ("accentColour", (juce::int64) col.getARGB());
    repaint();
}

void SampleBrowserComponent::openBrowserMenu()
{
    juce::PopupMenu menu;
    const auto layerName = wavelens::ui::layerFullName (currentLayer);
    const auto sampleName = proc.getSampleName (currentLayer);
    const bool hasSample = sampleName.isNotEmpty();

    menu.addSectionHeader (layerName.toUpperCase() + " SAMPLE");
    menu.addItem (1, hasSample ? "Replace Audio File..." : "Load Audio File...");
    menu.addItem (2, "Clear Sample", hasSample || proc.isLayerSampleMissing (currentLayer));
    menu.addItem (4, "Preview Sample", hasSample);

    const bool reversed = proc.apvts.getRawParameterValue (ids::layerReverse (currentLayer))->load() > 0.5f;
    menu.addItem (3, "Reverse Sample", true, reversed);

    menu.addSeparator();
    menu.addSectionHeader ("LOADED LAYERS");
    const int base = (currentLayer < 3) ? 0 : 3;
    for (int slot = 0; slot < 3; ++slot)
    {
        const int idx = base + slot;
        const auto sName = proc.getSampleName (idx);
        const auto label = wavelens::ui::layerFullName (idx) + ": "
                         + (sName.isNotEmpty() ? sName : juce::String ("Empty"));
        menu.addItem (10 + slot, label, true, idx == currentLayer);
    }

    auto options = juce::PopupMenu::Options()
                       .withTargetComponent (&selectorBtn)
                       .withMinimumWidth (juce::jmax (170, selectorBtn.getWidth()))
                       .withStandardItemHeight (24);
    if (auto* top = getTopLevelComponent())
        options = options.withParentComponent (top);

    juce::Component::SafePointer<SampleBrowserComponent> safeThis (this);
    menu.showMenuAsync (options, [safeThis] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        if (result == 1 && safeThis->onBrowseRequested)
            safeThis->onBrowseRequested();
        else if (result == 2 && safeThis->onClearRequested)
            safeThis->onClearRequested();
        else if (result == 4)
        {
            safeThis->proc.requestPreview (safeThis->currentLayer);
        }
        else if (result == 3)
        {
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (
                    safeThis->proc.apvts.getParameter (ids::layerReverse (safeThis->currentLayer))))
            {
                const bool cur = safeThis->proc.apvts.getRawParameterValue (ids::layerReverse (safeThis->currentLayer))->load() > 0.5f;
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (cur ? 0.0f : 1.0f));
                p->endChangeGesture();
            }
        }
        else if (result >= 10 && result < 13 && safeThis->onStepSlotRequested)
        {
            const int currentSlot = safeThis->currentLayer % 3;
            const int targetSlot = result - 10;
            if (targetSlot != currentSlot)
                safeThis->onStepSlotRequested (targetSlot - currentSlot);
        }
    });
}

void SampleBrowserComponent::paint (juce::Graphics& g)
{
    juce::ignoreUnused (g);
}

void SampleBrowserComponent::resized()
{
    auto r = getLocalBounds();
    prevBtn.setBounds (r.removeFromLeft (22));
    r.removeFromLeft (3);
    nextBtn.setBounds (r.removeFromRight (22));
    r.removeFromRight (3);
    selectorBtn.setBounds (r);
}
