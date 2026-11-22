#pragma once

#include "PluginProcessor.h"

/**
    Compact, editable per-layer ADSR preview.

    Drag A and D horizontally to change stage durations, S vertically to change
    sustain level, and R horizontally to change the release duration. The three
    curve selectors are independently automatable APVTS parameters.
*/
class LayerEnvelopeComponent final : public juce::Component, private juce::Timer
{
public:
    LayerEnvelopeComponent (WaveLensProcessor&, int layerIndex, juce::Colour traceColour);
    ~LayerEnvelopeComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class Handle { None, Attack, Decay, Sustain, Release };

    struct Geometry
    {
        juce::Rectangle<float> plot;
        juce::Point<float> attack;
        juce::Point<float> decay;
        juce::Point<float> sustain;
        juce::Point<float> release;
    };

    void timerCallback() override { repaint(); }
    Geometry getGeometry() const;
    void applyDrag (juce::Point<float> position);
    void beginGesture (Handle);
    void endGesture();
    void setParameter (const juce::String& parameterId, float value);
    static float curveValue (float progress, int curve) noexcept;

    WaveLensProcessor& processor;
    const int layer;
    const juce::Colour colour;

    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pSustain = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pAttackCurve = nullptr;
    std::atomic<float>* pDecayCurve = nullptr;
    std::atomic<float>* pReleaseCurve = nullptr;

    juce::ComboBox attackCurveBox, decayCurveBox, releaseCurveBox;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ComboAtt> attackCurveAttachment, decayCurveAttachment, releaseCurveAttachment;

    Handle activeHandle = Handle::None;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerEnvelopeComponent)
};
