#pragma once

#include "PluginProcessor.h"
#include "SpectralMultipleLookAndFeel.h"
#include "SampleBrowserComponent.h"
#include "LayerEnvelopeComponent.h"
#include <array>
#include <functional>
#include <memory>

/**
    One of the two primary sample sections in Spectral Multiple:
      - SNAPBACK  (groupIndex = 0, layers 0..2)
      - TRANSIENT (groupIndex = 1, layers 3..5)

    Displays the uppercase heading followed immediately by three coloured circular
    SampleSelectorDots and shows one active LayerEnvelopeComponent editor at a time.
*/
class SampleGroupComponent final : public juce::Component,
                                   public juce::FileDragAndDropTarget
{
public:
    SampleGroupComponent (WaveLensProcessor& processor, int groupIndex);
    ~SampleGroupComponent() override = default;

    int getSelectedSlot() const noexcept { return selectedSlot; }
    int getSelectedLayer() const noexcept { return baseLayer + selectedSlot; }
    void selectSlot (int slotIndex);
    void refreshState();

    std::function<void (int selectedLayer)> onSelectionChanged;
    std::function<void()> onSampleStateChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int, int) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    class BankSoloButton final : public juce::Button
    {
    public:
        BankSoloButton() : juce::Button ("BankSolo")
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        void setAccentColour (juce::Colour colour) noexcept { accent = colour; repaint(); }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    private:
        juce::Colour accent { wavelens::ui::kPurpleAccent };
    };

    class BankPowerButton final : public juce::Button
    {
    public:
        BankPowerButton() : juce::Button ("BankPower")
        {
            setTooltip ("Enable or mute all three samples in this bank");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        void setAccentColour (juce::Colour colour) noexcept { accent = colour; repaint(); }
        void setBankEnabled (bool enabled) noexcept { if (bankEnabled != enabled) { bankEnabled = enabled; repaint(); } }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    private:
        juce::Colour accent { wavelens::ui::kPurpleAccent };
        bool bankEnabled = true;
    };

    void promptLoadActiveLayer();
    void loadFileIntoActiveLayer (const juce::File& file);

    WaveLensProcessor& proc;
    const int group;
    const int baseLayer;
    int selectedSlot = 0;
    bool dragOver = false;

    BankPowerButton bankPowerBtn;
    BankSoloButton soloBtn;
    juce::TextButton auditionBtn { ">" };
    SampleBrowserComponent sampleBrowser;
    juce::TextButton clearBtn { "x" };

    std::array<std::unique_ptr<LayerEnvelopeComponent>, 3> editors;

    juce::Rectangle<int> headerBounds;
    juce::Rectangle<int> editorBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleGroupComponent)
};
