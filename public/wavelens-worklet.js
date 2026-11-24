const NUM_LAYERS = 6;
const VOICES_PER_LAYER = 4;
const MAX_PRE_ROLL_MS = 100.0;
const SCOPE_COLS = 1024;
const FFT_ORDER = 12;
const FFT_SIZE = 1 << FFT_ORDER;
const SPECTRUM_BINS = 512;
const SPECTRUM_HOP = 1024;
const BARS_TABLE = [1 / 16, 1 / 8, 1 / 4, 1 / 2, 1, 2, 4];

function clamp(v, min, max) {
  return v < min ? min : v > max ? max : v;
}

class Biquad {
  constructor() {
    this.b0 = 1;
    this.b1 = 0;
    this.b2 = 0;
    this.a1 = 0;
    this.a2 = 0;
    this.z1 = 0;
    this.z2 = 0;
  }

  reset() {
    this.z1 = 0;
    this.z2 = 0;
  }

  setButterworth(sampleRate, cutoffHz, isHighPass) {
    const fc = clamp(cutoffHz, 10, sampleRate * 0.48);
    const w0 = (2 * Math.PI * fc) / sampleRate;
    const cosw0 = Math.cos(w0);
    const sinw0 = Math.sin(w0);
    const alpha = sinw0 / Math.SQRT2;
    const a0 = 1 + alpha;

    if (isHighPass) {
      this.b0 = ((1 + cosw0) * 0.5) / a0;
      this.b1 = -(1 + cosw0) / a0;
      this.b2 = ((1 + cosw0) * 0.5) / a0;
    } else {
      this.b0 = ((1 - cosw0) * 0.5) / a0;
      this.b1 = (1 - cosw0) / a0;
      this.b2 = ((1 - cosw0) * 0.5) / a0;
    }
    this.a1 = (-2 * cosw0) / a0;
    this.a2 = (1 - alpha) / a0;
  }

  process(x) {
    const y = this.b0 * x + this.z1;
    this.z1 = this.b1 * x - this.a1 * y + this.z2;
    this.z2 = this.b2 * x - this.a2 * y;
    return y;
  }
}

class LinkwitzRileyStereo {
  constructor(isHighPass) {
    this.isHighPass = isHighPass;
    this.cutoff = isHighPass ? 20 : 20000;
    this.l1 = new Biquad();
    this.l2 = new Biquad();
    this.r1 = new Biquad();
    this.r2 = new Biquad();
  }

  prepare(sampleRate) {
    this.sampleRate = sampleRate;
    this.setCutoff(this.cutoff);
    this.reset();
  }

  setCutoff(hz) {
    this.cutoff = hz;
    if (!this.sampleRate) return;
    this.l1.setButterworth(this.sampleRate, hz, this.isHighPass);
    this.l2.setButterworth(this.sampleRate, hz, this.isHighPass);
    this.r1.setButterworth(this.sampleRate, hz, this.isHighPass);
    this.r2.setButterworth(this.sampleRate, hz, this.isHighPass);
  }

  reset() {
    this.l1.reset();
    this.l2.reset();
    this.r1.reset();
    this.r2.reset();
  }

  processLeft(x) {
    return this.l2.process(this.l1.process(x));
  }

  processRight(x) {
    return this.r2.process(this.r1.process(x));
  }
}

class RealFFT {
  constructor(order) {
    this.n = 1 << order;
    this.order = order;
    this.bitRev = new Uint32Array(this.n);
    for (let i = 0; i < this.n; i++) {
      let x = i;
      let r = 0;
      for (let b = 0; b < order; b++) {
        r = (r << 1) | (x & 1);
        x >>= 1;
      }
      this.bitRev[i] = r;
    }

    this.cosTable = new Float32Array(this.n >> 1);
    this.sinTable = new Float32Array(this.n >> 1);
    for (let i = 0; i < (this.n >> 1); i++) {
      const ang = (-2 * Math.PI * i) / this.n;
      this.cosTable[i] = Math.cos(ang);
      this.sinTable[i] = Math.sin(ang);
    }

    this.re = new Float32Array(this.n);
    this.im = new Float32Array(this.n);
    this.mag = new Float32Array(this.n >> 1);
  }

  computeMagnitudes(input) {
    const n = this.n;
    const re = this.re;
    const im = this.im;
    const bitRev = this.bitRev;

    for (let i = 0; i < n; i++) {
      re[i] = input[bitRev[i]];
      im[i] = 0;
    }

    for (let size = 2; size <= n; size <<= 1) {
      const half = size >> 1;
      const step = n / size;
      for (let i = 0; i < n; i += size) {
        let k = 0;
        for (let j = i; j < i + half; j++) {
          const wr = this.cosTable[k];
          const wi = this.sinTable[k];
          const tr = re[j + half] * wr - im[j + half] * wi;
          const ti = re[j + half] * wi + im[j + half] * wr;
          re[j + half] = re[j] - tr;
          im[j + half] = im[j] - ti;
          re[j] += tr;
          im[j] += ti;
          k += step;
        }
      }
    }

    const mag = this.mag;
    const halfN = n >> 1;
    for (let i = 0; i < halfN; i++) {
      mag[i] = Math.hypot(re[i], im[i]);
    }
    return mag;
  }
}

class WaveLensAudioWorklet extends AudioWorkletProcessor {
  constructor() {
    super();
    this.sr = sampleRate;
    this.maximumDelaySamples = Math.max(
      1,
      Math.round(this.sr * MAX_PRE_ROLL_MS * 0.001)
    );
    const delaySize = this.maximumDelaySamples + 1;
    this.delayLeft = new Float32Array(delaySize);
    this.delayRight = new Float32Array(delaySize);
    this.delayWritePos = 0;

    this.refractorySamples = Math.max(1, Math.round(this.sr * 0.025));
    this.refractoryRemaining = 0;
    this.previousInputLevel = 0;
    this.threshold = 0.03;
    this.triggerMode = 2; // 0 = Audio, 1 = MIDI, 2 = Both

    this.samples = new Array(NUM_LAYERS).fill(null);
    this.params = [];
    this.voices = [];
    this.nextVoiceToSteal = new Int32Array(NUM_LAYERS);
    this.highPass = [];
    this.lowPass = [];

    for (let i = 0; i < NUM_LAYERS; i++) {
      this.params.push({
        gain: 1.0,
        pan: 0.0,
        pitchSemitones: 0.0,
        delayShiftMs: i < 3 ? -50.0 : 0.0,
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
      });

      const layerVoices = [];
      for (let v = 0; v < VOICES_PER_LAYER; v++) {
        layerVoices.push({
          sample: null,
          position: 0,
          delayRemaining: 0,
          ageSamples: 0,
          releaseStartLevel: 1,
          inRelease: false,
          active: false,
        });
      }
      this.voices.push(layerVoices);

      const hp = new LinkwitzRileyStereo(true);
      hp.prepare(this.sr);
      this.highPass.push(hp);

      const lp = new LinkwitzRileyStereo(false);
      lp.prepare(this.sr);
      lp.setCutoff(Math.min(20000, this.sr * 0.45));
      this.lowPass.push(lp);
    }

    // Build 7 x 257 curve tables matching LayeredSampleEngine.cpp
    this.curveTables = [];
    for (let curve = 0; curve < 7; curve++) {
      const table = new Float32Array(257);
      for (let i = 0; i <= 256; i++) {
        const t = i / 256;
        let value = t;
        switch (curve) {
          case 0:
            value = t;
            break;
          case 1:
            value = t * t;
            break;
          case 2:
            value = 1 - (1 - t) * (1 - t);
            break;
          case 3:
            value = t * t * t * (t * (t * 6 - 15) + 10);
            break;
          case 4:
            value = (Math.exp(5 * t) - 1) / (Math.exp(5) - 1);
            break;
          case 5:
            value = Math.log1p(9 * t) / Math.log(10);
            break;
          case 6: {
            let u = t;
            for (let iter = 0; iter < 6; iter++) {
              const oneMinus = 1 - u;
              const x =
                3 * oneMinus * oneMinus * u * 0.22 +
                3 * oneMinus * u * u * 0.42 +
                u * u * u;
              const dx =
                3 * oneMinus * oneMinus * 0.22 +
                6 * oneMinus * u * (0.42 - 0.22) +
                3 * u * u * (1 - 0.42);
              if (Math.abs(dx) > 1e-5) {
                u = clamp(u + (t - x) / dx, 0, 1);
              }
            }
            const oneMinus = 1 - u;
            value =
              3 * oneMinus * oneMinus * u * 0.02 +
              3 * oneMinus * u * u * 0.98 +
              u * u * u;
            break;
          }
          default:
            break;
        }
        table[i] = clamp(value, 0, 1);
      }
      this.curveTables.push(table);
    }

    // Capture & spectrum state
    this.colMin = new Float32Array(NUM_LAYERS * SCOPE_COLS);
    this.colMax = new Float32Array(NUM_LAYERS * SCOPE_COLS);
    this.heldMin = new Float32Array(NUM_LAYERS * SCOPE_COLS);
    this.heldMax = new Float32Array(NUM_LAYERS * SCOPE_COLS);
    this.spectrumDb = new Float32Array(NUM_LAYERS * SPECTRUM_BINS).fill(-120);
    this.heldSpectrumDb = new Float32Array(NUM_LAYERS * SPECTRUM_BINS).fill(-120);

    this.fft = new RealFFT(FFT_ORDER);
    this.fftWindow = new Float32Array(FFT_SIZE);
    this.fftWindowSum = 0;
    for (let n = 0; n < FFT_SIZE; n++) {
      const w = 0.5 - 0.5 * Math.cos((2 * Math.PI * n) / (FFT_SIZE - 1));
      this.fftWindow[n] = w;
      this.fftWindowSum += w;
    }

    this.fftRing = [];
    this.fftWritePos = new Int32Array(NUM_LAYERS);
    for (let s = 0; s < NUM_LAYERS; s++) {
      this.fftRing.push(new Float32Array(FFT_SIZE));
    }
    this.fftScratch = new Float32Array(FFT_SIZE);
    this.spectrumBinLo = new Int32Array(SPECTRUM_BINS);
    this.spectrumBinMix = new Float32Array(SPECTRUM_BINS);
    for (let b = 0; b < SPECTRUM_BINS; b++) {
      const f = 20 * Math.pow(1000, b / (SPECTRUM_BINS - 1));
      const x = clamp((f * FFT_SIZE) / this.sr, 0, FFT_SIZE / 2 - 2);
      const lo = Math.floor(x);
      this.spectrumBinLo[b] = lo;
      this.spectrumBinMix[b] = x - lo;
    }
    this.fftSamplesSinceUpdate = 0;
    this.fftSamplesCollected = 0;

    // Metering
    this.peakDecay = Math.exp(-1 / (0.35 * this.sr));
    this.msAlpha = 1 - Math.exp(-1 / (0.3 * this.sr));
    this.peakEnv = 0;
    this.meanSquare = 0;
    this.peakNow = 0;
    this.rmsNow = 0;
    this.crestNow = 1;
    this.peakHold = 0;
    this.rmsHold = 0;
    this.crestHold = 1;

    // Capture configuration
    this.captureMode = 0; // 0 = Tempo, 1 = Live, 2 = Scroll
    this.barsIdx = 4; // 1 bar
    this.timeMs = 200;
    this.frozen = false;
    this.wasFrozen = false;
    this.colF = 0;
    this.colsPerSample = 1;
    this.curCol = 0;
    this.passStart = 0;
    this.filledCount = 0;
    this.needInit = true;
    this.havePass = false;
    this.tempoRunning = false;
    this.heldFrom = 0;
    this.heldTo = 0;
    this.heldValid = false;
    this.totalSamples = 0;
    this.bpm = 120;

    this.monitorActive = false;
    this.blocksSincePost = 0;
    this.layerValues = new Float32Array(NUM_LAYERS);

    this.port.onmessage = (e) => this.handleMessage(e.data);
  }

  handleMessage(msg) {
    switch (msg.type) {
      case 'loadSample': {
        const { layer, left, right, sampleRate: sRate } = msg;
        if (layer >= 0 && layer < NUM_LAYERS) {
          this.samples[layer] = {
            left,
            right,
            sourceSampleRate: sRate || this.sr,
            length: left.length,
          };
        }
        break;
      }
      case 'clearSample': {
        const { layer } = msg;
        if (layer >= 0 && layer < NUM_LAYERS) {
          this.samples[layer] = null;
          for (const v of this.voices[layer]) {
            v.active = false;
            v.sample = null;
          }
          this.nextVoiceToSteal[layer] = 0;
        }
        break;
      }
      case 'setLayerParams': {
        const { layer, params } = msg;
        if (layer >= 0 && layer < NUM_LAYERS) {
          const t = this.params[layer];
          t.gain = clamp(params.gain, 0, 2);
          t.pan = clamp(params.pan, -1, 1);
          t.pitchSemitones = clamp(params.pitchSemitones, -24, 24);
          t.delayShiftMs = clamp(params.delayShiftMs, -100, 100);
          let hp = clamp(params.highPassHz, 20, 1000);
          let lp = clamp(params.lowPassHz, 1000, Math.max(1000, this.sr * 0.45));
          hp = Math.min(hp, lp * 0.8);
          lp = Math.max(lp, hp * 1.25);
          if (hp !== t.highPassHz) {
            t.highPassHz = hp;
            this.highPass[layer].setCutoff(hp);
          }
          const clampedLp = Math.min(lp, this.sr * 0.45);
          if (clampedLp !== t.lowPassHz) {
            t.lowPassHz = clampedLp;
            this.lowPass[layer].setCutoff(clampedLp);
          }
          t.attackMs = clamp(params.attackMs, 0.1, 2000);
          t.holdMs = clamp(params.holdMs, 0, 2000);
          t.decayMs = clamp(params.decayMs, 0.1, 5000);
          t.sustain = clamp(params.sustain, 0, 1);
          t.releaseMs = clamp(params.releaseMs, 0.1, 10000);
          t.attackCurve = clamp(Math.round(params.attackCurve), 0, 7);
          t.decayCurve = clamp(Math.round(params.decayCurve), 0, 7);
          t.releaseCurve = clamp(Math.round(params.releaseCurve), 0, 7);
          t.attackTension = clamp(params.attackTension ?? 0, -1, 1);
          t.decayTension = clamp(params.decayTension ?? 0, -1, 1);
          t.releaseTension = clamp(params.releaseTension ?? 0, -1, 1);
          t.envEnabled = params.envEnabled !== undefined ? !!params.envEnabled : false;
          t.muted = !!params.muted;
          t.solo = !!params.solo;
          t.reverse = !!params.reverse;
          t.pogo = clamp(params.pogo ?? 0, -1, 1);
          const ts = clamp(params.trimStart ?? 0, 0, 0.99);
          const te = clamp(params.trimEnd ?? 1, ts + 0.005, 1);
          t.trimStart = ts;
          t.trimEnd = te;
          t.fadeInMs = clamp(params.fadeInMs ?? 0, 0, 2000);
          t.fadeOutMs = clamp(params.fadeOutMs ?? 0, 0, 5000);
        }
        break;
      }
      case 'preview': {
        this.startLayer(msg.layer, true);
        break;
      }
      case 'triggerAll': {
        this.triggerAllLayers();
        break;
      }
      case 'setMonitor': {
        const prevMode = this.captureMode;
        const prevBars = this.barsIdx;
        const prevTime = this.timeMs;
        if (msg.capture !== undefined) this.captureMode = clamp(msg.capture, 0, 2);
        if (msg.bars !== undefined) this.barsIdx = clamp(msg.bars, 0, 6);
        if (msg.timeMs !== undefined) this.timeMs = clamp(msg.timeMs, 1, 5000);
        if (msg.freeze !== undefined) this.frozen = !!msg.freeze;
        if (msg.monitorActive !== undefined) this.monitorActive = !!msg.monitorActive;

        const changed =
          this.captureMode !== prevMode ||
          (this.captureMode === 0 ? this.barsIdx !== prevBars : this.timeMs !== prevTime);
        if (changed) {
          this.resetCapture(true);
          this.tempoRunning = false;
        }
        break;
      }
      case 'reset': {
        this.resetCapture(true);
        this.tempoRunning = false;
        this.peakHold = 0;
        this.rmsHold = 0;
        this.crestHold = 1;
        break;
      }
    }
  }

  triggerAllLayers() {
    for (let i = 0; i < NUM_LAYERS; i++) {
      this.startLayer(i, false);
    }
  }

  isLayerAudible(layer) {
    const p = this.params[layer];
    if (p.muted) return false;
    let anySolo = false;
    for (let i = 0; i < NUM_LAYERS; i++) {
      if (this.params[i].solo) {
        anySolo = true;
        break;
      }
    }
    return !anySolo || p.solo;
  }

  startLayer(layer, ignoreTimingShift) {
    if (layer < 0 || layer >= NUM_LAYERS) return;
    const sample = this.samples[layer];
    if (!sample) return;
    const p = this.params[layer];
    if (!ignoreTimingShift && !this.isLayerAudible(layer)) return;
    if (ignoreTimingShift && p.muted) return;

    const layerVoices = this.voices[layer];
    let selectedVoice = -1;
    for (let offset = 0; offset < VOICES_PER_LAYER; offset++) {
      const candidate = (this.nextVoiceToSteal[layer] + offset) % VOICES_PER_LAYER;
      if (!layerVoices[candidate].active) {
        selectedVoice = candidate;
        break;
      }
    }
    if (selectedVoice < 0) {
      selectedVoice = this.nextVoiceToSteal[layer];
    }
    this.nextVoiceToSteal[layer] = (selectedVoice + 1) % VOICES_PER_LAYER;

    const voice = layerVoices[selectedVoice];
    voice.sample = sample;
    voice.position = 0;
    const shiftSamples = ignoreTimingShift
      ? 0
      : Math.round(p.delayShiftMs * this.sr * 0.001);
    voice.delayRemaining = ignoreTimingShift
      ? 0
      : clamp(this.maximumDelaySamples + shiftSamples, 0, this.maximumDelaySamples * 2);
    voice.ageSamples = 0;
    voice.releaseStartLevel = 1.0;
    voice.inRelease = false;
    voice.active = true;
  }

  applyPowerTension(u, tension) {
    const t = clamp(u, 0, 1);
    const ten = clamp(tension, -1, 1);
    if (Math.abs(ten) < 1e-4) return t;
    const exp = Math.pow(2, Math.abs(ten) * 3.0);
    return ten >= 0 ? 1 - Math.pow(1 - t, exp) : Math.pow(t, exp);
  }

  flSegmentLevel(progress, y0, y1, mode, tension) {
    const t = clamp(progress, 0, 1);
    const ten = clamp(tension, -1, 1);
    const m = clamp(Math.round(mode), 0, 7);
    const dirTen = y1 >= y0 ? ten : -ten;

    let frac = t;
    switch (m) {
      case 0:
        frac = t >= 1 ? 1 : 0;
        break;
      case 1: {
        const s = this.applyPowerTension(t, dirTen);
        frac = s * s * (3 - 2 * s);
        break;
      }
      case 2:
        frac = this.applyPowerTension(t, dirTen);
        break;
      case 3: {
        const hs = 0.5 - 0.5 * Math.cos(Math.PI * t);
        frac = this.applyPowerTension(hs, dirTen);
        break;
      }
      case 4: {
        const steps = Math.max(2, Math.round(2 + (ten + 1) * 6));
        frac = t >= 1 ? 1 : Math.floor(t * steps) / steps;
        break;
      }
      case 5: {
        const steps = Math.max(2, Math.round(2 + (ten + 1) * 6));
        if (t >= 1) frac = 1;
        else {
          const scaled = t * steps;
          const idx = Math.floor(scaled);
          const f = scaled - idx;
          frac = (idx + f * f * (3 - 2 * f)) / steps;
        }
        break;
      }
      case 6: {
        const cycles = Math.max(1, Math.round(1 + (ten + 1) * 5));
        frac = t >= 1 ? 1 : ((t * cycles) % 1) < 0.5 ? 0 : 1;
        break;
      }
      case 7: {
        const cycles = Math.max(1, Math.round(1 + (ten + 1) * 5));
        frac = 0.5 * (1 - Math.cos(Math.PI * (2 * cycles + 1) * t));
        break;
      }
      default:
        frac = this.applyPowerTension(t, dirTen);
        break;
    }
    return y0 + (y1 - y0) * frac;
  }

  computeEnvelopeSchedule(p, totalDurationMs) {
    const dur = Math.max(1, totalDurationMs);
    const a = Math.max(0.1, p.attackMs);
    const h = Math.max(0, p.holdMs);
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
    return {
      attackEndMs,
      holdEndMs,
      decayEndMs,
      releaseStartMs,
      releaseMs: Math.max(0.1, dur - releaseStartMs),
    };
  }

  envelopeLevelAtMs(p, sched, ageMs) {
    if (!p.envEnabled) return 1.0;
    const attSpan = Math.max(0.05, sched.attackEndMs);
    if (ageMs < sched.attackEndMs) {
      return this.flSegmentLevel(ageMs / attSpan, 0, 1, p.attackCurve, p.attackTension);
    }
    if (ageMs < sched.holdEndMs) {
      return 1.0;
    }
    const decSpan = Math.max(0.05, sched.decayEndMs - sched.holdEndMs);
    if (ageMs < sched.decayEndMs) {
      return this.flSegmentLevel(
        (ageMs - sched.holdEndMs) / decSpan,
        1.0,
        p.sustain,
        p.decayCurve,
        p.decayTension
      );
    }
    return p.sustain;
  }

  renderLayerSample(layer) {
    let left = 0;
    let right = 0;
    const p = this.params[layer];
    const layerVoices = this.voices[layer];

    if (!this.isLayerAudible(layer)) {
      for (let i = 0; i < VOICES_PER_LAYER; i++) {
        layerVoices[i].active = false;
      }
    } else {
      const leftPan = p.pan > 0 ? 1 - p.pan : 1;
      const rightPan = p.pan < 0 ? 1 + p.pan : 1;

      for (let v = 0; v < VOICES_PER_LAYER; v++) {
        const voice = layerVoices[v];
        if (!voice.active || !voice.sample) {
          voice.active = false;
          continue;
        }
        if (voice.delayRemaining > 0) {
          voice.delayRemaining--;
          continue;
        }

        const sample = voice.sample;
        const length = sample.length;
        if (length <= 1) {
          voice.active = false;
          continue;
        }

        const trimStartIdx = clamp(Math.floor(p.trimStart * length), 0, length - 2);
        const trimEndIdx = clamp(Math.ceil(p.trimEnd * length), trimStartIdx + 1, length);
        const trimmedLength = trimEndIdx - trimStartIdx;

        if (trimmedLength <= 0 || voice.position >= trimmedLength) {
          voice.active = false;
          continue;
        }

        const u = clamp(voice.position / trimmedLength, 0, 1);
        const pogoSemitones = p.pogo * 24.0 * Math.pow(1 - u, 2.5);
        const speed =
          (sample.sourceSampleRate / this.sr) *
          Math.pow(2, (p.pitchSemitones + pogoSemitones) / 12);

        const samplePos = p.reverse
          ? trimEndIdx - 1 - voice.position
          : trimStartIdx + voice.position;

        const index0 = clamp(Math.floor(samplePos), trimStartIdx, trimEndIdx - 1);
        const index1 = p.reverse
          ? Math.max(trimStartIdx, index0 - 1)
          : Math.min(trimEndIdx - 1, index0 + 1);
        const frac = p.reverse ? index0 - samplePos : samplePos - index0;

        const rawL =
          sample.left[index0] + (sample.left[index1] - sample.left[index0]) * frac;
        const rawR =
          sample.right[index0] + (sample.right[index1] - sample.right[index0]) * frac;

        const safeSpeed = Math.max(0.0001, speed);
        const totalOutputSamples = trimmedLength / safeSpeed;
        const elapsedSamples = voice.ageSamples;
        const remainingSamples = (trimmedLength - voice.position) / safeSpeed;
        const totalDurationMs = (totalOutputSamples / this.sr) * 1000.0;
        const elapsedMs = (elapsedSamples / this.sr) * 1000.0;
        const remainingMs = (remainingSamples / this.sr) * 1000.0;

        let env = 1.0;
        if (p.envEnabled) {
          const sched = this.computeEnvelopeSchedule(p, totalDurationMs);
          if (remainingMs <= sched.releaseMs) {
            if (!voice.inRelease) {
              voice.releaseStartLevel = this.envelopeLevelAtMs(p, sched, elapsedMs);
              voice.inRelease = true;
            }
            const releaseProgress = clamp(
              1.0 - remainingMs / Math.max(0.1, sched.releaseMs),
              0,
              1
            );
            env = this.flSegmentLevel(
              releaseProgress,
              voice.releaseStartLevel,
              0,
              p.releaseCurve,
              p.releaseTension
            );
          } else {
            env = this.envelopeLevelAtMs(p, sched, elapsedMs);
          }
        }

        const fadeInSamples = p.fadeInMs * 0.001 * this.sr;
        const fadeOutSamples = p.fadeOutMs * 0.001 * this.sr;
        const fadeInGain = fadeInSamples > 0.5 ? clamp(elapsedSamples / fadeInSamples, 0, 1) : 1;
        const fadeOutGain = fadeOutSamples > 0.5 ? clamp(remainingSamples / fadeOutSamples, 0, 1) : 1;

        const gain = p.gain * env * fadeInGain * fadeOutGain;
        left += rawL * gain * leftPan;
        right += rawR * gain * rightPan;

        voice.position += speed;
        voice.ageSamples++;
        if (voice.position >= trimmedLength) {
          voice.active = false;
        }
      }
    }

    left = this.lowPass[layer].processLeft(this.highPass[layer].processLeft(left));
    right = this.lowPass[layer].processRight(this.highPass[layer].processRight(right));
    return [left, right];
  }

  resetCapture(clearHeld) {
    this.needInit = true;
    this.havePass = false;
    this.filledCount = 0;
    this.colF = 0;
    this.curCol = 0;
    this.passStart = 0;
    if (clearHeld) {
      this.heldValid = false;
      this.fftSamplesSinceUpdate = 0;
      this.fftSamplesCollected = 0;
      this.fftWritePos.fill(0);
      this.spectrumDb.fill(-120);
      this.heldSpectrumDb.fill(-120);
    }
  }

  copyRangeToHeld(from, to) {
    const f = clamp(from, 0, SCOPE_COLS - 1);
    const t = clamp(to, 0, SCOPE_COLS - 1);
    if (t < f) return;
    for (let ch = 0; ch < NUM_LAYERS; ch++) {
      const base = ch * SCOPE_COLS;
      for (let c = f; c <= t; c++) {
        this.heldMin[base + c] = this.colMin[base + c];
        this.heldMax[base + c] = this.colMax[base + c];
      }
    }
    this.heldFrom = f;
    this.heldTo = t;
    this.heldValid = true;
    this.heldSpectrumDb.set(this.spectrumDb);
  }

  copyPassToHeld() {
    if (this.captureMode === 2 || !this.havePass) return;
    if (this.curCol - this.passStart >= 8) {
      this.copyRangeToHeld(this.passStart, this.curCol);
    }
  }

  startNewPass(copyToHeld, startColumn) {
    if (copyToHeld) this.copyPassToHeld();
    this.needInit = true;
    this.havePass = false;
    this.filledCount = 0;
    this.colF = clamp(startColumn, 0, SCOPE_COLS - 0.001);
    this.curCol = Math.floor(this.colF);
    this.passStart = this.curCol;
  }

  onWrap() {
    if (this.captureMode !== 2 && this.havePass) {
      this.copyRangeToHeld(this.passStart, SCOPE_COLS - 1);
    }
    this.passStart = 0;
  }

  initColumn(col, v) {
    for (let ch = 0; ch < NUM_LAYERS; ch++) {
      const idx = ch * SCOPE_COLS + col;
      this.colMin[idx] = v[ch];
      this.colMax[idx] = v[ch];
    }
  }

  updateColumn(col, v) {
    for (let ch = 0; ch < NUM_LAYERS; ch++) {
      const idx = ch * SCOPE_COLS + col;
      if (v[ch] < this.colMin[idx]) this.colMin[idx] = v[ch];
      if (v[ch] > this.colMax[idx]) this.colMax[idx] = v[ch];
    }
  }

  pushSample(v) {
    if (this.needInit) {
      this.initColumn(this.curCol, v);
      this.needInit = false;
      this.havePass = true;
      this.filledCount = 1;
      return;
    }

    this.colF += this.colsPerSample;
    let target = Math.floor(this.colF);
    if (target === this.curCol) {
      this.updateColumn(this.curCol, v);
      return;
    }

    for (let c = this.curCol + 1; c <= target; c++) {
      let cc = c;
      if (cc >= SCOPE_COLS) {
        if (cc === SCOPE_COLS) this.onWrap();
        cc -= SCOPE_COLS;
      }
      this.initColumn(cc, v);
      if (this.filledCount < SCOPE_COLS) {
        this.filledCount++;
      }
    }

    if (target >= SCOPE_COLS) {
      target -= SCOPE_COLS;
      this.colF -= SCOPE_COLS;
    }
    this.curCol = target;
  }

  pushSpectrumSamples(v) {
    for (let s = 0; s < NUM_LAYERS; s++) {
      const pos = this.fftWritePos[s];
      this.fftRing[s][pos] = v[s];
      this.fftWritePos[s] = (pos + 1) % FFT_SIZE;
    }
    this.fftSamplesCollected = Math.min(FFT_SIZE, this.fftSamplesCollected + 1);
    if (++this.fftSamplesSinceUpdate >= SPECTRUM_HOP) {
      this.fftSamplesSinceUpdate = 0;
      if (this.fftSamplesCollected === FFT_SIZE) {
        this.processSpectrumFrame();
      }
    }
  }

  processSpectrumFrame() {
    const scale = 2.0 / Math.max(this.fftWindowSum, 1.0);
    const scratch = this.fftScratch;
    const win = this.fftWindow;

    for (let s = 0; s < NUM_LAYERS; s++) {
      if (!this.samples[s]) continue;
      const ring = this.fftRing[s];
      const oldest = this.fftWritePos[s];
      for (let n = 0; n < FFT_SIZE; n++) {
        scratch[n] = ring[(oldest + n) % FFT_SIZE] * win[n];
      }
      const mag = this.fft.computeMagnitudes(scratch);
      const specOffset = s * SPECTRUM_BINS;
      for (let b = 0; b < SPECTRUM_BINS; b++) {
        const lo = this.spectrumBinLo[b];
        const m = this.spectrumBinMix[b];
        const amp = (mag[lo] * (1 - m) + mag[lo + 1] * m) * scale;
        const db = amp > 1e-6 ? 20 * Math.log10(amp) : -120;
        const old = this.spectrumDb[specOffset + b];
        this.spectrumDb[specOffset + b] =
          old + (db - old) * (db > old ? 0.68 : 0.20);
      }
    }
  }

  process(inputs, outputs) {
    const output = outputs[0];
    if (!output || output.length === 0) return true;
    const outL = output[0];
    const outR = output.length > 1 ? output[1] : outL;
    const numSamples = outL.length;

    const input = inputs[0];
    const hasInput = input && input.length > 0;
    const inL = hasInput ? input[0] : null;
    const inR = hasInput && input.length > 1 ? input[1] : inL;

    if (this.frozen) {
      this.wasFrozen = true;
      this.tempoRunning = false;
    } else if (this.wasFrozen) {
      this.wasFrozen = false;
      if (this.captureMode !== 0) {
        this.startNewPass(true, 0);
      }
    }

    let capturing = false;
    if (!this.frozen) {
      if (this.captureMode !== 0) {
        capturing = true;
        const windowSamples = Math.max(1, this.timeMs * 0.001 * this.sr);
        this.colsPerSample = clamp(SCOPE_COLS / windowSamples, 1e-6, SCOPE_COLS * 0.5);
      } else {
        // Tempo mode synchronized to continuous 120 BPM internal clock
        const ppq = (this.totalSamples * (this.bpm / 60)) / this.sr;
        const windowQuarters = BARS_TABLE[this.barsIdx] * 4.0;
        const startCol = ((ppq % windowQuarters) / windowQuarters) * SCOPE_COLS;
        this.colsPerSample = clamp(
          (SCOPE_COLS * (this.bpm / 60 / this.sr)) / windowQuarters,
          1e-6,
          SCOPE_COLS * 0.5
        );
        capturing = true;
        if (!this.tempoRunning) {
          this.startNewPass(true, startCol);
          this.tempoRunning = true;
        } else {
          let diff = startCol - this.colF;
          if (diff < -SCOPE_COLS * 0.5) diff += SCOPE_COLS;
          else if (diff > SCOPE_COLS * 0.5) diff -= SCOPE_COLS;
          if (Math.abs(diff) <= 4.0) {
            this.colF = Math.max(this.colF + diff, this.curCol);
          } else {
            this.startNewPass(true, startCol);
          }
        }
      }
    }

    const delayLen = this.delayLeft.length;
    const audioEnabled = this.triggerMode === 0 || this.triggerMode === 2;
    const layerVals = this.layerValues;

    for (let i = 0; i < numSamples; i++) {
      const sampleInL = inL ? inL[i] : 0;
      const sampleInR = inR ? inR[i] : 0;
      const inputLevel = Math.max(Math.abs(sampleInL), Math.abs(sampleInR));

      if (
        audioEnabled &&
        this.refractoryRemaining <= 0 &&
        inputLevel >= this.threshold &&
        this.previousInputLevel < this.threshold
      ) {
        this.triggerAllLayers();
        this.refractoryRemaining = this.refractorySamples;
      }
      this.previousInputLevel = inputLevel;
      if (this.refractoryRemaining > 0) this.refractoryRemaining--;

      this.delayLeft[this.delayWritePos] = sampleInL;
      this.delayRight[this.delayWritePos] = sampleInR;
      const readPos =
        (this.delayWritePos + delayLen - this.maximumDelaySamples) % delayLen;
      const dryL = this.delayLeft[readPos];
      const dryR = this.delayRight[readPos];
      this.delayWritePos = (this.delayWritePos + 1) % delayLen;

      let masterL = dryL;
      let masterR = dryR;

      for (let layer = 0; layer < NUM_LAYERS; layer++) {
        const [lL, lR] = this.renderLayerSample(layer);
        masterL += lL;
        masterR += lR;
        layerVals[layer] = Math.abs(lL) >= Math.abs(lR) ? lL : lR;
      }

      // Meter tracks rendered output/input peak when standalone, or incoming trigger if present
      const meterSig =
        inputLevel > 1e-6
          ? inputLevel
          : Math.abs(masterL) >= Math.abs(masterR)
            ? masterL
            : masterR;
      this.peakEnv = Math.max(Math.abs(meterSig), this.peakEnv * this.peakDecay);
      this.meanSquare += this.msAlpha * (meterSig * meterSig - this.meanSquare);

      if (capturing) {
        this.pushSpectrumSamples(layerVals);
        this.pushSample(layerVals);
      }

      outL[i] = masterL;
      if (outR !== outL) outR[i] = masterR;
      this.totalSamples++;
    }

    const rms = Math.sqrt(Math.max(0, this.meanSquare));
    const crest = clamp(this.peakEnv / Math.max(rms, 1e-6), 1, 1000);
    this.peakNow = this.peakEnv;
    this.rmsNow = rms;
    this.crestNow = crest;
    if (this.peakEnv > this.peakHold) this.peakHold = this.peakEnv;
    if (rms > this.rmsHold) this.rmsHold = rms;
    if (rms > 1e-4 && crest > this.crestHold) this.crestHold = crest;

    if (this.monitorActive && ++this.blocksSincePost >= 11) {
      this.blocksSincePost = 0;
      this.port.postMessage({
        type: 'snapshot',
        colMin: this.colMin.slice(),
        colMax: this.colMax.slice(),
        heldMin: this.heldMin.slice(),
        heldMax: this.heldMax.slice(),
        spectrumDb: this.spectrumDb.slice(),
        heldSpectrumDb: this.heldSpectrumDb.slice(),
        head: this.curCol,
        passStart: this.passStart,
        filled: this.filledCount,
        hasData: this.havePass,
        heldFrom: this.heldFrom,
        heldTo: this.heldTo,
        heldValid: this.heldValid,
        peakNow: this.peakNow,
        rmsNow: this.rmsNow,
        crestNow: this.crestNow,
        peakHold: this.peakHold,
        rmsHold: this.rmsHold,
        crestHold: this.crestHold,
      });
    }

    return true;
  }
}

registerProcessor('wavelens-processor', WaveLensAudioWorklet);
