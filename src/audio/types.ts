export const NUM_LAYERS = 6;
export const PRE_LAYERS = 3;
export const VOICES_PER_LAYER = 4;
export const MAX_PRE_ROLL_MS = 100.0;
export const DEFAULT_PRE_ROLL_MS = 50.0;
export const PREVIEW_POINTS = 2048;

export const SCOPE_COLS = 1024;
export const FFT_ORDER = 12;
export const FFT_SIZE = 1 << FFT_ORDER;
export const SPECTRUM_BINS = 512;
export const SPECTRUM_HOP = 1024;

export const SIGNAL_COLOURS: readonly string[] = [
  '#56ccf2', // Snapback 1: sky blue
  '#7d92ff', // Snapback 2: periwinkle indigo
  '#a5b4ff', // Snapback 3: ice lavender-blue
  '#cc6bff', // Transient 1: electric orchid
  '#f46ec8', // Transient 2: neon rose-violet
  '#8b5cf6', // Transient 3: deep violet
];

export const CURVE_NAMES: readonly string[] = [
  'Hold',
  'Smooth',
  'Single curve',
  'Half sine',
  'Stairs',
  'Smooth stairs',
  'Pulse',
  'Wave',
];

export const CURVE_SHORT_NAMES: readonly string[] = [
  'Hold',
  'Smooth',
  'Single',
  'H-Sine',
  'Stairs',
  'SmStrs',
  'Pulse',
  'Wave',
];

export const BAR_NAMES: readonly string[] = [
  '1/16',
  '1/8',
  '1/4',
  '1/2',
  '1',
  '2',
  '4',
];

export const BAR_LABELS: readonly string[] = [
  '1/16 bar',
  '1/8 bar',
  '1/4 bar',
  '1/2 bar',
  '1 bar',
  '2 bars',
  '4 bars',
];

export const BARS_TABLE: readonly number[] = [
  1 / 16,
  1 / 8,
  1 / 4,
  1 / 2,
  1,
  2,
  4,
];

export type ScopeViewMode = 0 | 1; // 0 = Waveform, 1 = Spectrum
export type CaptureMode = 0 | 1 | 2; // 0 = Tempo, 1 = Live, 2 = Scroll
export type LayoutMode = 0 | 1; // 0 = Overlay, 1 = Split
export type MeterMode = 0 | 1 | 2; // 0 = Peak, 1 = RMS, 2 = Crest

export interface LayerParameters {
  gain: number;           // 0.0 .. 2.0 (default 1.0)
  pan: number;            // -1.0 .. 1.0 (default 0.0)
  pitchSemitones: number; // -24.0 .. 24.0 (default 0.0)
  delayShiftMs: number;   // -100.0 .. 100.0 (default -50 for PRE, 0 for MAIN)
  highPassHz: number;     // 20.0 .. 1000.0 (default 20.0)
  lowPassHz: number;      // 1000.0 .. 20000.0 (default 20000.0)
  attackMs: number;       // 0.1 .. 2000.0 (default 210.0)
  holdMs: number;         // 0.0 .. 2000.0 (default 0.0)
  decayMs: number;        // 0.1 .. 5000.0 (default 310.0)
  sustain: number;        // 0.0 .. 1.0 (default 0.76)
  releaseMs: number;      // 0.1 .. 10000.0 (default 480.0)
  attackCurve: number;    // 0 .. 7 FL Studio modes (default 2 = Single curve)
  decayCurve: number;     // 0 .. 7
  releaseCurve: number;   // 0 .. 7
  attackTension: number;  // -1.0 .. 1.0 (default 0.35)
  decayTension: number;   // -1.0 .. 1.0 (default -0.35)
  releaseTension: number; // -1.0 .. 1.0 (default -0.45)
  envEnabled: boolean;    // false at open (FL Enable/Disable LED)
  muted: boolean;         // false
  solo: boolean;          // false
  reverse: boolean;       // false
  pogo: number;           // -1.0 .. 1.0 (default 0.0, FL-style pitch bend)
  trimStart: number;      // 0.0 .. 0.99 (normalized sample start)
  trimEnd: number;        // 0.005 .. 1.0 (normalized sample end)
  fadeInMs: number;       // 0.0 .. 2000.0
  fadeOutMs: number;      // 0.0 .. 5000.0
}

export interface WaveformPreview {
  minimum: Float32Array;
  maximum: Float32Array;
  durationMs: number;
  loaded: boolean;
}

export interface LayerState {
  name: string;
  preview: WaveformPreview;
  params: LayerParameters;
}

export interface GlobalMonitorParams {
  view: ScopeViewMode;
  capture: CaptureMode;
  bars: number;
  timeMs: number;
  gainDb: number;
  layout: LayoutMode;
  showSig: boolean[];
  freeze: boolean;
  hold: boolean;
  meter: MeterMode;
}

export interface MonitorSnapshot {
  colMin: Float32Array;
  colMax: Float32Array;
  heldMin: Float32Array;
  heldMax: Float32Array;
  spectrumDb: Float32Array;
  heldSpectrumDb: Float32Array;
  head: number;
  passStart: number;
  filled: number;
  hasData: boolean;
  heldFrom: number;
  heldTo: number;
  heldValid: boolean;
  peakNow: number;
  rmsNow: number;
  crestNow: number;
  peakHold: number;
  rmsHold: number;
  crestHold: number;
}

export function createDefaultLayerParams(layerIndex: number): LayerParameters {
  return {
    gain: 1.0,
    pan: 0.0,
    pitchSemitones: 0.0,
    delayShiftMs: layerIndex < PRE_LAYERS ? -50.0 : 0.0,
    highPassHz: 20.0,
    lowPassHz: 20000.0,
    attackMs: 210.0,
    holdMs: 0.0,
    decayMs: 310.0,
    sustain: 0.76,
    releaseMs: 480.0,
    attackCurve: 2,
    decayCurve: 2,
    releaseCurve: 2,
    attackTension: 0.35,
    decayTension: -0.35,
    releaseTension: -0.45,
    envEnabled: false,
    muted: false,
    solo: false,
    reverse: false,
    pogo: 0.0,
    trimStart: 0.0,
    trimEnd: 1.0,
    fadeInMs: 0.0,
    fadeOutMs: 0.0,
  };
}

export function createEmptyPreview(): WaveformPreview {
  return {
    minimum: new Float32Array(PREVIEW_POINTS),
    maximum: new Float32Array(PREVIEW_POINTS),
    durationMs: 0,
    loaded: false,
  };
}

export function createEmptySnapshot(): MonitorSnapshot {
  const spectrumDb = new Float32Array(NUM_LAYERS * SPECTRUM_BINS).fill(-120);
  const heldSpectrumDb = new Float32Array(NUM_LAYERS * SPECTRUM_BINS).fill(-120);
  return {
    colMin: new Float32Array(NUM_LAYERS * SCOPE_COLS),
    colMax: new Float32Array(NUM_LAYERS * SCOPE_COLS),
    heldMin: new Float32Array(NUM_LAYERS * SCOPE_COLS),
    heldMax: new Float32Array(NUM_LAYERS * SCOPE_COLS),
    spectrumDb,
    heldSpectrumDb,
    head: 0,
    passStart: 0,
    filled: 0,
    hasData: false,
    heldFrom: 0,
    heldTo: 0,
    heldValid: false,
    peakNow: 0,
    rmsNow: 0,
    crestNow: 1,
    peakHold: 0,
    rmsHold: 0,
    crestHold: 1,
  };
}

export function layerSlotLabel(layerIndex: number): string {
  return layerIndex < PRE_LAYERS
    ? `PRE ${layerIndex + 1}`
    : `MAIN ${layerIndex - PRE_LAYERS + 1}`;
}

export function layerShortChip(layerIndex: number): string {
  return layerIndex < PRE_LAYERS
    ? `P${layerIndex + 1}`
    : `M${layerIndex - PRE_LAYERS + 1}`;
}
