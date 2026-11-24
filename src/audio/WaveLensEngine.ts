import { decodeAudioFile } from './curves';
import {
  GlobalMonitorParams,
  LayerParameters,
  MonitorSnapshot,
  NUM_LAYERS,
  WaveformPreview,
  createDefaultLayerParams,
  createEmptySnapshot,
} from './types';

interface DecodedSample {
  left: Float32Array;
  right: Float32Array;
  sampleRate: number;
  preview: WaveformPreview;
  displayName: string;
}

export class WaveLensAudioEngine {
  private ctx: AudioContext | null = null;
  private workletNode: AudioWorkletNode | null = null;
  private initPromise: Promise<void> | null = null;
  private samples: Array<DecodedSample | null> = new Array(NUM_LAYERS).fill(null);
  private layerParams: LayerParameters[] = Array.from(
    { length: NUM_LAYERS },
    (_, i) => createDefaultLayerParams(i)
  );
  private monitorActive = false;
  private latestSnapshot: MonitorSnapshot = createEmptySnapshot();
  private snapshotListeners = new Set<(snap: MonitorSnapshot) => void>();
  private micStream: MediaStream | null = null;
  private micSource: MediaStreamAudioSourceNode | null = null;

  public async ensureInitialized(): Promise<AudioContext> {
    if (this.ctx && this.workletNode) {
      if (this.ctx.state === 'suspended') {
        await this.ctx.resume();
      }
      return this.ctx;
    }

    if (!this.initPromise) {
      this.initPromise = this.initInternal();
    }
    await this.initPromise;
    if (this.ctx && this.ctx.state === 'suspended') {
      await this.ctx.resume();
    }
    return this.ctx!;
  }

  private async initInternal(): Promise<void> {
    const AudioCtx =
      window.AudioContext ||
      (window as unknown as { webkitAudioContext: typeof AudioContext })
        .webkitAudioContext;
    const ctx = new AudioCtx();
    await ctx.audioWorklet.addModule('/wavelens-worklet.js');

    const node = new AudioWorkletNode(ctx, 'wavelens-processor', {
      numberOfInputs: 1,
      numberOfOutputs: 1,
      outputChannelCount: [2],
    });
    node.connect(ctx.destination);

    node.port.onmessage = (e) => {
      const data = e.data;
      if (data?.type === 'snapshot') {
        this.latestSnapshot = data as MonitorSnapshot;
        for (const cb of this.snapshotListeners) {
          cb(this.latestSnapshot);
        }
      }
    };

    // Sync any already-loaded samples & params
    for (let i = 0; i < NUM_LAYERS; i++) {
      node.port.postMessage({
        type: 'setLayerParams',
        layer: i,
        params: this.layerParams[i],
      });
      const sample = this.samples[i];
      if (sample) {
        node.port.postMessage({
          type: 'loadSample',
          layer: i,
          left: sample.left,
          right: sample.right,
          sampleRate: sample.sampleRate,
        });
      }
    }
    node.port.postMessage({
      type: 'setMonitor',
      monitorActive: this.monitorActive,
    });

    this.ctx = ctx;
    this.workletNode = node;
  }

  public async loadSampleFile(
    layer: number,
    file: File
  ): Promise<{ preview: WaveformPreview; displayName: string }> {
    const ctx = await this.ensureInitialized();
    const decoded = await decodeAudioFile(ctx, file);
    this.samples[layer] = decoded;
    this.workletNode?.port.postMessage({
      type: 'loadSample',
      layer,
      left: decoded.left,
      right: decoded.right,
      sampleRate: decoded.sampleRate,
    });
    return {
      preview: decoded.preview,
      displayName: decoded.displayName,
    };
  }

  public clearSample(layer: number): void {
    this.samples[layer] = null;
    this.workletNode?.port.postMessage({
      type: 'clearSample',
      layer,
    });
  }

  public setLayerParameters(layer: number, params: LayerParameters): void {
    this.layerParams[layer] = { ...params };
    this.workletNode?.port.postMessage({
      type: 'setLayerParams',
      layer,
      params: this.layerParams[layer],
    });
  }

  public async requestPreview(layer: number): Promise<void> {
    await this.ensureInitialized();
    this.workletNode?.port.postMessage({
      type: 'preview',
      layer,
    });
  }

  public async triggerAll(): Promise<void> {
    await this.ensureInitialized();
    this.workletNode?.port.postMessage({
      type: 'triggerAll',
    });
  }

  public setMonitorParams(
    params: Pick<GlobalMonitorParams, 'capture' | 'bars' | 'timeMs' | 'freeze'>,
    monitorActive: boolean
  ): void {
    this.monitorActive = monitorActive;
    this.workletNode?.port.postMessage({
      type: 'setMonitor',
      capture: params.capture,
      bars: params.bars,
      timeMs: params.timeMs,
      freeze: params.freeze,
      monitorActive,
    });
  }

  public requestReset(): void {
    this.latestSnapshot = createEmptySnapshot();
    for (const cb of this.snapshotListeners) {
      cb(this.latestSnapshot);
    }
    this.workletNode?.port.postMessage({ type: 'reset' });
  }

  public getLatestSnapshot(): MonitorSnapshot {
    return this.latestSnapshot;
  }

  public subscribeSnapshot(cb: (snap: MonitorSnapshot) => void): () => void {
    this.snapshotListeners.add(cb);
    return () => {
      this.snapshotListeners.delete(cb);
    };
  }

  public async toggleAudioTriggerInput(enable: boolean): Promise<boolean> {
    if (!enable) {
      if (this.micSource) {
        this.micSource.disconnect();
        this.micSource = null;
      }
      if (this.micStream) {
        this.micStream.getTracks().forEach((t) => t.stop());
        this.micStream = null;
      }
      return false;
    }

    const ctx = await this.ensureInitialized();
    if (!navigator.mediaDevices?.getUserMedia) {
      throw new Error('Microphone audio input is not available in this browser.');
    }
    const stream = await navigator.mediaDevices.getUserMedia({
      audio: {
        echoCancellation: false,
        noiseSuppression: false,
        autoGainControl: false,
      },
    });
    const source = ctx.createMediaStreamSource(stream);
    if (this.workletNode) {
      source.connect(this.workletNode);
    }
    this.micStream = stream;
    this.micSource = source;
    return true;
  }

  public dispose(): void {
    if (this.micSource) this.micSource.disconnect();
    if (this.micStream) this.micStream.getTracks().forEach((t) => t.stop());
    if (this.workletNode) this.workletNode.disconnect();
    if (this.ctx) void this.ctx.close();
    this.ctx = null;
    this.workletNode = null;
  }
}
