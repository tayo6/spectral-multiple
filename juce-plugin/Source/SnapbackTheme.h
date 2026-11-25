#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Single source of truth for the SNAPBACK flat near-black palette.
    SNAPBACK = periwinkle blue family, TRANSIENT = violet family.

    wavelens::ui (SpectralMultipleLookAndFeel.h) aliases these values, so all
    components share one palette definition.
*/
namespace snap
{
    // ---- Surfaces (flat, no gradients) ---------------------------------------
    inline const juce::Colour bg        { 0xff0e0e12 }; // main background
    inline const juce::Colour panel     { 0xff141419 }; // panel background
    inline const juce::Colour panelHi   { 0xff191921 }; // elevated panel / toolbar
    inline const juce::Colour inset     { 0xff111116 }; // rulers, readouts, dividers
    inline const juce::Colour border    { 0xff27272f }; // panel outlines
    inline const juce::Colour borderSoft{ 0xff1f1f27 }; // subtle separators

    // ---- Parameter tiles ------------------------------------------------------
    inline const juce::Colour tile      { 0xff1b1b22 }; // rounded parameter tile
    inline const juce::Colour tileTrack { 0xff2b2b34 }; // thin track inside tiles
    inline const juce::Colour tileLabel { 0xff8b8f9c }; // small grey label

    // ---- Text ------------------------------------------------------------------
    inline const juce::Colour text      { 0xffeceef2 }; // primary white
    inline const juce::Colour muted     { 0xff8f94a3 }; // secondary grey
    inline const juce::Colour disabled  { 0xff55555f }; // disabled text
    inline const juce::Colour onAccent  { 0xff0f1016 }; // dark text on accent fills

    // ---- Bank accents -----------------------------------------------------------
    inline const juce::Colour snap      { 0xff5b7ff0 }; // SNAPBACK periwinkle blue
    inline const juce::Colour snapHi    { 0xff8ca6ff }; // bright SNAPBACK
    inline const juce::Colour tr        { 0xff9b6df0 }; // TRANSIENT violet
    inline const juce::Colour trHi      { 0xffb795ff }; // bright TRANSIENT

    // ---- Waveform / meters --------------------------------------------------------
    inline const juce::Colour wave      { 0xff97a0b4 }; // neutral grey-blue waveform
    inline const juce::Colour grid      { 0xff8a94b4 }; // subtle blue-grey grid
    inline const juce::Colour meterCool { 0xff5b7ff0 };
    inline const juce::Colour meterMid  { 0xffe8b64c };
    inline const juce::Colour meterHot  { 0xffe0564a };
    inline const juce::Colour warn      { 0xffe8b64c }; // missing-sample amber

    /** Individual signal colour of layer n (0..5): blue family vs violet family. */
    inline juce::Colour signalColour (int n) noexcept
    {
        static const juce::uint32 table[] = { 0xff5b7ff0,   // SNAPBACK 1
                                              0xff7d92f3,   // SNAPBACK 2
                                              0xffa3b3f7,   // SNAPBACK 3
                                              0xff9b6df0,   // TRANSIENT 1
                                              0xffb18bf5,   // TRANSIENT 2
                                              0xffc9b0fa }; // TRANSIENT 3
        return juce::Colour (table[(size_t) juce::jlimit (0, 5, n)]);
    }

    /** Bank accent of a SNAPBACK (0..2) or TRANSIENT (3..5) layer. */
    inline juce::Colour bankAccent (int layer) noexcept    { return layer < 3 ? snap : tr; }

    /** Bright bank accent of a SNAPBACK (0..2) or TRANSIENT (3..5) layer. */
    inline juce::Colour bankAccentHi (int layer) noexcept  { return layer < 3 ? snapHi : trHi; }

    /** Symmetric trapezoid used for the TRIGGER tab (wide top, narrow bottom). */
    inline juce::Path trapezoidPath (juce::Rectangle<float> b, float insetWidth) noexcept
    {
        juce::Path p;
        p.startNewSubPath (b.getX(), b.getY());
        p.lineTo (b.getRight(), b.getY());
        p.lineTo (b.getRight() - insetWidth, b.getBottom());
        p.lineTo (b.getX() + insetWidth, b.getBottom());
        p.closeSubPath();
        return p;
    }
}