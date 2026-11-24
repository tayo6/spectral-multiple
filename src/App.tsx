import React, { useCallback, useEffect, useRef, useState } from 'react';
import { LayerEnvelopeComponent } from './components/LayerEnvelopeComponent';
import { LayerOverlayView } from './components/LayerOverlayView';
import { MonitorView } from './components/MonitorView';
import { loadDemoKitIntoEngine } from './engine/demoSamples';
import { clamp, formatDb } from './engine/envelopeMath';
import {
  createDefaultGlobalState,
  createDefaultLayerParameters,
  GlobalState,
  LayerParameters,
  MAX_PRE_ROLL_MS,
  MeterMode,
  NUM_LAYERS,
  SIGNAL_COLORS,
} from './engine/types';
import { MeterSnapshot, WebAudioSampleEngine } from './engine/WebAudioSampleEngine';

const STORAGE_KEY = 'wavelens_spectral_multiple_state_v2';

function loadPersistedState(): {
  global: GlobalState;
  layers: LayerParameters[];
} {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (raw) {
      const parsed = JSON.parse(raw) as {
        global?: Partial<GlobalState>;
        layers?: Array<Partial<LayerParameters>>;
      };
      const global = { ...createDefaultGlobalState(), ...(parsed.global ?? {}) };
      const layers = Array.from({ length: NUM_LAYERS }, (_, i) => ({
        ...createDefaultLayerParameters(),
        ...(parsed.layers?.[i] ?? {}),
      }));
      return { global, layers };
    }
  } catch {
    // ignore corrupted storage
  }
  return {
    global: createDefaultGlobalState(),
    layers: Array.from({ length: NUM_LAYERS }, () => createDefaultLayerParameters()),
  };
}

export const App: React.FC = () => {
  const initial = useRef(loadPersistedState());
  const engineRef = useRef<WebAudioSampleEngine | null>(null);
  if (!engineRef.current) {
    engineRef.current = new WebAudioSampleEngine();
  }
  const engine = engineRef.current;

  const [globalState, setGlobalState] = useState<GlobalState>(initial.current.global);
  const [layerParams, setLayerParams] = useState<LayerParameters[]>(initial.current.layers);
  const [sampleVersion, setSampleVersion] = useState(0);
  const [overlayZoom, setOverlayZoom] = useState(1.0);
  const [overlayViewStart, setOverlayViewStart] = useState(0.0);
  const [errorBanner, setErrorBanner] = useState<string | null>(null);
  const [meterSnapshot, setMeterSnapshot] = useState<MeterSnapshot>({
    peakNow: 0,
    rmsNow: 0,
    crestNow: 1,
    peakHold: 0,
    rmsHold: 0,
    crestHold: 1,
  });

  const fileInputRef = useRef<HTMLInputElement>(null);
  const pendingLoadLayerRef = useRef<number>(0);

  // Keep latest params in ref for MIDI / keyboard callbacks
  const layerParamsRef = useRef(layerParams);
  layerParamsRef.current = layerParams;
  const globalStateRef = useRef(globalState);
  globalStateRef.current = globalState;

  // Persist parameter state
  useEffect(() => {
    try {
      localStorage.setItem(
        STORAGE_KEY,
        JSON.stringify({ global: globalState, layers: layerParams })
      );
    } catch {
      // ignore quota errors
    }
  }, [globalState, layerParams]);

  // Sync Web Audio node parameters in real time
  useEffect(() => {
    for (let i = 0; i < NUM_LAYERS; i++) {
      const audible = engine.isLayerAudible(i, layerParams);
      engine.syncLayerNodes(i, layerParams[i], audible);
    }
  }, [engine, layerParams]);

  // Live master meter polling
  useEffect(() => {
    const id = window.setInterval(() => {
      setMeterSnapshot(engine.readMasterMeter());
    }, 33);
    return () => window.clearInterval(id);
  }, [engine]);

  const handleTrigger = useCallback(() => {
    engine.triggerAllLayers(layerParamsRef.current, globalStateRef.current.preRollMs);
  }, [engine]);

  // Spacebar manual trigger + Web MIDI Note-On trigger
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (
        e.code === 'Space' &&
        !(e.target instanceof HTMLInputElement) &&
        !(e.target instanceof HTMLSelectElement) &&
        !(e.target instanceof HTMLTextAreaElement)
      ) {
        e.preventDefault();
        handleTrigger();
      }
    };
    window.addEventListener('keydown', onKeyDown);

    let midiAccess: MIDIAccess | null = null;
    if (typeof navigator !== 'undefined' && 'requestMIDIAccess' in navigator) {
      navigator
        .requestMIDIAccess()
        .then((access) => {
          midiAccess = access;
          const attach = () => {
            access.inputs.forEach((input) => {
              input.onmidimessage = (msg: MIDIMessageEvent) => {
                const data = msg.data;
                if (!data || data.length < 3) return;
                const status = data[0] & 0xf0;
                const velocity = data[2];
                if (status === 0x90 && velocity > 0) {
                  handleTrigger();
                }
              };
            });
          };
          attach();
          access.onstatechange = attach;
        })
        .catch(() => {
          // MIDI permission denied or unsupported
        });
    }

    return () => {
      window.removeEventListener('keydown', onKeyDown);
      if (midiAccess) {
        midiAccess.inputs.forEach((inp) => {
          inp.onmidimessage = null;
        });
      }
    };
  }, [handleTrigger]);

  const updateLayerParams = useCallback(
    (layer: number, patch: Partial<LayerParameters>) => {
      setLayerParams((prev) =>
        prev.map((lp, idx) => (idx === layer ? { ...lp, ...patch } : lp))
      );
    },
    []
  );

  const loadFileIntoLayer = useCallback(
    async (layer: number, file: File) => {
      const res = await engine.loadSampleFromFile(layer, file);
      if (!res.ok) {
        setErrorBanner(res.error ?? 'Sample load failed.');
        window.setTimeout(() => setErrorBanner(null), 4000);
      } else {
        setErrorBanner(null);
        setSampleVersion((v) => v + 1);
      }
    },
    [engine]
  );

  const promptLoadLayer = (layer: number) => {
    pendingLoadLayerRef.current = layer;
    if (fileInputRef.current) {
      fileInputRef.current.value = '';
      fileInputRef.current.click();
    }
  };

  const handleClearLayer = (layer: number) => {
    engine.clearSample(layer);
    setSampleVersion((v) => v + 1);
  };

  const handleLoadDemoKit = () => {
    loadDemoKitIntoEngine(engine);
    setSampleVersion((v) => v + 1);
  };

  const clampOverlayViewport = (nextStart: number, nextZoom: number) => {
    const z = clamp(nextZoom, 1.0, 32.0);
    const span = 1.0 / z;
    setOverlayZoom(z <= 1.0001 ? 1.0 : z);
    setOverlayViewStart(
      z <= 1.0001 ? 0.0 : clamp(nextStart, 0.0, Math.max(0.0, 1.0 - span))
    );
  };

  const zoomOverlayBy = (factor: number) => {
    const curSpan = 1.0 / Math.max(1.0, overlayZoom);
    const anchor = overlayViewStart + curSpan * 0.5;
    const nextZoom = clamp(overlayZoom * factor, 1.0, 32.0);
    const nextSpan = 1.0 / nextZoom;
    clampOverlayViewport(anchor - 0.5 * nextSpan, nextZoom);
  };

  const focusTriggerOverlay = () => {
    if (overlayZoom > 1.2) {
      clampOverlayViewport(0.0, 1.0);
    } else {
      const nextZoom = 4.0;
      clampOverlayViewport(0.5 - 0.5 / nextZoom, nextZoom);
    }
  };

  const loadedMask = engine.getLoadedMask();
  // Reference sampleVersion so React re-renders cleanly on sample load/clear
  void sampleVersion;

  const zoomLabel =
    overlayZoom <= 1.02
      ? '1.0x'
      : overlayZoom >= 10
        ? `${Math.round(overlayZoom)}x`
        : `${overlayZoom.toFixed(1)}x`;

  const renderDeck = (isSnapback: boolean) => {
    const baseOffset = isSnapback ? 0 : 3;
    const selectedIdx = isSnapback
      ? globalState.selectedSnapback
      : globalState.selectedTransient;
    const title = isSnapback ? 'SNAPBACK' : 'TRANSIENT';
    const titleColor = isSnapback ? '#8ea0ff' : '#cb8cff';
    const sampleInfo = engine.samples[selectedIdx];
    const params = layerParams[selectedIdx];
    const stackNum = isSnapback ? selectedIdx + 1 : selectedIdx - 2;
    const subtitle = sampleInfo.loaded
      ? sampleInfo.displayName
      : `Stack ${stackNum} (Empty)`;

    return (
      <div className="flex-1 min-w-0 h-full bg-[#1a1923] border border-[#2d2b3c] rounded-[5px] flex flex-col overflow-hidden">
        {/* Deck Header (34px) */}
        <div className="h-[34px] shrink-0 bg-[#1f1e2a] border-b border-[#2d2b3c] px-2.5 flex items-center justify-between gap-2">
          <div className="flex items-center gap-2 min-w-0">
            <span
              className="text-[13px] font-bold tracking-wide shrink-0"
              style={{ color: titleColor }}
            >
              {title}
            </span>

            {/* 3 Circle Selectors */}
            <div className="flex items-center gap-1.5 shrink-0">
              {[0, 1, 2].map((slot) => {
                const idx = baseOffset + slot;
                const active = idx === selectedIdx;
                const col = SIGNAL_COLORS[idx];
                const hasSample = engine.samples[idx].loaded;
                return (
                  <button
                    key={idx}
                    type="button"
                    title={`${title} Stack ${slot + 1}`}
                    onClick={() =>
                      setGlobalState((prev) =>
                        isSnapback
                          ? { ...prev, selectedSnapback: idx }
                          : { ...prev, selectedTransient: idx }
                      )
                    }
                    className="w-[14px] h-[14px] rounded-full cursor-pointer transition-transform hover:scale-110 flex items-center justify-center"
                    style={{
                      backgroundColor: col,
                      boxShadow: active
                        ? '0 0 0 2px #1f1e2a, 0 0 0 3.5px rgba(255,255,255,0.85)'
                        : undefined,
                      opacity: hasSample || active ? 1 : 0.65,
                    }}
                  />
                );
              })}
            </div>

            <span className="text-[11px] text-[#9fa4c4] truncate ml-1">
              {subtitle}
            </span>
          </div>

          {/* Right Header Buttons */}
          <div className="flex items-center gap-[3px] shrink-0">
            <button
              type="button"
              onClick={() =>
                setGlobalState((prev) =>
                  isSnapback
                    ? { ...prev, selectedSnapback: (prev.selectedSnapback + 2) % 3 }
                    : {
                        ...prev,
                        selectedTransient: 3 + ((prev.selectedTransient - 3 + 2) % 3),
                      }
                )
              }
              className="h-[20px] w-[20px] text-[11px] bg-[#1b1a26] text-[#9fa4c4] hover:text-[#e6e8f2] border border-[#2d2b3c] rounded-sm cursor-pointer"
            >
              &lt;
            </button>
            <button
              type="button"
              onClick={() =>
                setGlobalState((prev) =>
                  isSnapback
                    ? { ...prev, selectedSnapback: (prev.selectedSnapback + 1) % 3 }
                    : {
                        ...prev,
                        selectedTransient: 3 + ((prev.selectedTransient - 3 + 1) % 3),
                      }
                )
              }
              className="h-[20px] w-[20px] text-[11px] bg-[#1b1a26] text-[#9fa4c4] hover:text-[#e6e8f2] border border-[#2d2b3c] rounded-sm cursor-pointer"
            >
              &gt;
            </button>

            <button
              type="button"
              title="Mute layer"
              onClick={() =>
                updateLayerParams(selectedIdx, { muted: !params.muted })
              }
              style={{
                backgroundColor: params.muted ? '#ff7b00' : '#1b1a26',
                color: params.muted ? '#111118' : '#9fa4c4',
              }}
              className="h-[20px] w-[22px] ml-0.5 text-[10.5px] font-bold border border-[#2d2b3c] rounded-sm cursor-pointer"
            >
              M
            </button>
            <button
              type="button"
              title="Solo layer"
              onClick={() =>
                updateLayerParams(selectedIdx, { solo: !params.solo })
              }
              style={{
                backgroundColor: params.solo ? SIGNAL_COLORS[selectedIdx] : '#1b1a26',
                color: params.solo ? '#111118' : '#9fa4c4',
              }}
              className="h-[20px] w-[22px] text-[10.5px] font-bold border border-[#2d2b3c] rounded-sm cursor-pointer"
            >
              S
            </button>
            <button
              type="button"
              title="Audition layer"
              disabled={!sampleInfo.loaded}
              onClick={() => engine.previewLayer(selectedIdx, layerParams)}
              className="h-[20px] w-[22px] text-[10.5px] font-bold bg-[#1b1a26] text-[#e6e8f2] hover:bg-[#2a2840] border border-[#2d2b3c] rounded-sm disabled:opacity-40 cursor-pointer"
            >
              &gt;
            </button>
            <button
              type="button"
              onClick={() => promptLoadLayer(selectedIdx)}
              className="h-[20px] px-2 text-[10.5px] font-medium bg-[#1b1a26] text-[#e6e8f2] hover:bg-[#2a2840] border border-[#2d2b3c] rounded-sm cursor-pointer"
            >
              {sampleInfo.loaded ? 'Replace' : 'Load...'}
            </button>
            <button
              type="button"
              title="Clear sample"
              disabled={!sampleInfo.loaded}
              onClick={() => handleClearLayer(selectedIdx)}
              className="h-[20px] w-[20px] text-[11px] bg-[#1b1a26] text-[#9fa4c4] hover:text-[#e6e8f2] border border-[#2d2b3c] rounded-sm disabled:opacity-35 cursor-pointer"
            >
              x
            </button>
          </div>
        </div>

        {/* Deck Body */}
        <div className="flex-1 min-h-0 p-1.5">
          <LayerEnvelopeComponent
            layer={selectedIdx}
            params={params}
            sample={sampleInfo}
            onChangeParams={(patch) => updateLayerParams(selectedIdx, patch)}
            onDropAudioFile={(file) => void loadFileIntoLayer(selectedIdx, file)}
          />
        </div>
      </div>
    );
  };

  return (
    <div className="w-screen h-screen bg-[#100f15] text-[#e6e8f2] flex flex-col overflow-hidden">
      {/* Hidden File Input */}
      <input
        ref={fileInputRef}
        type="file"
        accept=".wav,.aif,.aiff,.flac,.ogg,.mp3,.m4a,audio/*"
        className="hidden"
        onChange={(e) => {
          const file = e.target.files?.[0];
          if (file) void loadFileIntoLayer(pendingLoadLayerRef.current, file);
        }}
      />

      {/* Error Toast */}
      {errorBanner && (
        <div className="fixed top-11 left-1/2 -translate-x-1/2 z-50 bg-[#2a1828] border border-[#ff4e42] text-[#ffd6d6] text-xs px-3 py-1.5 rounded shadow-lg">
          {errorBanner}
        </div>
      )}

      {/* Top Header Bar (38px) */}
      <header className="h-[38px] shrink-0 bg-[#15141d] border-b border-[#2d2b3c] px-3 flex items-center justify-between relative">
        <div className="flex items-center gap-1.5 z-10">
          <button
            type="button"
            onClick={() => engine.resetMeters()}
            className="h-[24px] px-2.5 text-[11px] font-medium bg-[#1b1a26] text-[#9fa4c4] hover:text-[#e6e8f2] border border-[#2d2b3c] rounded-sm cursor-pointer"
          >
            Reset
          </button>

          <button
            type="button"
            onClick={handleLoadDemoKit}
            title="Synthesize and load a demo PRE + MAIN kit to audition immediately"
            className="h-[24px] px-2.5 text-[11px] font-medium bg-[#1e1c2e] text-[#c0a6ff] hover:bg-[#2a2742] border border-[#3d3659] rounded-sm cursor-pointer"
          >
            Demo Kit
          </button>

          {globalState.monitorMode && (
            <>
              <button
                type="button"
                onClick={() =>
                  setGlobalState((prev) => ({ ...prev, freeze: !prev.freeze }))
                }
                style={{
                  backgroundColor: globalState.freeze ? '#2a2840' : '#1b1a26',
                  color: globalState.freeze ? '#e6e8f2' : '#9fa4c4',
                }}
                className="h-[24px] px-2.5 text-[11px] font-medium border border-[#2d2b3c] rounded-sm cursor-pointer"
              >
                Freeze
              </button>
              <button
                type="button"
                onClick={() =>
                  setGlobalState((prev) => ({ ...prev, hold: !prev.hold }))
                }
                style={{
                  backgroundColor: globalState.hold ? '#2a2840' : '#1b1a26',
                  color: globalState.hold ? '#e6e8f2' : '#9fa4c4',
                }}
                className="h-[24px] px-2.5 text-[11px] font-medium border border-[#2d2b3c] rounded-sm cursor-pointer"
              >
                Hold
              </button>
            </>
          )}
        </div>

        {/* Centered Gradient Title */}
        <div
          className="absolute inset-0 flex items-center justify-center pointer-events-none font-bold text-[18px] tracking-widest"
        >
          <span className="bg-gradient-to-r from-[#7d92ff] to-[#c77dff] bg-clip-text text-transparent">
            S N A P B A C K
          </span>
        </div>

        {/* Right BPM Controls */}
        <div className="flex items-center gap-1 z-10">
          <button
            type="button"
            onClick={() =>
              setGlobalState((prev) => ({
                ...prev,
                bpm: Math.max(40, prev.bpm - 5),
              }))
            }
            className="h-[22px] w-[22px] text-[12px] font-bold bg-[#1b1a26] text-[#9fa4c4] hover:text-[#e6e8f2] border border-[#2d2b3c] rounded-sm cursor-pointer"
          >
            -
          </button>
          <span className="w-[54px] text-center text-[11px] text-[#e6e8f2] tabular-nums">
            {Math.round(globalState.bpm)} BPM
          </span>
          <button
            type="button"
            onClick={() =>
              setGlobalState((prev) => ({
                ...prev,
                bpm: Math.min(240, prev.bpm + 5),
              }))
            }
            className="h-[22px] w-[22px] text-[12px] font-bold bg-[#1b1a26] text-[#9fa4c4] hover:text-[#e6e8f2] border border-[#2d2b3c] rounded-sm cursor-pointer"
          >
            +
          </button>
        </div>
      </header>

      {/* Top Canvas Area (~47% height) */}
      <div className="h-[44%] min-h-[210px] w-full shrink-0">
        {globalState.monitorMode ? (
          <MonitorView
            engine={engine}
            loadedMask={loadedMask}
            viewMode={globalState.viewMode}
            layoutMode={globalState.layoutMode}
            freeze={globalState.freeze}
            hold={globalState.hold}
            meterMode={globalState.meterMode}
            showSig={globalState.showSig}
            meterSnapshot={meterSnapshot}
            onViewModeChange={(viewMode) =>
              setGlobalState((prev) => ({ ...prev, viewMode }))
            }
            onLayoutModeChange={(layoutMode) =>
              setGlobalState((prev) => ({ ...prev, layoutMode }))
            }
            onToggleSig={(layer) =>
              setGlobalState((prev) => {
                const next = [...prev.showSig] as [
                  boolean,
                  boolean,
                  boolean,
                  boolean,
                  boolean,
                  boolean,
                ];
                next[layer] = !next[layer];
                return { ...prev, showSig: next };
              })
            }
            onCycleMeterMode={() =>
              setGlobalState((prev) => {
                const order: MeterMode[] = ['PEAK', 'RMS', 'CREST'];
                const next = order[(order.indexOf(prev.meterMode) + 1) % 3];
                return { ...prev, meterMode: next };
              })
            }
          />
        ) : (
          <LayerOverlayView
            samples={engine.samples}
            layerParams={layerParams}
            selectedSnapback={globalState.selectedSnapback}
            selectedTransient={globalState.selectedTransient}
            bpm={globalState.bpm}
            preRollMs={globalState.preRollMs}
            gridMode={globalState.gridMode}
            zoomFactor={overlayZoom}
            viewStartNorm={overlayViewStart}
            onViewportChange={clampOverlayViewport}
          />
        )}
      </div>

      {/* Center Divider Bar (34px) */}
      <div className="h-[34px] shrink-0 bg-[#171620] border-y border-[#2d2b3c] px-3 flex items-center justify-between relative">
        {/* Left: Limiter + PRE Lead */}
        <div className="flex items-center gap-3 z-10">
          <span className="text-[11px] text-[#9fa4c4] w-[112px] tabular-nums">
            Limiter &nbsp;{formatDb(meterSnapshot.peakNow)}
          </span>
          <div className="flex items-center gap-1.5">
            <span className="text-[10.5px] font-bold text-[#8ea0ff]">PRE</span>
            <input
              type="range"
              min={0}
              max={MAX_PRE_ROLL_MS}
              step={0.1}
              value={globalState.preRollMs}
              title="PRE lead offset before dry transient (0 ms to 100 ms)"
              onChange={(e) =>
                setGlobalState((prev) => ({
                  ...prev,
                  preRollMs: Number(e.target.value),
                }))
              }
              className="pre-slider w-[108px]"
            />
            <span className="w-[52px] h-[18px] leading-[16px] text-center text-[10.5px] bg-[#12111a] text-[#e6e8f2] border border-[#2d2b3c] rounded-sm tabular-nums">
              {globalState.preRollMs.toFixed(1)} ms
            </span>
          </div>
        </div>

        {/* Center Raised TRIGGER Button */}
        <div className="absolute left-1/2 -translate-x-1/2 -top-[5px] z-20">
          <button
            type="button"
            onClick={handleTrigger}
            title="Trigger all audible layers (Shortcut: Spacebar)"
            className="w-[116px] h-[30px] bg-[#242233] hover:bg-[#312e48] active:scale-[0.98] text-[#eef0ff] text-[12px] font-bold tracking-wider border border-[#454162] rounded shadow-md cursor-pointer transition"
          >
            TRIGGER
          </button>
        </div>

        {/* Right: Grid/Seconds, Detail, Zoom, Output */}
        <div className="flex items-center gap-1.5 z-10">
          <div className="flex items-center">
            <button
              type="button"
              onClick={() => setGlobalState((prev) => ({ ...prev, gridMode: true }))}
              style={{
                backgroundColor: globalState.gridMode ? '#2a2840' : '#111018',
                color: globalState.gridMode ? '#b4c0ff' : '#9fa4c4',
              }}
              className="h-[22px] px-2 text-[11px] border border-[#2d2b3c] rounded-l-sm cursor-pointer"
            >
              Grid
            </button>
            <button
              type="button"
              onClick={() => setGlobalState((prev) => ({ ...prev, gridMode: false }))}
              style={{
                backgroundColor: !globalState.gridMode ? '#2a2840' : '#111018',
                color: !globalState.gridMode ? '#c79bff' : '#9fa4c4',
              }}
              className="h-[22px] px-2 text-[11px] border border-l-0 border-[#2d2b3c] rounded-r-sm cursor-pointer"
            >
              Seconds
            </button>
          </div>

          <button
            type="button"
            onClick={focusTriggerOverlay}
            className="h-[22px] px-2 text-[11px] bg-[#1b1a26] text-[#9fa4c4] hover:text-[#e6e8f2] border border-[#2d2b3c] rounded-sm cursor-pointer"
          >
            Detail
          </button>

          <div className="flex items-center gap-[2px]">
            <button
              type="button"
              disabled={overlayZoom <= 1.01}
              onClick={() => zoomOverlayBy(1 / 1.6)}
              className="h-[22px] w-[22px] text-[11px] bg-[#1b1a26] text-[#9fa4c4] hover:text-[#e6e8f2] border border-[#2d2b3c] rounded-sm disabled:opacity-40 cursor-pointer"
            >
              -
            </button>
            <button
              type="button"
              onClick={() => clampOverlayViewport(0, 1)}
              className="h-[22px] w-[42px] text-[11px] bg-[#1b1a26] text-[#e6e8f2] border border-[#2d2b3c] rounded-sm tabular-nums cursor-pointer"
            >
              {zoomLabel}
            </button>
            <button
              type="button"
              disabled={overlayZoom >= 31.99}
              onClick={() => zoomOverlayBy(1.6)}
              className="h-[22px] w-[22px] text-[11px] bg-[#1b1a26] text-[#9fa4c4] hover:text-[#e6e8f2] border border-[#2d2b3c] rounded-sm disabled:opacity-40 cursor-pointer"
            >
              +
            </button>
          </div>

          <button
            type="button"
            title="Toggle live oscilloscope / FFT spectrum monitor"
            onClick={() =>
              setGlobalState((prev) => ({
                ...prev,
                monitorMode: !prev.monitorMode,
              }))
            }
            style={{
              backgroundColor: globalState.monitorMode ? '#2c2546' : '#1d1c28',
              color: globalState.monitorMode ? '#e6e8f2' : '#9fa4c4',
            }}
            className="h-[22px] px-2.5 text-[11px] font-medium border border-[#2d2b3c] rounded-sm cursor-pointer"
          >
            Output
          </button>
        </div>
      </div>

      {/* Bottom 2-Column Decks */}
      <div className="flex-1 min-h-0 p-2 flex gap-2">
        {renderDeck(true)}
        {renderDeck(false)}
      </div>
    </div>
  );
};

export default App;
