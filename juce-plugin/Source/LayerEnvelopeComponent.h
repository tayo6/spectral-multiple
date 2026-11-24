#pragma once

#include "PluginProcessor.h"
#include "SpectralMultipleLookAndFeel.h"
#include "FilterControlComponent.h"

/**
    Waveform-backed sample editor, FL-style tension envelope, and Snapback-inspired
    parameter deck for one individual SNAPBACK (0..2) or TRANSIENT (3..5) layer.
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
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Handle
    {
        None,
        TrimStart,
        TrimEnd,
        Attack,
        Hold,
        Decay,
        Sustain,
        Release,
        TensionAttack,
        TensionDecay,
        TensionRelease,
        Pan,
        OverviewPan
    };

    enum class Segment { Attack, Decay, Release };

    struct Geometry
    {
        juce::Rectangle<float> plot;
        float trimX0 = 0.0f;
        float trimX1 = 0.0f;
        float trimW = 1.0f;
        float trimmedDurationMs = 1000.0f;
        float attackEndX = 0.0f;
        float holdEndX = 0.0f;
        float releaseStartX = 0.0f;
        juce::Point<float> attack, hold, decay, sustain, release;
        juce::Point<float> tensionAttack, tensionDecay, tensionRelease;
        bool tensionAttackVisible = false;
        bool tensionDecayVisible = false;
        bool tensionReleaseVisible = false;
    };

    void timerCallback() override { repaint(); }
    void clampViewport (float nextViewStart, float nextZoom) noexcept;
    void zoomAroundNormalized (float anchorNorm, float factor) noexcept;
    void focusViewportAt (float targetNorm, float defaultZoomIfUnzoomed = 4.0f) noexcept;
    void zoomToFitTrim() noexcept;
    float normToPlotX (float norm, const juce::Rectangle<float>& plot) const noexcept;
    float plotXToNorm (float x, const juce::Rectangle<float>& plot) const noexcept;
    void updateZoomReadout();

    Geometry getGeometry (float totalDurationMs) const;
    Handle hitTestHandle (juce::Point<float> pos, const Geometry&, float maxRadius = 15.0f) const noexcept;
    void resolveCircleForContext (juce::Point<float> pos,
                                  const Geometry& g,
                                  Segment& outSegment,
                                  juce::Point<float>& outCirclePos,
                                  juce::String& outTitle) const noexcept;
    void showFlCurveMenu (Segment segment,
                          juce::Point<float> circleLocalPos,
                          const juce::String& title);
    void applyDrag (juce::Point<float> position);
    void beginGesture (Handle);
    void endGesture();
    void setParameter (const juce::String& parameterId, float value);
    LayeredSampleEngine::LayerParameters readLayerParameters() const noexcept;

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
    std::atomic<float>* pEnvEnabled = nullptr;
    std::atomic<float>* pReverse = nullptr;
    std::atomic<float>* pPitch = nullptr;
    std::atomic<float>* pTrimStart = nullptr;
    std::atomic<float>* pTrimEnd = nullptr;
    std::atomic<float>* pFadeIn = nullptr;
    std::atomic<float>* pFadeOut = nullptr;

    juce::TextButton reverseBtn, envEnableBtn;
    juce::TextButton focusStartBtn, focusEndBtn, zoomFitBtn, zoomOutBtn, zoomResetBtn, zoomInBtn;
    juce::ComboBox attackCurveBox, decayCurveBox, releaseCurveBox;
    juce::Slider gainSlider, delaySlider, pitchSlider, panSlider, pogoSlider;
    juce::Slider fadeInSlider, fadeOutSlider, trimStartSlider, trimEndSlider;
    FilterControlComponent filterBar;

    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<ButtonAtt> reverseAttachment, envEnableAttachment;
    std::unique_ptr<ComboAtt> attackCurveAttachment, decayCurveAttachment, releaseCurveAttachment;
    std::unique_ptr<SliderAtt> gainAttachment, delayAttachment, pitchAttachment, panAttachment, pogoAttachment;
    std::unique_ptr<SliderAtt> fadeInAttachment, fadeOutAttachment, trimStartAttachment, trimEndAttachment;

    juce::Rectangle<int> flBarBounds;
    juce::Rectangle<int> controlStripBounds;
    juce::Rectangle<float> overviewBarBounds;
    Handle activeHandle = Handle::None;
    float zoomFactor = 1.0f;
    float viewStartNorm = 0.0f;
    float dragStartX = 0.0f;
    float dragStartY = 0.0f;
    float dragStartTension = 0.0f;
    float dragStartViewStart = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerEnvelopeComponent)
};