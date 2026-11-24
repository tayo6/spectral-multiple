import {
  clamp,
  computeEnvelopeSchedule,
  computeWaveformPreview,
  createEmptyPreview,
  envelopeLevelAtMs,
  flSegmentLevel,
} from './envelopeMath';
import {
  LayerParameters,
  LayerSampleInfo,
  MAX_PRE_ROLL_MS,
  NUM_LAYERS,
  PRE_LAYERS,
  VOICES_PER_LAYER,
} from './types';

interface LayerAudioNodes {
  hpf1: BiquadFilterNode;
  hpf2: BiquadFilterNode;
  lpf1: BiquadFilterNode;
  lpf2: BiquadFilterNode;
  panner: StereoPannerNode;
  gain: GainNode;
  analyser: AnalyserNode;
  timeData: Float32Array<ArrayBuffer>;
  freqData: Float32Array<ArrayBuffer>;
}

interface ActiveVoice {
  source: AudioBufferSourceNode;
  endTime: number;
}

export interface MeterSnapshot {
  peakNow: number;
  rmsNow: number;
  crestNow: number;
  peakHold: number;
  rmsHold: number;
  crestHold: number;
}

export class WebAudioSampleEngine {
  private ctx: AudioContext | null = null;
  private masterGain: GainNode | null = null;
  private limiter: DynamicsCompressorNode | null = null;
  private masterAnalyser: AnalyserNode | null = null;
  private masterTimeData: Float32Array<ArrayBuffer> = new Float32Array(2048);
  private layerNodes: LayerAudioNodes[] = [];
  private activeVoices: ActiveVoice[][] = Array.from({ length: NUM_LAYERS }, () => []);

  public samples: LayerSampleInfo[] = Array.from({ length: NUM_LAYERS }, () => ({
    loaded: false,
    displayName: '',
    durationMs: 0,
    sampleRate: 44100,
    preview: createEmptyPreview(),
    buffer: null,
  }));

  private peakEnv = 0;
  private meanSquare = 0;
  private peakHold = 0;
  private rmsHold = 0;
  private crestHold = 1;

  // Scope ring buffers for live / scroll / held oscilloscope & FFT capture
  public static readonly SCOPE_COLS = 1024;
  public static readonly SPECTRUM_BINS = 512;

  public ensureContext(): AudioContext {
    if (!this.ctx) {
      const AudioCtx = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
      this.ctx = new AudioCtx();

      this.masterGain = this.ctx.createGain();
      this.masterGain.gain.value = 1.0;

      // Soft transparent safety limiter at output
      this.limiter = this.ctx.createDynamicsCompressor();
      this.limiter.threshold.value = -1.0;
      this.limiter.knee.value = 3.0;
      this.limiter.ratio.value = 12.0;
      this.limiter.attack.value = 0.001;
      this.limiter.release.value = 0.08;

      this.masterAnalyser = this.ctx.createAnalyser();
      this.masterAnalyser.fftSize = 2048;
      this.masterTimeData = new Float32Array(this.masterAnalyser.fftSize);

      this.masterGain.connect(this.limiter);
      this.limiter.connect(this.masterAnalyser);
      this.masterAnalyser.connect(this.ctx.destination);

      this.layerNodes = [];
      for (let i = 0; i < NUM_LAYERS; i++) {
        const hpf1 = this.ctx.createBiquadFilter();
        hpf1.type = 'highpass';
        hpf1.Q.value = 0.7071;
        hpf1.frequency.value = 20;

        const hpf2 = this.ctx.createBiquadFilter();
        hpf2.type = 'highpass';
        hpf2.Q.value = 0.7071;
        hpf2.frequency.value = 20;

        const lpf1 = this.ctx.createBiquadFilter();
        lpf1.type = 'lowpass';
        lpf1.Q.value = 0.7071;
        lpf1.frequency.value = 20000;

        const lpf2 = this.ctx.createBiquadFilter();
        lpf2.type = 'lowpass';
        lpf2.Q.value = 0.7071;
        lpf2.frequency.value = 20000;

        const panner = this.ctx.createStereoPanner();
        const gain = this.ctx.createGain();
        const analyser = this.ctx.createAnalyser();
        analyser.fftSize = 4096;
        analyser.smoothingTimeConstant = 0.55;

        hpf1.connect(hpf2);
        hpf2.connect(lpf1);
        lpf1.connect(lpf2);
        lpf2.connect(panner);
        panner.connect(gain);
        gain.connect(analyser);
        analyser.connect(this.masterGain);

        this.layerNodes.push({
          hpf1,
          hpf2,
          lpf1,
          lpf2,
          panner,
          gain,
          analyser,
          timeData: new Float32Array(analyser.fftSize),
          freqData: new Float32Array(analyser.frequencyBinCount),
        });
      }
    }

    if (this.ctx.state === 'suspended') {
      void this.ctx.resume();
    }
    return this.ctx;
  }

  public getSampleRate(): number {
    return this.ctx?.sampleRate ?? 44100;
  }

  public async loadSampleFromFile(layer: number, file: File): Promise<{ ok: boolean; error?: string }> {
    if (layer < 0 || layer >= NUM_LAYERS) {
      return { ok: false, error: 'Invalid layer number.' };
    }
    try {
      const ctx = this.ensureContext();
      const arrayBuf = await file.arrayBuffer();
      const decoded = await ctx.decodeAudioData(arrayBuf);
      if (decoded.length <= 0 || decoded.sampleRate <= 0) {
        return { ok: false, error: 'Could not decode audio file.' };
      }
      if (decoded.duration > 180) {
        return { ok: false, error: 'Samples longer than three minutes are not supported.' };
      }
      const displayName = file.name.replace(/\.[^/.]+$/, '');
      this.setSampleBuffer(layer, decoded, displayName);
      return { ok: true };
    } catch (err) {
      return {
        ok: false,
        error: err instanceof Error ? err.message : 'Unsupported or corrupted audio file.',
      };
    }
  }

  public setSampleBuffer(layer: number, buffer: AudioBuffer, displayName: string): void {
    if (layer < 0 || layer >= NUM_LAYERS) return;
    const preview = computeWaveformPreview(buffer);
    this.samples[layer] = {
      loaded: true,
      displayName,
      durationMs: preview.durationMs,
      sampleRate: buffer.sampleRate,
      preview,
      buffer,
    };
  }

  public clearSample(layer: number): void {
    if (layer < 0 || layer >= NUM_LAYERS) return;
    this.stopLayerVoices(layer);
    this.samples[layer] = {
      loaded: false,
      displayName: '',
      durationMs: 0,
      sampleRate: 44100,
      preview: createEmptyPreview(),
      buffer: null,
    };
  }

  public stopLayerVoices(layer: number): void {
    const voices = this.activeVoices[layer];
    for (const v of voices) {
      try {
        v.source.stop();
        v.source.disconnect();
      } catch {
        // already stopped
      }
    }
    this.activeVoices[layer] = [];
  }

  public getLoadedMask(): number {
    let mask = 0;
    for (let i = 0; i < NUM_LAYERS; i++) {
      if (this.samples[i].loaded) mask |= 1 << i;
    }
    return mask;
  }

  public isLayerAudible(layer: number, allParams: LayerParameters[]): boolean {
    if (layer < 0 || layer >= NUM_LAYERS) return false;
    const p = allParams[layer];
    if (p.muted) return false;
    const anySolo = allParams.some((lp) => lp.solo);
    return !anySolo || p.solo;
  }

  public syncLayerNodes(layer: number, p: LayerParameters, audible: boolean): void {
    if (!this.ctx || !this.layerNodes[layer]) return;
    const nodes = this.layerNodes[layer];
    const now = this.ctx.currentTime;
    const maxLp = Math.max(1000, this.ctx.sampleRate * 0.45);
    const hpHz = clamp(Math.min(p.highPassHz, p.lowPassHz * 0.8), 20, 1000);
    const lpHz = clamp(Math.max(p.lowPassHz, hpHz * 1.25), 1000, maxLp);

    nodes.hpf1.frequency.setTargetAtTime(hpHz > 20.5 ? hpHz : 10, now, 0.008);
    nodes.hpf2.frequency.setTargetAtTime(hpHz > 20.5 ? hpHz : 10, now, 0.008);
    nodes.lpf1.frequency.setTargetAtTime(p.lowPassHz < 19990 ? lpHz : maxLp, now, 0.008);
    nodes.lpf2.frequency.setTargetAtTime(p.lowPassHz < 19990 ? lpHz : maxLp, now, 0.008);
    nodes.panner.pan.setTargetAtTime(clamp(p.pan, -1, 1), now, 0.008);
    nodes.gain.gain.setTargetAtTime(audible ? clamp(p.gain, 0, 2) : 0, now, 0.006);
  }

  private renderVoiceBuffer(
    ctx: AudioContext,
    srcBuffer: AudioBuffer,
    p: LayerParameters
  ): AudioBuffer | null {
    const sr = ctx.sampleRate;
    const srcLen = srcBuffer.length;
    if (srcLen <= 0) return null;

    const trimStartIdx =
      srcLen <= 1 ? 0 : clamp(Math.floor(p.trimStart * srcLen), 0, srcLen - 2);
    const trimEndIdx =
      srcLen <= 1 ? 1 : clamp(Math.ceil(p.trimEnd * srcLen), trimStartIdx + 1, srcLen);
    const trimmedLength = trimEndIdx - trimStartIdx;
    if (trimmedLength <= 0) return null;

    const baseSpeed =
      (srcBuffer.sampleRate / sr) * Math.pow(2.0, p.pitchSemitones / 12.0);
    const safeBaseSpeed = Math.max(0.0001, baseSpeed);

    // Estimate rendered length considering Pogo pitch bend
    let estSamples = 0;
    if (Math.abs(p.pogo) < 1.0e-4) {
      estSamples = Math.ceil(trimmedLength / safeBaseSpeed);
    } else {
      const steps = 64;
      const du = 1.0 / steps;
      let invSum = 0;
      for (let k = 0; k < steps; k++) {
        const uMid = (k + 0.5) * du;
        const pogoSt = p.pogo * 24.0 * Math.pow(1.0 - uMid, 2.5);
        invSum += 1.0 / Math.max(0.0001, safeBaseSpeed * Math.pow(2.0, pogoSt / 12.0));
      }
      estSamples = Math.ceil(trimmedLength * du * invSum) + 64;
    }

    const maxOutSamples = Math.min(Math.max(1, estSamples), sr * 30);
    const outBuf = ctx.createBuffer(2, maxOutSamples, sr);
    const outL = outBuf.getChannelData(0);
    const outR = outBuf.getChannelData(1);

    const srcL = srcBuffer.getChannelData(0);
    const srcR = srcBuffer.numberOfChannels > 1 ? srcBuffer.getChannelData(1) : srcL;

    const totalOutputSamples = trimmedLength / safeBaseSpeed;
    const totalDurationMs = (totalOutputSamples / sr) * 1000.0;
    const sched = p.envEnabled ? computeEnvelopeSchedule(p, totalDurationMs) : null;
    const fadeInSamples = p.fadeInMs * 0.001 * sr;
    const fadeOutSamples = p.fadeOutMs * 0.001 * sr;

    let position = 0.0;
    let ageSamples = 0;
    let inRelease = false;
    let releaseStartLevel = 1.0;

    while (ageSamples < maxOutSamples && position < trimmedLength) {
      const u = clamp(position / trimmedLength, 0.0, 1.0);
      const pogoSemitones = p.pogo * 24.0 * Math.pow(1.0 - u, 2.5);
      const speed = baseSpeed * Math.pow(2.0, pogoSemitones / 12.0);

      const rawSamplePos = p.reverse
        ? trimEndIdx - 1 - position
        : trimStartIdx + position;
      const clampedPos = clamp(rawSamplePos, trimStartIdx, trimEndIdx - 1);
      const idx0 = clamp(Math.floor(clampedPos), trimStartIdx, trimEndIdx - 1);
      const idx1 = Math.min(trimEndIdx - 1, idx0 + 1);
      const frac = clamp(clampedPos - idx0, 0.0, 1.0);

      const rawL = srcL[idx0] + (srcL[idx1] - srcL[idx0]) * frac;
      const rawR = srcR[idx0] + (srcR[idx1] - srcR[idx0]) * frac;

      const elapsedSamplesBase = position / safeBaseSpeed;
      const remainingSamplesBase = (trimmedLength - position) / safeBaseSpeed;
      const elapsedMs = (elapsedSamplesBase / sr) * 1000.0;
      const remainingMs = (remainingSamplesBase / sr) * 1000.0;

      let envelope = 1.0;
      if (p.envEnabled && sched) {
        if (remainingMs <= sched.releaseMs) {
          if (!inRelease) {
            releaseStartLevel = envelopeLevelAtMs(p, sched, elapsedMs);
            inRelease = true;
          }
          const releaseProgress = clamp(
            1.0 - remainingMs / Math.max(0.1, sched.releaseMs),
            0.0,
            1.0
          );
          envelope = flSegmentLevel(
            releaseProgress,
            releaseStartLevel,
            0.0,
            p.releaseCurve,
            p.releaseTension
          );
        } else {
          envelope = envelopeLevelAtMs(p, sched, elapsedMs);
        }
      }

      const fadeInGain =
        fadeInSamples > 0.5 ? clamp(ageSamples / fadeInSamples, 0.0, 1.0) : 1.0;
      const fadeOutGain =
        fadeOutSamples > 0.5 ? clamp(remainingSamplesBase / fadeOutSamples, 0.0, 1.0) : 1.0;

      const g = envelope * fadeInGain * fadeOutGain;
      outL[ageSamples] = rawL * g;
      outR[ageSamples] = rawR * g;

      position += speed;
      ageSamples++;
    }

    return outBuf;
  }

  private startLayerVoice(
    layer: number,
    p: LayerParameters,
    when: number,
    allParams: LayerParameters[],
    ignoreMuteSolo = false
  ): void {
    const sample = this.samples[layer];
    if (!sample.loaded || !sample.buffer) return;
    if (!ignoreMuteSolo && !this.isLayerAudible(layer, allParams)) return;
    if (ignoreMuteSolo && p.muted) return;

    const ctx = this.ensureContext();
    const audible = ignoreMuteSolo ? !p.muted : this.isLayerAudible(layer, allParams);
    this.syncLayerNodes(layer, p, audible);

    const rendered = this.renderVoiceBuffer(ctx, sample.buffer, p);
    if (!rendered) return;

    const now = ctx.currentTime;
    const voices = this.activeVoices[layer];
    // Prune finished voices
    for (let i = voices.length - 1; i >= 0; i--) {
      if (voices[i].endTime <= now) {
        voices.splice(i, 1);
      }
    }
    // Enforce 4-voice polyphony with oldest stealing
    while (voices.length >= VOICES_PER_LAYER) {
      const oldest = voices.shift();
      if (oldest) {
        try {
          oldest.source.stop();
          oldest.source.disconnect();
        } catch {
          // ignore
        }
      }
    }

    const src = ctx.createBufferSource();
    src.buffer = rendered;
    src.connect(this.layerNodes[layer].hpf1);

    const startTime = Math.max(now, when);
    src.start(startTime);
    const endTime = startTime + rendered.duration;
    voices.push({ source: src, endTime });
    src.onended = () => {
      src.disconnect();
      const idx = this.activeVoices[layer].findIndex((v) => v.source === src);
      if (idx >= 0) this.activeVoices[layer].splice(idx, 1);
    };
  }

  public triggerAllLayers(allParams: LayerParameters[], preRollMs: number): void {
    const ctx = this.ensureContext();
    const now = ctx.currentTime;
    // Anchor at MAX_PRE_ROLL_MS (100 ms) so PRE layers genuinely fire before MAIN transient
    const maxDelaySec = MAX_PRE_ROLL_MS * 0.001;
    const preRollSec = clamp(preRollMs, 0, MAX_PRE_ROLL_MS) * 0.001;

    // Find earliest start offset so we don't clip negative offsets unnecessarily
    let minOffsetSec = 0;
    for (let i = 0; i < NUM_LAYERS; i++) {
      if (!this.samples[i].loaded) continue;
      const baseSec = i < PRE_LAYERS ? maxDelaySec - preRollSec : maxDelaySec;
      const shiftSec = allParams[i].delayShiftMs * 0.001;
      const total = baseSec + shiftSec;
      if (total < minOffsetSec) minOffsetSec = total;
    }
    const anchorOffset = minOffsetSec < 0 ? -minOffsetSec : 0;

    for (let i = 0; i < NUM_LAYERS; i++) {
      const baseSec = i < PRE_LAYERS ? maxDelaySec - preRollSec : maxDelaySec;
      const shiftSec = allParams[i].delayShiftMs * 0.001;
      const when = now + anchorOffset + Math.max(0, baseSec + shiftSec);
      this.startLayerVoice(i, allParams[i], when, allParams, false);
    }
  }

  public previewLayer(layer: number, allParams: LayerParameters[]): void {
    const ctx = this.ensureContext();
    this.startLayerVoice(layer, allParams[layer], ctx.currentTime, allParams, true);
  }

  public resetMeters(): void {
    this.peakEnv = 0;
    this.meanSquare = 0;
    this.peakHold = 0;
    this.rmsHold = 0;
    this.crestHold = 1;
  }

  public readMasterMeter(): MeterSnapshot {
    if (!this.masterAnalyser) {
      return {
        peakNow: this.peakEnv,
        rmsNow: Math.sqrt(this.meanSquare),
        crestNow: this.crestHold,
        peakHold: this.peakHold,
        rmsHold: this.rmsHold,
        crestHold: this.crestHold,
      };
    }

    this.masterAnalyser.getFloatTimeDomainData(this.masterTimeData);
    let blockPeak = 0;
    let blockSumSq = 0;
    const n = this.masterTimeData.length;
    for (let i = 0; i < n; i++) {
      const v = this.masterTimeData[i];
      const av = Math.abs(v);
      if (av > blockPeak) blockPeak = av;
      blockSumSq += v * v;
    }

    this.peakEnv = Math.max(blockPeak, this.peakEnv * 0.85);
    const blockMs = blockSumSq / n;
    this.meanSquare += 0.35 * (blockMs - this.meanSquare);
    const rms = Math.sqrt(Math.max(0, this.meanSquare));
    const crest = clamp(this.peakEnv / Math.max(rms, 1.0e-6), 1.0, 1000.0);

    if (this.peakEnv > this.peakHold) this.peakHold = this.peakEnv;
    if (rms > this.rmsHold) this.rmsHold = rms;
    if (rms > 1.0e-4 && crest > this.crestHold) this.crestHold = crest;

    return {
      peakNow: this.peakEnv,
      rmsNow: rms,
      crestNow: crest,
      peakHold: this.peakHold,
      rmsHold: this.rmsHold,
      crestHold: this.crestHold,
    };
  }

  public getLayerTimeDomain(layer: number): Float32Array | null {
    const nodes = this.layerNodes[layer];
    if (!nodes) return null;
    nodes.analyser.getFloatTimeDomainData(nodes.timeData);
    return nodes.timeData;
  }

  public getLayerSpectrum(layer: number): Float32Array | null {
    const nodes = this.layerNodes[layer];
    if (!nodes) return null;
    nodes.analyser.getFloatFrequencyData(nodes.freqData);
    return nodes.freqData;
  }
}
