import { WebAudioSampleEngine } from './WebAudioSampleEngine';

/**
 * Generates clean procedural transient and snapback AudioBuffers so users
 * can immediately audition the 6-layer PRE/MAIN engine without uploading local WAVs.
 */
export function loadDemoKitIntoEngine(engine: WebAudioSampleEngine): void {
  const ctx = engine.ensureContext();
  const sr = ctx.sampleRate;

  // Layer 0 (PRE 1): Reverse Snapback Breath / Pre-Suck (120 ms)
  {
    const len = Math.floor(sr * 0.14);
    const buf = ctx.createBuffer(2, len, sr);
    const l = buf.getChannelData(0);
    const r = buf.getChannelData(1);
    let seed = 1337;
    const rand = () => {
      seed = (seed * 16807) % 2147483647;
      return (seed - 1) / 1073741823.5 - 1.0;
    };
    let lp = 0;
    for (let i = 0; i < len; i++) {
      const u = i / len;
      const env = Math.pow(u, 3.2) * (1.0 - Math.pow(u, 18.0));
      lp = lp * 0.72 + rand() * 0.28;
      const sweep = Math.sin(2 * Math.PI * (90 + 480 * u * u) * (i / sr));
      const val = (lp * 0.55 + sweep * 0.35) * env * 0.75;
      l[i] = val;
      r[i] = val * 0.96;
    }
    engine.setSampleBuffer(0, buf, 'Demo_Snapback_Suck');
  }

  // Layer 1 (PRE 2): Crisp Pre-Click Flam (45 ms)
  {
    const len = Math.floor(sr * 0.055);
    const buf = ctx.createBuffer(2, len, sr);
    const l = buf.getChannelData(0);
    const r = buf.getChannelData(1);
    for (let i = 0; i < len; i++) {
      const t = i / sr;
      const u = i / len;
      const env = Math.pow(u, 4.0) * Math.exp(-Math.max(0, u - 0.85) * 28.0);
      const tone = Math.sin(2 * Math.PI * (800 + 2200 * u) * t);
      const val = tone * env * 0.55;
      l[i] = val;
      r[i] = val;
    }
    engine.setSampleBuffer(1, buf, 'Demo_Pre_Flam');
  }

  // Layer 3 (MAIN 1): Punchy Sub-Kick Transient (340 ms)
  {
    const len = Math.floor(sr * 0.36);
    const buf = ctx.createBuffer(2, len, sr);
    const l = buf.getChannelData(0);
    const r = buf.getChannelData(1);
    let phase = 0;
    for (let i = 0; i < len; i++) {
      const t = i / sr;
      const freq = 48 + 185 * Math.exp(-t / 0.022);
      phase += (2 * Math.PI * freq) / sr;
      const body = Math.sin(phase) * Math.exp(-t / 0.115);
      const click = Math.sin(2 * Math.PI * 1900 * t) * Math.exp(-t / 0.004) * 0.45;
      const val = Math.tanh((body + click) * 1.15) * 0.85;
      l[i] = val;
      r[i] = val;
    }
    engine.setSampleBuffer(3, buf, 'Demo_Punch_Kick');
  }

  // Layer 4 (MAIN 2): Stereo Snap / Top Crack (220 ms)
  {
    const len = Math.floor(sr * 0.24);
    const buf = ctx.createBuffer(2, len, sr);
    const l = buf.getChannelData(0);
    const r = buf.getChannelData(1);
    let seed = 4242;
    const rand = () => {
      seed = (seed * 16807) % 2147483647;
      return (seed - 1) / 1073741823.5 - 1.0;
    };
    for (let i = 0; i < len; i++) {
      const t = i / sr;
      const env = Math.exp(-t / 0.045);
      const transient = Math.sin(2 * Math.PI * 240 * t) * Math.exp(-t / 0.015) * 0.5;
      l[i] = (rand() * 0.55 * env + transient) * 0.7;
      r[i] = (rand() * 0.55 * env + transient) * 0.7;
    }
    engine.setSampleBuffer(4, buf, 'Demo_Top_Snap');
  }
}
