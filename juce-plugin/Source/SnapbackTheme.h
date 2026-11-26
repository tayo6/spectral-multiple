#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Single source of truth for the WAVESTACK dark colour theme taken from
    the supplied "wavestack-ui-coloured" SVG.

      - plugin background  #110e16, panels #211f28, cards #17151d
      - sample box         #131118 fill, #282632 stroke
      - tracks             #0d0b10 2.2 px round-cap, accent active span
      - thumbs             #e6e4ed r=3
      - SNAPBACK family    #8b9cf7 title / #8597ff trace / #6d7ef2 envelope
      - TRANSIENT family   #aa7ef7 title / #be8aff trace / #985ee6 envelope
      - labels #767185, values #dcd9e6, icons #9b96ab

    wavelens::ui (SpectralMultipleLookAndFeel.h) aliases these values, so all
    components share one palette definition.
*/
namespace snap
{
    // ---- Surfaces --------------------------------------------------------------
    inline const juce::Colour bg        { 0xff110e16 }; // plugin background
    inline const juce::Colour panel     { 0xff211f28 }; // layer panel background
    inline const juce::Colour panelHi   { 0xff18161e }; // toolbar background
    inline const juce::Colour inset     { 0xff131118 }; // sample box / M-S squares
    inline const juce::Colour border    { 0xff282632 }; // sample box frame
    inline const juce::Colour borderSoft{ 0xff2c2a38 }; // pad card stroke

    // ---- Parameter tiles ---------------------------------------------------------
    inline const juce::Colour tile      { 0xff17151d }; // rounded parameter card
    inline const juce::Colour tileTrack { 0xff0d0b10 }; // 2.2 px round-cap track
    inline const juce::Colour tileLabel { 0xff767185 }; // small grey label
    inline const juce::Colour thumb     { 0xffe6e4ed }; // slider / trim thumb

    // ---- Text ----------------------------------------------------------------------
    inline const juce::Colour text      { 0xffdcd9e6 }; // primary value white
    inline const juce::Colour muted     { 0xff767185 }; // secondary grey label
    inline const juce::Colour disabled  { 0xff565261 }; // disabled text
    inline const juce::Colour onAccent  { 0xff131118 }; // dark text on accent fills

    // ---- Bank accents -----------------------------------------------------------------
    inline const juce::Colour snap      { 0xff6d7ef2 }; // SNAPBACK envelope / active
    inline const juce::Colour snapHi    { 0xff8b9cf7 }; // SNAPBACK title / power
    inline const juce::Colour snapTrace { 0xff8597ff }; // SNAPBACK waveform trace
    inline const juce::Colour tr        { 0xff985ee6 }; // TRANSIENT envelope / active
    inline const juce::Colour trHi      { 0xffaa7ef7 }; // TRANSIENT title / power
    inline const juce::Colour trTrace   { 0xffbe8aff }; // TRANSIENT waveform trace

    // ---- Waveform / meters --------------------------------------------------------------
    inline const juce::Colour wave      { 0xff9d9ca8 }; // dry input trace
    inline const juce::Colour waveFill  { 0xff8b8a96 }; // dry input fill (34%)
    inline const juce::Colour grid      { 0xff363240 }; // centre baseline
    inline const juce::Colour meterCool { 0xff6d7ef2 };
    inline const juce::Colour meterMid  { 0xff8b8699 };
    inline const juce::Colour meterHot  { 0xffbe8aff };
    inline const juce::Colour warn      { 0xffe8b64c }; // missing-sample amber

    // ---- FL envelope handles -------------------------------------------------------------
    inline const juce::Colour flPoint   { 0xfff2c14e }; // FL ADSR point handle (gold)
    inline const juce::Colour flOutline { 0xff131118 }; // dark ring around points

    /** Individual signal colour of layer n (0..5): blue family vs violet family. */
    inline juce::Colour signalColour (int n) noexcept
    {
        static const juce::uint32 table[] = { 0xff8597ff,   // SNAPBACK 1
                                              0xff8597ff,   // SNAPBACK 2
                                              0xff8597ff,   // SNAPBACK 3
                                              0xffbe8aff,   // TRANSIENT 1
                                              0xffbe8aff,   // TRANSIENT 2
                                              0xffbe8aff }; // TRANSIENT 3
        return juce::Colour (table[(size_t) juce::jlimit (0, 5, n)]);
    }

    /** Bank accent of a SNAPBACK (0..2) or TRANSIENT (3..5) layer. */
    inline juce::Colour bankAccent (int layer) noexcept    { return layer < 3 ? snap : tr; }

    /** Bright bank accent of a SNAPBACK (0..2) or TRANSIENT (3..5) layer. */
    inline juce::Colour bankAccentHi (int layer) noexcept  { return layer < 3 ? snapHi : trHi; }

    /** Waveform trace colour of a SNAPBACK (0..2) or TRANSIENT (3..5) layer. */
    inline juce::Colour bankTrace (int layer) noexcept     { return layer < 3 ? snapTrace : trTrace; }

    /** Icon stroke colour (SVG class "ui-icon"). */
    inline const juce::Colour icon      { 0xff9b96ab };

    /**
        Symmetric trapezoid used for the COLLAPSE tab: SVG points
        "338,370 363,330 437,330 462,370" - wide bottom on the toolbar's
        bottom edge, narrow top pointing up into the visualizer.
    */
    inline juce::Path trapezoidPath (juce::Rectangle<float> b, float insetWidth) noexcept
    {
        juce::Path p;
        p.startNewSubPath (b.getX() + insetWidth, b.getY());
        p.lineTo (b.getRight() - insetWidth, b.getY());
        p.lineTo (b.getRight(), b.getBottom());
        p.lineTo (b.getX(), b.getBottom());
        p.closeSubPath();
        return p;
    }
}