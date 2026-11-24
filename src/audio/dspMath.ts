import { clamp } from './curves';

export interface TrimBounds {
  trimStartIdx: number;
  trimEndIdx: number;
  trimmedLength: number;
}

export function computeTrimBounds(
  length: number,
  trimStart: number,
  trimEnd: number
): TrimBounds {
  if (length <= 1) {
    return { trimStartIdx: 0, trimEndIdx: Math.max(0, length), trimmedLength: 0 };
  }
  const ts = clamp(trimStart, 0, 0.99);
  const te = clamp(trimEnd, ts + 0.005, 1);
  const trimStartIdx = clamp(Math.floor(ts * length), 0, length - 2);
  const trimEndIdx = clamp(Math.ceil(te * length), trimStartIdx + 1, length);
  return {
    trimStartIdx,
    trimEndIdx,
    trimmedLength: trimEndIdx - trimStartIdx,
  };
}

export interface EnvelopeSchedule {
  attackEndMs: number;
  holdEndMs: number;
  decayEndMs: number;
  releaseStartMs: number;
  releaseMs: number;
  totalDurationMs: number;
}

/**
 * Maps AHDSR stage times onto a sample/window of `totalDurationMs`.
 * When `attack + hold + decay + release <= totalDurationMs`, times are used 1:1
 * and any extra middle span holds at sustain. When `totalDurationMs` is shorter
 * than the stage sum, stages scale proportionally so D, S, and R never collide.
 */
export function computeEnvelopeSchedule(
  attackMs: number,
  holdMs: number,
  decayMs: number,
  releaseMs: number,
  totalDurationMs: number
): EnvelopeSchedule {
  const dur = Math.max(1, totalDurationMs);
  const a = Math.max(0.1, attackMs);
  const h = Math.max(0, holdMs);
  const d = Math.max(0.1, decayMs);
  const r = Math.max(0.1, releaseMs);
  const sum = a + h + d + r;
  const scale = sum > dur ? dur / sum : 1.0;

  const effA = a * scale;
  const effH = h * scale;
  const effD = d * scale;
  const effR = r * scale;

  const attackEndMs = effA;
  const holdEndMs = effA + effH;
  const decayEndMs = effA + effH + effD;
  const releaseStartMs = Math.max(decayEndMs, dur - effR);
  return {
    attackEndMs,
    holdEndMs,
    decayEndMs,
    releaseStartMs,
    releaseMs: Math.max(0.1, dur - releaseStartMs),
    totalDurationMs: dur,
  };
}

export function computePogoSemitones(
  pogo: number,
  position: number,
  trimmedLength: number
): number {
  if (trimmedLength <= 0) return 0;
  const u = clamp(position / trimmedLength, 0, 1);
  return clamp(pogo, -1, 1) * 24.0 * Math.pow(1 - u, 2.5);
}

export function computeSampleReadPosition(
  trimStartIdx: number,
  trimEndIdx: number,
  position: number,
  reverse: boolean
): number {
  return reverse ? trimEndIdx - 1 - position : trimStartIdx + position;
}

export function computeFadeGains(
  elapsedSamples: number,
  remainingSamples: number,
  fadeInMs: number,
  fadeOutMs: number,
  sampleRate: number
): { fadeInGain: number; fadeOutGain: number; combinedGain: number } {
  const fadeInSamples = Math.max(0, fadeInMs) * 0.001 * sampleRate;
  const fadeOutSamples = Math.max(0, fadeOutMs) * 0.001 * sampleRate;
  const fadeInGain =
    fadeInSamples > 0.5 ? clamp(elapsedSamples / fadeInSamples, 0, 1) : 1;
  const fadeOutGain =
    fadeOutSamples > 0.5 ? clamp(remainingSamples / fadeOutSamples, 0, 1) : 1;
  return {
    fadeInGain,
    fadeOutGain,
    combinedGain: fadeInGain * fadeOutGain,
  };
}
