export const NUM_LAYERS = 6;
export const PRE_LAYERS = 3;
export const VOICES_PER_LAYER = 4;
export const MAX_PRE_ROLL_MS = 100.0;
export const DEFAULT_PRE_ROLL_MS = 50.0;
export const PREVIEW_POINTS = 2048;

export const SIGNAL_COLORS = [
  '#8b7cff', // SNAPBACK 1: violet
  '#a78bfa', // SNAPBACK 2: light violet
  '#c084fc', // SNAPBACK 3: lavender
  '#c06cff', // TRANSIENT 1: magenta-violet
  '#d58cff', // TRANSIENT 2: light magenta
  '#9b8cff', // TRANSIENT 3: periwinkle
] as const;

export const FL_CURVE_NAMES = [
  'Hold',
  'Smooth',
  'Single curve',
  'Half sine',
  'Stairs',
  'Smooth stairs',
  'Pulse',
  'Wave',
] as const;

export type FlCurveMode = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7;

export interface LayerParameters {
  gain: number;           // 0..2
  pan: number;            // -1..1
  pitchSemitones: number; // -24..24
  delayShiftMs: number;   // -100..100
  highPassHz: number;     // 20..1000
  lowPassHz: number;      // 1000..20000
  attackMs: number;       // 0.1..2000
  holdMs: number;         // 0..2000
  decayMs: number;        // 0.1..5000
  sustain: number;        // 0..1
  releaseMs: number;      // 0.1..10000
  attackCurve: number;    // 0..7
  decayCurve: number;     // 0..7
  releaseCurve: number;   // 0..7
  attackTension: number;  // -1..1
  decayTension: number;   // -1..1
  releaseTension: number; // -1..1
  envEnabled: boolean;
  muted: boolean;
  solo: boolean;
  reverse: boolean;
  pogo: number;           // -1..1
  trimStart: number;      // 0..0.99
  trimEnd: number;        // 0.005..1
  fadeInMs: number;       // 0..2000
  fadeOutMs: number;      // 0..5000
}

export function createDefaultLayerParameters(): LayerParameters {
  return {
    gain: 1.0,
    pan: 0.0,
    pitchSemitones: 0.0,
    delayShiftMs: 0.0,
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

export interface WaveformPreview {
  minimum: Float32Array;
  maximum: Float32Array;
  durationMs: number;
  loaded: boolean;
}

export interface LayerSampleInfo {
  loaded: boolean;
  displayName: string;
  durationMs: number;
  sampleRate: number;
  preview: WaveformPreview;
  buffer: AudioBuffer | null;
}

export interface EnvelopeSchedule {
  attackEndMs: number;
  holdEndMs: number;
  decayEndMs: number;
  releaseStartMs: number;
  releaseMs: number;
}

export type MonitorViewMode = 'waveform' | 'spectrum';
export type MonitorLayoutMode = 'overlay' | 'split';
export type MeterMode = 'PEAK' | 'RMS' | 'CREST';

export interface GlobalState {
  bpm: number;
  preRollMs: number;
  gridMode: boolean;
  monitorMode: boolean;
  viewMode: MonitorViewMode;
  layoutMode: MonitorLayoutMode;
  freeze: boolean;
  hold: boolean;
  meterMode: MeterMode;
  showSig: [boolean, boolean, boolean, boolean, boolean, boolean];
  selectedSnapback: number;  // 0..2
  selectedTransient: number; // 3..5
  triggerMode: 'audio' | 'midi' | 'both';
  threshold: number;
}

export function createDefaultGlobalState(): GlobalState {
  return {
    bpm: 120,
    preRollMs: DEFAULT_PRE_ROLL_MS,
    gridMode: true,
    monitorMode: false,
    viewMode: 'waveform',
    layoutMode: 'overlay',
    freeze: false,
    hold: false,
    meterMode: 'PEAK',
    showSig: [true, true, true, true, true, true],
    selectedSnapback: 0,
    selectedTransient: 3,
    triggerMode: 'both',
    threshold: 0.03,
  };
}
