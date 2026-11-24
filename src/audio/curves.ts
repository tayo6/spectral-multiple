import { PREVIEW_POINTS, WaveformPreview } from './types';

export function clamp(v: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, v));
}

export function applyPowerTension(u: number, tension: number): number {
  const t = clamp(u, 0, 1);
  const ten = clamp(tension, -1, 1);
  if (Math.abs(ten) < 1e-4) return t;
  const exp = Math.pow(2, Math.abs(ten) * 3.0);
  return ten >= 0 ? 1 - Math.pow(1 - t, exp) : Math.pow(t, exp);
}

/**
 * Evaluates an FL Studio style segment transition from y0 -> y1 at normalized t in [0, 1].
 * Positive visual tension (+1) always bends the curve UP visually; negative (-1) bends DOWN.
 */
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
  const rising = y1 >= y0;
  const dirTen = rising ? ten : -ten;

  let frac = t;
  switch (m) {
    case 0: // Hold
      frac = t >= 1 ? 1 : 0;
      break;
    case 1: {
      // Smooth (S-curve with tension shift)
      const shaped = applyPowerTension(t, dirTen);
      frac = shaped * shaped * (3 - 2 * shaped);
      break;
    }
    case 2: // Single curve
      frac = applyPowerTension(t, dirTen);
      break;
    case 3: {
      // Half sine
      const halfSine = 0.5 - 0.5 * Math.cos(Math.PI * t);
      frac = applyPowerTension(halfSine, dirTen);
      break;
    }
    case 4: {
      // Stairs (step count controlled by tension)
      const steps = Math.max(2, Math.round(2 + (ten + 1) * 6));
      frac = t >= 1 ? 1 : Math.floor(t * steps) / steps;
      break;
    }
    case 5: {
      // Smooth stairs
      const steps = Math.max(2, Math.round(2 + (ten + 1) * 6));
      if (t >= 1) {
        frac = 1;
      } else {
        const scaled = t * steps;
        const idx = Math.floor(scaled);
        const f = scaled - idx;
        const smoothF = f * f * (3 - 2 * f);
        frac = (idx + smoothF) / steps;
      }
      break;
    }
    case 6: {
      // Pulse (cycles controlled by tension)
      const cycles = Math.max(1, Math.round(1 + (ten + 1) * 5));
      frac = t >= 1 ? 1 : ((t * cycles) % 1) < 0.5 ? 0 : 1;
      break;
    }
    case 7: {
      // Wave (sine oscillations ending cleanly at y1)
      const cycles = Math.max(1, Math.round(1 + (ten + 1) * 5));
      frac = 0.5 * (1 - Math.cos(Math.PI * (2 * cycles + 1) * t));
      break;
    }
    default:
      frac = applyPowerTension(t, dirTen);
      break;
  }

  return y0 + (y1 - y0) * frac;
}

export function curveValue(progress: number, curve: number, tension = 0): number {
  return flSegmentLevel(progress, 0, 1, curve, tension);
}

export function formatEnvelopeTime(valueMs: number): string {
  if (valueMs >= 1000) {
    return `${(valueMs / 1000).toFixed(2)}s`;
  }
  return `${valueMs.toFixed(valueMs < 10 ? 1 : 0)}ms`;
}

export function formatMs(ms: number): string {
  if (ms >= 1000) {
    return `${(ms / 1000).toFixed(2)} s`;
  }
  return `${ms.toFixed(ms < 10 ? 2 : 1)} ms`;
}

export function formatHzShort(hz: number): string {
  return hz >= 1000 ? `${(hz / 1000).toFixed(1)}k` : `${Math.round(hz)}`;
}

export function formatPan(pan: number): string {
  if (Math.abs(pan) < 0.005) return 'C';
  return `${Math.round(Math.abs(pan) * 100)}${pan < 0 ? 'L' : 'R'}`;
}

export function formatPitch(st: number): string {
  return `${st > 0 ? '+' : ''}${st.toFixed(1)}`;
}

export function formatDb(linear: number, floorDb = -90): string {
  const db = 20 * Math.log10(Math.max(linear, 1e-6));
  return db <= floorDb ? '-inf' : `${db.toFixed(1)} dB`;
}

export function skewedToValue(
  norm: number,
  min: number,
  max: number,
  skew: number
): number {
  const t = clamp(norm, 0, 1);
  return min + (max - min) * Math.pow(t, 1 / skew);
}

export function valueToSkewed(
  val: number,
  min: number,
  max: number,
  skew: number
): number {
  const proportion = clamp((val - min) / (max - min), 0, 1);
  return Math.pow(proportion, skew);
}

export function computeWaveformPreview(
  left: Float32Array,
  right: Float32Array,
  sampleRate: number
): WaveformPreview {
  const sampleCount = left.length;
  const minimum = new Float32Array(PREVIEW_POINTS);
  const maximum = new Float32Array(PREVIEW_POINTS);

  for (let point = 0; point < PREVIEW_POINTS; point++) {
    const first = Math.floor((point * sampleCount) / PREVIEW_POINTS);
    const last = Math.max(
      first + 1,
      Math.floor(((point + 1) * sampleCount) / PREVIEW_POINTS)
    );
    let minVal = 1.0;
    let maxVal = -1.0;
    const end = Math.min(sampleCount, last);
    for (let s = first; s < end; s++) {
      const l = left[s];
      const r = right[s];
      if (l < minVal) minVal = l;
      if (r < minVal) minVal = r;
      if (l > maxVal) maxVal = l;
      if (r > maxVal) maxVal = r;
    }
    minimum[point] = minVal;
    maximum[point] = maxVal;
  }

  return {
    minimum,
    maximum,
    durationMs: (sampleCount * 1000) / Math.max(1, sampleRate),
    loaded: true,
  };
}

export function stripExtension(filename: string): string {
  const idx = filename.lastIndexOf('.');
  return idx > 0 ? filename.slice(0, idx) : filename;
}

/**
 * Fallback decoder for uncompressed PCM AIFF/WAV if the browser's decodeAudioData rejects AIFF.
 */
function tryDecodePcmFallback(buffer: ArrayBuffer): {
  left: Float32Array;
  right: Float32Array;
  sampleRate: number;
} | null {
  if (buffer.byteLength < 12) return null;
  const view = new DataView(buffer);
  const tag = (offset: number) =>
    String.fromCharCode(
      view.getUint8(offset),
      view.getUint8(offset + 1),
      view.getUint8(offset + 2),
      view.getUint8(offset + 3)
    );

  const rootTag = tag(0);
  if (rootTag === 'FORM' && tag(8) === 'AIFF') {
    let offset = 12;
    let numChannels = 0;
    let numSampleFrames = 0;
    let sampleSize = 16;
    let sampleRate = 44100;
    let ssndOffset = -1;
    let ssndDataOffset = 0;

    while (offset + 8 <= buffer.byteLength) {
      const chunkId = tag(offset);
      const chunkSize = view.getUint32(offset + 4, false);
      const dataStart = offset + 8;

      if (chunkId === 'COMM' && chunkSize >= 18) {
        numChannels = view.getUint16(dataStart, false);
        numSampleFrames = view.getUint32(dataStart + 2, false);
        sampleSize = view.getUint16(dataStart + 6, false);
        // 80-bit IEEE 754 extended float for sample rate
        const exp = view.getUint16(dataStart + 8, false) & 0x7fff;
        const hi = view.getUint32(dataStart + 10, false);
        const lo = view.getUint32(dataStart + 14, false);
        if (exp !== 0) {
          sampleRate = (hi * 4294967296 + lo) * Math.pow(2, exp - 16383 - 63);
        }
      } else if (chunkId === 'SSND' && chunkSize >= 8) {
        ssndDataOffset = view.getUint32(dataStart, false);
        ssndOffset = dataStart + 8 + ssndDataOffset;
      }

      offset = dataStart + chunkSize + (chunkSize % 2);
    }

    if (
      ssndOffset >= 0 &&
      numChannels >= 1 &&
      numSampleFrames > 0 &&
      (sampleSize === 16 || sampleSize === 24 || sampleSize === 32)
    ) {
      const bytesPerSample = sampleSize / 8;
      const frameBytes = bytesPerSample * numChannels;
      const maxFrames = Math.min(
        numSampleFrames,
        Math.floor((buffer.byteLength - ssndOffset) / frameBytes)
      );
      const left = new Float32Array(maxFrames);
      const right = new Float32Array(maxFrames);

      for (let i = 0; i < maxFrames; i++) {
        for (let ch = 0; ch < Math.min(2, numChannels); ch++) {
          const p = ssndOffset + i * frameBytes + ch * bytesPerSample;
          let sample = 0;
          if (sampleSize === 16) {
            sample = view.getInt16(p, false) / 32768;
          } else if (sampleSize === 24) {
            let v =
              (view.getUint8(p) << 16) |
              (view.getUint8(p + 1) << 8) |
              view.getUint8(p + 2);
            if (v & 0x800000) v |= ~0xffffff;
            sample = v / 8388608;
          } else if (sampleSize === 32) {
            sample = view.getInt32(p, false) / 2147483648;
          }
          if (ch === 0) left[i] = sample;
          else right[i] = sample;
        }
        if (numChannels === 1) right[i] = left[i];
      }

      return { left, right, sampleRate };
    }
  }
  return null;
}

export async function decodeAudioFile(
  audioCtx: AudioContext,
  file: File
): Promise<{
  left: Float32Array;
  right: Float32Array;
  sampleRate: number;
  preview: WaveformPreview;
  displayName: string;
}> {
  const arrayBuffer = await file.arrayBuffer();
  let left: Float32Array;
  let right: Float32Array;
  let sampleRate: number;

  try {
    const decoded = await audioCtx.decodeAudioData(arrayBuffer.slice(0));
    sampleRate = decoded.sampleRate;
    const maxSamples = Math.floor(sampleRate * 180);
    if (decoded.length > maxSamples) {
      throw new Error('Samples longer than three minutes are not supported.');
    }
    left = new Float32Array(decoded.getChannelData(0));
    right =
      decoded.numberOfChannels > 1
        ? new Float32Array(decoded.getChannelData(1))
        : new Float32Array(left);
  } catch (err) {
    const fallback = tryDecodePcmFallback(arrayBuffer);
    if (!fallback) {
      throw err instanceof Error
        ? err
        : new Error('The audio file could not be decoded.');
    }
    const maxSamples = Math.floor(fallback.sampleRate * 180);
    if (fallback.left.length > maxSamples) {
      throw new Error('Samples longer than three minutes are not supported.');
    }
    left = fallback.left;
    right = fallback.right;
    sampleRate = fallback.sampleRate;
  }

  const preview = computeWaveformPreview(left, right, sampleRate);
  return {
    left,
    right,
    sampleRate,
    preview,
    displayName: stripExtension(file.name),
  };
}
