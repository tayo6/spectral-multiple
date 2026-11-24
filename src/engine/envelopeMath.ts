import {
  EnvelopeSchedule,
  LayerParameters,
  PREVIEW_POINTS,
  WaveformPreview,
} from './types';

export function clamp(val: number, min: number, max: number): number {
  return val < min ? min : val > max ? max : val;
}

export function applyPowerTension(u: number, tension: number): number {
  const t = clamp(u, 0, 1);
  const ten = clamp(tension, -1, 1);
  if (Math.abs(ten) < 1.0e-4) return t;
  const expVal = Math.pow(2.0, Math.abs(ten) * 3.0);
  return ten >= 0 ? 1.0 - Math.pow(1.0 - t, expVal) : Math.pow(t, expVal);
}

export function flSegmentLevel(
  progress: number,
  y0: number,
  y1: number,
  mode: number,
  tension: number
): number {
  const t = clamp(progress, 0, 1);
  const ten = clamp(tension, -1, 1);
  const m = clamp(Math.round(mode), 0, 7);
  const dirTen = y1 >= y0 ? ten : -ten;

  let frac = t;
  switch (m) {
    case 0: // Hold
      frac = t >= 1.0 ? 1.0 : 0.0;
      break;
    case 1: { // Smooth
      const s = applyPowerTension(t, dirTen);
      frac = s * s * (3.0 - 2.0 * s);
      break;
    }
    case 2: // Single curve
      frac = applyPowerTension(t, dirTen);
      break;
    case 3: { // Half sine
      const hs = 0.5 - 0.5 * Math.cos(Math.PI * t);
      frac = applyPowerTension(hs, dirTen);
      break;
    }
    case 4: { // Stairs
      const steps = Math.max(2.0, Math.round(2.0 + (ten + 1.0) * 6.0));
      frac = t >= 1.0 ? 1.0 : Math.floor(t * steps) / steps;
      break;
    }
    case 5: { // Smooth stairs
      const steps = Math.max(2.0, Math.round(2.0 + (ten + 1.0) * 6.0));
      if (t >= 1.0) {
        frac = 1.0;
      } else {
        const scaled = t * steps;
        const idx = Math.floor(scaled);
        const f = scaled - idx;
        const smoothF = f * f * (3.0 - 2.0 * f);
        frac = (idx + smoothF) / steps;
      }
      break;
    }
    case 6: { // Pulse
      const cycles = Math.max(1.0, Math.round(1.0 + (ten + 1.0) * 5.0));
      if (t >= 1.0) {
        frac = 1.0;
      } else {
        const phase = (t * cycles) % 1.0;
        frac = phase < 0.5 ? 0.0 : 1.0;
      }
      break;
    }
    case 7: { // Wave
      const cycles = Math.max(1.0, Math.round(1.0 + (ten + 1.0) * 5.0));
      frac = 0.5 * (1.0 - Math.cos(Math.PI * (2.0 * cycles + 1.0) * t));
      break;
    }
    default:
      frac = applyPowerTension(t, dirTen);
      break;
  }

  return y0 + (y1 - y0) * frac;
}

export function computeEnvelopeSchedule(
  p: LayerParameters,
  totalDurationMs: number
): EnvelopeSchedule {
  const dur = Math.max(1.0, totalDurationMs);
  const a = Math.max(0.1, p.attackMs);
  const h = Math.max(0.0, p.holdMs);
  const d = Math.max(0.1, p.decayMs);
  const r = Math.max(0.1, p.releaseMs);
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
  const releaseMs = Math.max(0.1, dur - releaseStartMs);

  return {
    attackEndMs,
    holdEndMs,
    decayEndMs,
    releaseStartMs,
    releaseMs,
  };
}

export function envelopeLevelAtMs(
  p: LayerParameters,
  sched: EnvelopeSchedule,
  ageMs: number
): number {
  if (!p.envEnabled) return 1.0;

  const attSpan = Math.max(0.05, sched.attackEndMs);
  if (ageMs < sched.attackEndMs) {
    return flSegmentLevel(ageMs / attSpan, 0.0, 1.0, p.attackCurve, p.attackTension);
  }

  if (ageMs < sched.holdEndMs) {
    return 1.0;
  }

  const decSpan = Math.max(0.05, sched.decayEndMs - sched.holdEndMs);
  if (ageMs < sched.decayEndMs) {
    return flSegmentLevel(
      (ageMs - sched.holdEndMs) / decSpan,
      1.0,
      p.sustain,
      p.decayCurve,
      p.decayTension
    );
  }

  return p.sustain;
}

export function evaluateCompleteEnvelopeAtMs(
  p: LayerParameters,
  sched: EnvelopeSchedule,
  ageMs: number
): number {
  if (!p.envEnabled) return 1.0;

  if (ageMs <= sched.releaseStartMs) {
    return envelopeLevelAtMs(p, sched, ageMs);
  }

  const releaseStartLevel = envelopeLevelAtMs(p, sched, sched.releaseStartMs);
  const releaseProgress = clamp(
    (ageMs - sched.releaseStartMs) / Math.max(0.1, sched.releaseMs),
    0.0,
    1.0
  );
  return flSegmentLevel(
    releaseProgress,
    releaseStartLevel,
    0.0,
    p.releaseCurve,
    p.releaseTension
  );
}

export function createEmptyPreview(): WaveformPreview {
  return {
    minimum: new Float32Array(PREVIEW_POINTS),
    maximum: new Float32Array(PREVIEW_POINTS),
    durationMs: 0,
    loaded: false,
  };
}

export function computeWaveformPreview(audioBuffer: AudioBuffer): WaveformPreview {
  const sampleCount = audioBuffer.length;
  const numChannels = Math.min(2, audioBuffer.numberOfChannels);
  const minimum = new Float32Array(PREVIEW_POINTS);
  const maximum = new Float32Array(PREVIEW_POINTS);

  if (sampleCount <= 0 || numChannels <= 0) {
    return createEmptyPreview();
  }

  const ch0 = audioBuffer.getChannelData(0);
  const ch1 = numChannels > 1 ? audioBuffer.getChannelData(1) : ch0;

  for (let point = 0; point < PREVIEW_POINTS; point++) {
    const first = clamp(
      Math.floor((point * sampleCount) / PREVIEW_POINTS),
      0,
      sampleCount - 1
    );
    const last = clamp(
      Math.max(first + 1, Math.floor(((point + 1) * sampleCount) / PREVIEW_POINTS)),
      first + 1,
      sampleCount
    );
    let minV = 1.0;
    let maxV = -1.0;
    for (let s = first; s < last; s++) {
      const v0 = ch0[s];
      const v1 = ch1[s];
      if (v0 < minV) minV = v0;
      if (v0 > maxV) maxV = v0;
      if (v1 < minV) minV = v1;
      if (v1 > maxV) maxV = v1;
    }
    minimum[point] = minV;
    maximum[point] = maxV;
  }

  return {
    minimum,
    maximum,
    durationMs: (sampleCount * 1000.0) / audioBuffer.sampleRate,
    loaded: true,
  };
}

export function formatMs(ms: number): string {
  if (ms >= 1000.0) return `${(ms / 1000.0).toFixed(2)} s`;
  return `${ms.toFixed(ms < 10.0 ? 2 : 1)} ms`;
}

export function formatTimeTickLabel(ms: number): string {
  const absMs = Math.abs(ms);
  if (absMs < 0.05) return '0.00s';
  if (absMs >= 1000.0) return `${(ms / 1000.0).toFixed(2)}s`;
  if (absMs >= 100.0) return `${(ms / 1000.0).toFixed(3)}s`;
  if (absMs >= 10.0) return `${ms.toFixed(1)}ms`;
  return `${ms.toFixed(2)}ms`;
}

export function formatEnvelopeTime(value: number): string {
  if (value >= 1000.0) return `${(value / 1000.0).toFixed(2)}s`;
  return `${value.toFixed(value < 10.0 ? 1 : 0)}ms`;
}

export function formatTrimPercent(norm: number): string {
  const pct = norm * 100.0;
  if (Math.abs(pct - Math.round(pct)) < 0.05) {
    return `${Math.round(pct)}%`;
  }
  return `${pct.toFixed(1)}%`;
}

export function formatDb(linear: number, floorDb = -90.0): string {
  const db = 20.0 * Math.log10(Math.max(linear, 1.0e-6));
  return db <= floorDb ? '-inf dB' : `${db.toFixed(1)} dB`;
}
