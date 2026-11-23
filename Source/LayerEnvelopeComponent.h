#pragma once

#include "PluginProcessor.h"

/** Per-layer sample waveform with a draggable AHDSR envelope overlaid on top. */
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
    enum class Handle { None, Attack, Hold, Decay, Sustain, Release };

    struct Geometry
    {
        juce::Rectangle<float> plot;
        juce::Point<float> attack, hold, decay, sustain, release;
        float durationMs = 1000.0f;
    };

    void timerCallback() override { repaint(); }
    Geometry getGeometry (float durationMs) const;
    void applyDrag (juce::Point<float> position);
    void beginGesture (Handle);
    void endGesture();
    void setParameter (const juce::String& parameterId, float value);
    static float curveValue (float progress, int curve) noexcept;

    WaveLensProcessor& processor;
    const int layer;
    const juce::Colour colour;

    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pHold = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pSustain = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pAttackCurve = nullptr;
    std::atomic<float>* pDecayCurve = nullptr;
    std::atomic<float>* pReleaseCurve = nullptr;

    juce::ComboBox attackCurveBox, decayCurveBox, releaseCurveBox;
    juce::Slider panSlider, pitchSlider, highPassSlider, lowPassSlider;
    juce::TextButton muteButton;

    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ComboAtt> attackCurveAttachment, decayCurveAttachment, releaseCurveAttachment;
    std::unique_ptr<SliderAtt> panAttachment, pitchAttachment, highPassAttachment, lowPassAttachment;
    std::unique_ptr<ButtonAtt> muteAttachment;

    Handle activeHandle = Handle::None;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerEnvelopeComponent)
};
