#pragma once

#include "PluginProcessor.h"

#include <vector>

/**
    FL Studio-inspired, waveform-backed envelope editor.

    The editor keeps the familiar ADSR anchors but adds per-section curve modes,
    draggable tension handles, point context menus, undo, snapping and presets.
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
    enum class Handle
    {
        None, Attack, Hold, Decay, Sustain, Release,
        AttackTension, DecayTension, ReleaseTension
    };

    enum Stage { AttackStage = 0, DecayStage, ReleaseStage };

    struct Snapshot
    {
        float attack = 2.0f, hold = 20.0f, decay = 60.0f, sustain = 1.0f, release = 120.0f;
        int attackCurve = 4, decayCurve = 4, releaseCurve = 4;
        float attackTension = 0.0f, decayTension = 0.0f, releaseTension = 0.0f;
        bool enabled = true;
    };

    struct Geometry
    {
        juce::Rectangle<float> plot;
        juce::Point<float> attack, hold, decay, sustain, release;
        juce::Point<float> attackTension, decayTension, releaseTension;
        float durationMs = 1000.0f;
        float attackEndMs = 2.0f, holdEndMs = 22.0f, decayEndMs = 82.0f, releaseStartMs = 880.0f;
        float sustainLevel = 1.0f;
    };

    void timerCallback() override;
    Geometry getGeometry (float durationMs) const;
    void drawEnvelopeCurve (juce::Graphics&, const Geometry&) const;
    void drawPoint (juce::Graphics&, juce::Point<float>, const juce::String&, bool selected) const;
    void applyDrag (juce::Point<float> position);
    void beginGesture (Handle);
    void endGesture();
    void setParameter (const juce::String& parameterId, float value);
    void setCurveForStage (int stage, int curveIndex);
    juce::String curveParameterId (int stage) const;
    juce::String tensionParameterId (int stage) const;
    int curveForStage (int stage) const;
    float tensionForStage (int stage) const;
    int stageForHandle (Handle) const;
    Handle nearestHandle (juce::Point<float>, float maxDistance) const;
    Snapshot captureSnapshot() const;
    void restoreSnapshot (const Snapshot&);
    void pushUndo();
    void undo();
    void showPointMenu (int stage);
    void showEnvelopeMenu();
    void savePreset();
    void loadPreset();
    float curveValue (float progress, int curve, float tension) const noexcept;
    float timeFromX (float x, const Geometry&) const noexcept;
    float snapTime (float milliseconds, const Geometry&) const noexcept;

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
    std::atomic<float>* pAttackTension = nullptr;
    std::atomic<float>* pDecayTension = nullptr;
    std::atomic<float>* pReleaseTension = nullptr;
    std::atomic<float>* pEnvelopeEnabled = nullptr;

    juce::Slider panSlider, pitchSlider, highPassSlider, lowPassSlider;
    juce::TextButton muteButton, enableButton, undoButton, freezeButton, drawButton;
    juce::TextButton snapButton, slideButton, envelopeMenuButton;

    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAtt> panAttachment, pitchAttachment, highPassAttachment, lowPassAttachment;
    std::unique_ptr<ButtonAtt> muteAttachment, envelopeEnabledAttachment;

    std::vector<Snapshot> undoStack;
    Handle activeHandle = Handle::None;
    int selectedStage = AttackStage;
    bool frozen = false;
    bool drawMode = false;
    bool snapEnabled = true;
    bool slidePoints = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerEnvelopeComponent)
};
