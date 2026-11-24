import { clamp } from './curves';

export type TimelineMode = 'grid' | 'seconds';

export interface TimelineTick {
  norm: number;
  label: string;
  major: boolean;
}

export interface LayerBarPlacement {
  startNorm: number;
  endNorm: number;
  spanNorm: number;
  trimmedMs: number;
  playbackDurationMs: number;
  startMsFromTrigger: number;
}

export function barDurationMs(bpm: number): number {
  const safeBpm = clamp(Number.isFinite(bpm) ? bpm : 120, 40, 240);
  // 4/4 bar = 4 quarter notes
  return (4 * 60000) / safeBpm;
}

export function formatTimeTickLabel(ms: number): string {
  const absMs = Math.abs(ms);
  if (absMs < 0.05) return '0.00s';
  if (absMs >= 1000) {
    return `${(ms / 1000).toFixed(2)}s`;
  }
  if (absMs >= 100) {
    return `${(ms / 1000).toFixed(3)}s`;
  }
  if (absMs >= 10) {
    return `${ms.toFixed(1)}ms`;
  }
  return `${ms.toFixed(2)}ms`;
}

/**
 * Computes musical grid lines inside the visible `[viewStartNorm, viewStartNorm + 1/zoom]`
 * span across a 1-Bar (4-beat) master canvas, subdividing from beats to 1/64 notes as zoom increases.
 */
export function computeGridTicks(
  viewStartNorm: number,
  zoom: number
): TimelineTick[] {
  const safeZoom = clamp(Number.isFinite(zoom) ? zoom : 1, 1, 32);
  const span = 1 / safeZoom;
  const viewEnd = Math.min(1, viewStartNorm + span);

  // Choose musical subdivision in 1 bar: 4, 8, 16, 32, or 64
  let divisions = 8;
  let majorStep = 2; // every beat = 2 eighths
  if (safeZoom >= 12) {
    divisions = 64;
    majorStep = 4; // every 1/16 note
  } else if (safeZoom >= 4.5) {
    divisions = 32;
    majorStep = 2; // every 1/16 note
  } else if (safeZoom >= 1.8) {
    divisions = 16;
    majorStep = 4; // every beat
  }

  const ticks: TimelineTick[] = [];
  const firstIdx = Math.max(0, Math.floor(viewStartNorm * divisions));
  const lastIdx = Math.min(divisions, Math.ceil(viewEnd * divisions));

  for (let i = firstIdx; i <= lastIdx; i++) {
    const norm = i / divisions;
    const major = i % majorStep === 0;
    let label = '';
    if (major && i < divisions) {
      const beatFloat = (i / divisions) * 4;
      const beatNum = Math.floor(beatFloat) + 1;
      const sixteenths = Math.round((beatFloat - Math.floor(beatFloat)) * 4) + 1;
      label = divisions >= 32 ? `1.${beatNum}.${sixteenths}` : `1.${beatNum}`;
    } else if (i === divisions) {
      label = '2.1';
    }
    ticks.push({ norm, label, major });
  }
  return ticks;
}

/**
 * Computes human-readable seconds/millisecond ticks across the visible zoom window of a 1-bar canvas.
 */
export function computeSecondsTicks(
  viewStartNorm: number,
  zoom: number,
  bpm: number
): TimelineTick[] {
  const safeZoom = clamp(Number.isFinite(zoom) ? zoom : 1, 1, 32);
  const barMs = barDurationMs(bpm);
  const visibleMs = barMs / safeZoom;

  const candidatesMs = [1000, 500, 250, 100, 50, 25, 10, 5, 2, 1];
  let stepMs = 250;
  for (const c of candidatesMs) {
    const count = visibleMs / c;
    if (count >= 4 && count <= 10) {
      stepMs = c;
      break;
    }
  }
  const minorStepMs = stepMs / 2;

  const startMs = viewStartNorm * barMs;
  const endMs = Math.min(barMs, startMs + visibleMs);
  const firstIdx = Math.max(0, Math.floor(startMs / minorStepMs));
  const lastIdx = Math.min(
    Math.ceil(barMs / minorStepMs),
    Math.ceil(endMs / minorStepMs)
  );

  const ticks: TimelineTick[] = [];
  for (let i = firstIdx; i <= lastIdx; i++) {
    const ms = i * minorStepMs;
    const norm = clamp(ms / barMs, 0, 1);
    const major = i % 2 === 0;
    ticks.push({
      norm,
      label: major ? formatTimeTickLabel(ms) : '',
      major,
    });
  }
  return ticks;
}

/**
 * Computes a layer's trimmed, pitch-adjusted placement on the 1-bar master timeline
 * anchored at `triggerAnchorNorm` (default 0.50 = Beat 1.3, aligned above the TRIGGER tab).
 */
export function computeLayerBarPlacement(
  durationMs: number,
  trimStart: number,
  trimEnd: number,
  pitchSemitones: number,
  delayShiftMs: number,
  bpm: number,
  triggerAnchorNorm = 0.5
): LayerBarPlacement {
  const ts = clamp(trimStart, 0, 0.99);
  const te = clamp(trimEnd, ts + 0.005, 1);
  const trimmedMs = Math.max(1, durationMs * (te - ts));
  const pitchFactor = Math.pow(2, clamp(pitchSemitones, -24, 24) / 12);
  const playbackDurationMs = Math.max(1, trimmedMs / Math.max(0.1, pitchFactor));
  const barMs = barDurationMs(bpm);
  const startMsFromTrigger = clamp(delayShiftMs, -100, 100);
  const startNorm = triggerAnchorNorm + startMsFromTrigger / barMs;
  const spanNorm = playbackDurationMs / barMs;
  return {
    startNorm,
    endNorm: startNorm + spanNorm,
    spanNorm,
    trimmedMs,
    playbackDurationMs,
    startMsFromTrigger,
  };
}
