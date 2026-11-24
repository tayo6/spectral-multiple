import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { WaveLensAudioEngine } from './audio/WaveLensEngine';
import {
  GlobalMonitorParams,
  LayerParameters,
  LayerState,
  LayoutMode,
  MeterMode,
  NUM_LAYERS,
  SIGNAL_COLOURS,
  ScopeViewMode,
  createDefaultLayerParams,
  createEmptyPreview,
  layerShortChip,
  layerSlotLabel,
} from './audio/types';
import { TimelineMode } from './audio/timelineMath';
import {
  focusViewportAt,
  formatZoomFactor,
  MAX_WAVEFORM_ZOOM,
  WaveformViewport,
  zoomAroundNormalized,
} from './audio/waveformZoom';
import { LayerEnvelopeComponent } from './components/LayerEnvelopeComponent';
import { MeterView } from './components/MeterView';
import { ScopeView } from './components/ScopeView';
import { TopMasterBarCanvas } from './components/TopMasterBarCanvas';

export const App: React.FC = () => {
  const engineRef = useRef<WaveLensAudioEngine | null>(null);
  if (!engineRef.current) {
    engineRef.current = new WaveLensAudioEngine();
  }
  const engine = engineRef.current;

  const [layers, setLayers] = useState<LayerState[]>(() =>
    Array.from({ length: NUM_LAYERS }, (_, i) => ({
      name: '',
      preview: createEmptyPreview(),
      params: createDefaultLayerParams(i),
    }))
  );

  // Selected stack inside each deck: 0..2 for SNAPBACK, 3..5 for TRANSIENT
  const [selectedSnapback, setSelectedSnapback] = useState<number>(0);
  const [selectedTransient, setSelectedTransient] = useState<number>(3);

  const [bpm, setBpm] = useState<number>(120);
  const [timelineMode, setTimelineMode] = useState<TimelineMode>('grid');
  const [masterViewport, setMasterViewport] = useState<WaveformViewport>({
    viewStart: 0,
    zoom: 1,
  });
  const [triggerPulseAt, setTriggerPulseAt] = useState<number>(0);
  const [monitorMode, setMonitorMode] = useState<boolean>(false);
  const [micActive, setMicActive] = useState<boolean>(false);
  const [peakDbLabel, setPeakDbLabel] = useState<string>('-inf dB');
  const [errorMsg, setErrorMsg] = useState<string | null>(null);

  const [monitorParams, setMonitorParams] = useState<GlobalMonitorParams>({
    view: 0,
    capture: 0,
    bars: 4,
    timeMs: 200,
    gainDb: 0,
    layout: 0,
    showSig: [true, true, true, true, true, true],
    freeze: false,
    hold: false,
    meter: 0,
  });

  const fileInputRef = useRef<HTMLInputElement | null>(null);
  const pendingLoadLayerRef = useRef<number>(0);

  const loadedMask = useMemo(() => {
    let mask = 0;
    layers.forEach((l, i) => {
      if (l.preview.loaded) mask |= 1 << i;
    });
    return mask;
  }, [layers]);

  const loadedLayerCount = useMemo(
    () => layers.filter((l) => l.preview.loaded).length,
    [layers]
  );

  useEffect(() => {
    engine.setMonitorParams(
      {
        capture: monitorParams.capture,
        bars: monitorParams.bars,
        timeMs: monitorParams.timeMs,
        freeze: monitorParams.freeze,
      },
      monitorMode
    );
  }, [
    engine,
    monitorMode,
    monitorParams.capture,
    monitorParams.bars,
    monitorParams.timeMs,
    monitorParams.freeze,
  ]);

  useEffect(() => {
    const unsub = engine.subscribeSnapshot((snap) => {
      if (snap.peakNow <= 1e-6) {
        setPeakDbLabel('-inf dB');
      } else {
        const db = 20 * Math.log10(snap.peakNow);
        setPeakDbLabel(`${db >= 0 ? '+' : ''}${db.toFixed(1)} dB`);
      }
    });
    return () => {
      unsub();
      engine.dispose();
    };
  }, [engine]);

  const handleTriggerAll = useCallback(async () => {
    try {
      setErrorMsg(null);
      setTriggerPulseAt(Date.now());
      await engine.triggerAll();
    } catch (err) {
      setErrorMsg(err instanceof Error ? err.message : 'Failed to trigger audio.');
    }
  }, [engine]);

  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (
        e.code === 'Space' &&
        !(
          e.target instanceof HTMLInputElement ||
          e.target instanceof HTMLSelectElement ||
          e.target instanceof HTMLTextAreaElement ||
          e.target instanceof HTMLButtonElement
        )
      ) {
        e.preventDefault();
        void handleTriggerAll();
      }
    };
    window.addEventListener('keydown', onKeyDown);

    let midiAccess: MIDIAccess | null = null;
    const onMidiMessage = (ev: MIDIMessageEvent) => {
      const data = ev.data;
      if (!data || data.length < 3) return;
      const status = data[0] & 0xf0;
      const velocity = data[2];
      if (status === 0x90 && velocity > 0) {
        void handleTriggerAll();
      }
    };

    if (typeof navigator !== 'undefined' && 'requestMIDIAccess' in navigator) {
      navigator
        .requestMIDIAccess()
        .then((access) => {
          midiAccess = access;
          for (const input of access.inputs.values()) {
            input.onmidimessage = onMidiMessage;
          }
          access.onstatechange = () => {
            for (const input of access.inputs.values()) {
              input.onmidimessage = onMidiMessage;
            }
          };
        })
        .catch(() => {});
    }

    return () => {
      window.removeEventListener('keydown', onKeyDown);
      if (midiAccess) {
        for (const input of midiAccess.inputs.values()) {
          input.onmidimessage = null;
        }
      }
    };
  }, [handleTriggerAll]);

  const loadSampleForLayer = useCallback(
    async (layerIndex: number, file: File) => {
      try {
        setErrorMsg(null);
        const { preview, displayName } = await engine.loadSampleFile(layerIndex, file);
        setLayers((prev) =>
          prev.map((item, idx) =>
            idx === layerIndex ? { ...item, name: displayName, preview } : item
          )
        );
      } catch (err) {
        setErrorMsg(err instanceof Error ? err.message : 'Sample load failed.');
      }
    },
    [engine]
  );

  const openFilePickerForLayer = (layerIndex: number) => {
    pendingLoadLayerRef.current = layerIndex;
    if (fileInputRef.current) {
      fileInputRef.current.value = '';
      fileInputRef.current.click();
    }
  };

  const handleFileChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (file) {
      void loadSampleForLayer(pendingLoadLayerRef.current, file);
    }
  };

  const handleClearLayer = (layerIndex: number) => {
    engine.clearSample(layerIndex);
    setLayers((prev) =>
      prev.map((item, idx) =>
        idx === layerIndex
          ? { ...item, name: '', preview: createEmptyPreview() }
          : item
      )
    );
  };

  const updateLayerParams = useCallback(
    (layerIndex: number, patch: Partial<LayerParameters>) => {
      setLayers((prev) =>
        prev.map((item, idx) => {
          if (idx !== layerIndex) return item;
          const updated = { ...item.params, ...patch };
          engine.setLayerParameters(layerIndex, updated);
          return { ...item, params: updated };
        })
      );
    },
    [engine]
  );

  const handleAudition = async (layerIndex: number) => {
    try {
      setErrorMsg(null);
      await engine.requestPreview(layerIndex);
    } catch (err) {
      setErrorMsg(err instanceof Error ? err.message : 'Preview failed.');
    }
  };

  const handleToggleMic = async () => {
    try {
      setErrorMsg(null);
      const active = await engine.toggleAudioTriggerInput(!micActive);
      setMicActive(active);
    } catch (err) {
      setMicActive(false);
      setErrorMsg(
        err instanceof Error ? err.message : 'Could not access audio input.'
      );
    }
  };

  const getSnapshot = useCallback(() => engine.getLatestSnapshot(), [engine]);

  const selectLayer = useCallback((idx: number) => {
    if (idx < 3) setSelectedSnapback(idx);
    else setSelectedTransient(idx);
  }, []);

  const handleMasterZoom = (factor: number) => {
    setMasterViewport((prev) => zoomAroundNormalized(prev, 0.5, factor));
  };

  const renderDeck = (
    deckType: 'snapback' | 'transient',
    selectedIdx: number,
    onSelectIdx: (idx: number) => void
  ) => {
    const baseOffset = deckType === 'snapback' ? 0 : 3;
    const deckTitle = deckType === 'snapback' ? 'SNAPBACK' : 'TRANSIENT';
    const layer = layers[selectedIdx];
    const activeColour = SIGNAL_COLOURS[selectedIdx];
    const isLoaded = layer.preview.loaded;
    const stackNum = selectedIdx - baseOffset + 1;

    return (
      <section
        data-testid={`${deckType}-deck`}
        onDragOver={(e) => e.preventDefault()}
        onDrop={(e) => {
          e.preventDefault();
          const file = e.dataTransfer.files?.[0];
          if (file) void loadSampleForLayer(selectedIdx, file);
        }}
        className="flex-1 min-w-0 min-h-0 bg-[#1a1923] border border-[#2d2b3c] rounded-[5px] flex flex-col overflow-hidden"
      >
        {/* Deck Header: Power + SNAPBACK/TRANSIENT text + 3 colored stack circles right after text */}
        <div className="h-[34px] shrink-0 bg-[#1f1e2a] border-b border-[#2e2c3d] px-3 flex items-center justify-between gap-2">
          <div className="flex items-center gap-2.5 min-w-0">
            <button
              type="button"
              aria-label={`Mute ${deckTitle} Stack ${stackNum}`}
              aria-pressed={layer.params.muted}
              onClick={() =>
                updateLayerParams(selectedIdx, { muted: !layer.params.muted })
              }
              title={
                layer.params.muted
                  ? `Unmute ${deckTitle} ${stackNum}`
                  : `Mute ${deckTitle} ${stackNum}`
              }
              className="w-5 h-5 rounded-full border flex items-center justify-center text-[10px] cursor-pointer transition-colors shrink-0"
              style={{
                backgroundColor: layer.params.muted ? '#15141c' : '#262436',
                borderColor: layer.params.muted ? '#424058' : activeColour,
                color: layer.params.muted ? '#5e617c' : activeColour,
              }}
            >
              ⏻
            </button>

            <span
              className="text-[13px] font-bold tracking-[0.14em] uppercase whitespace-nowrap shrink-0"
              style={{
                color: deckType === 'snapback' ? '#8ea0ff' : '#cb8cff',
              }}
            >
              {deckTitle}
            </span>

            {/* 3 Colored Stack Circles immediately after SNAPBACK / TRANSIENT text */}
            <div
              className="flex items-center gap-1.5 shrink-0 pl-0.5"
              role="group"
              aria-label={`${deckTitle} 3 sample stack selectors`}
            >
              {[0, 1, 2].map((slot) => {
                const layerIdx = baseOffset + slot;
                const col = SIGNAL_COLOURS[layerIdx];
                const isSelected = selectedIdx === layerIdx;
                const slotLayer = layers[layerIdx];
                const hasSample = slotLayer.preview.loaded;
                const isMuted = slotLayer.params.muted;

                return (
                  <button
                    key={layerIdx}
                    type="button"
                    data-testid={`${deckType}-circle-${slot}`}
                    aria-label={`${deckTitle} Stack ${slot + 1}${slotLayer.name ? ` (${slotLayer.name})` : ''}`}
                    aria-pressed={isSelected}
                    onClick={() => onSelectIdx(layerIdx)}
                    onDragOver={(e) => e.preventDefault()}
                    onDrop={(e) => {
                      e.preventDefault();
                      e.stopPropagation();
                      const file = e.dataTransfer.files?.[0];
                      if (file) {
                        onSelectIdx(layerIdx);
                        void loadSampleForLayer(layerIdx, file);
                      }
                    }}
                    title={`${deckTitle} ${slot + 1}${slotLayer.name ? ` · ${slotLayer.name}` : ' (Empty — click to view or drop sample)'}`}
                    className="w-[15px] h-[15px] rounded-full cursor-pointer transition-transform flex items-center justify-center focus:outline-none focus-visible:ring-2 focus-visible:ring-white/80"
                    style={{
                      backgroundColor: hasSample ? col : 'transparent',
                      border: `2px solid ${col}`,
                      opacity: isMuted ? 0.38 : 1,
                      transform: isSelected ? 'scale(1.18)' : 'scale(1)',
                      boxShadow: isSelected
                        ? `0 0 0 2px #1f1e2a, 0 0 0 3.5px ${col}`
                        : 'none',
                    }}
                  >
                    {!hasSample && (
                      <span
                        className="w-[5px] h-[5px] rounded-full"
                        style={{ backgroundColor: col }}
                      />
                    )}
                  </button>
                );
              })}
            </div>

            <span className="text-[11px] text-[#9fa4c4] truncate hidden sm:inline">
              · {layer.name ? layer.name : `Stack ${stackNum} (Empty)`}
            </span>
          </div>

          <div className="flex items-center gap-1 shrink-0">
            <button
              type="button"
              aria-label={`Previous ${deckTitle} stack`}
              onClick={() =>
                onSelectIdx(baseOffset + ((selectedIdx - baseOffset + 2) % 3))
              }
              className="h-5 w-5 rounded-[2px] border border-[#343246] bg-[#16151e] hover:bg-[#272536] text-[10px] text-[#c7caea] flex items-center justify-center cursor-pointer"
              title="Previous stack"
            >
              ‹
            </button>
            <button
              type="button"
              aria-label={`Next ${deckTitle} stack`}
              onClick={() =>
                onSelectIdx(baseOffset + ((selectedIdx - baseOffset + 1) % 3))
              }
              className="h-5 w-5 rounded-[2px] border border-[#343246] bg-[#16151e] hover:bg-[#272536] text-[10px] text-[#c7caea] flex items-center justify-center cursor-pointer"
              title="Next stack"
            >
              ›
            </button>

            <button
              type="button"
              aria-label={`Solo ${deckTitle} Stack ${stackNum}`}
              aria-pressed={layer.params.solo}
              onClick={() =>
                updateLayerParams(selectedIdx, { solo: !layer.params.solo })
              }
              title={`Solo ${deckTitle} ${stackNum}`}
              className="h-5 px-1.5 rounded-[2px] border text-[9.5px] font-bold cursor-pointer transition-colors"
              style={{
                backgroundColor: layer.params.solo ? activeColour : '#16151e',
                borderColor: layer.params.solo ? activeColour : '#343246',
                color: layer.params.solo ? '#111118' : '#b7bad9',
              }}
            >
              S
            </button>

            <button
              type="button"
              aria-label={`Audition ${deckTitle} Stack ${stackNum}`}
              disabled={!isLoaded}
              onClick={() => void handleAudition(selectedIdx)}
              title={`Audition ${deckTitle} ${stackNum}`}
              className="h-5 px-1.5 rounded-[2px] border border-[#343246] bg-[#16151e] hover:bg-[#272536] text-[9.5px] text-[#e6e8f8] cursor-pointer disabled:opacity-35 disabled:cursor-not-allowed"
            >
              ▶
            </button>

            <button
              type="button"
              onClick={() => openFilePickerForLayer(selectedIdx)}
              className="h-5 px-2 rounded-[2px] border border-[#3b3950] bg-[#242232] hover:bg-[#312f45] text-[10px] font-semibold text-[#e6e8f8] cursor-pointer whitespace-nowrap"
            >
              {layer.name ? 'Replace' : 'Load...'}
            </button>

            {isLoaded && (
              <button
                type="button"
                aria-label={`Clear ${deckTitle} Stack ${stackNum}`}
                onClick={() => handleClearLayer(selectedIdx)}
                title="Clear loaded sample"
                className="h-5 w-5 rounded-[2px] border border-[#343246] bg-[#16151e] hover:bg-[#5e2329] text-[11px] font-bold text-[#e6e8f8] flex items-center justify-center cursor-pointer"
              >
                ×
              </button>
            )}
          </div>
        </div>

        {/* Individual Sample Canvas + Real-Time FL Studio Envelope */}
        <div className="flex-1 min-h-0 p-1.5 flex flex-col">
          <LayerEnvelopeComponent
            layer={selectedIdx}
            deckId={deckType}
            preview={layer.preview}
            params={layer.params}
            onChangeParams={(patch) => updateLayerParams(selectedIdx, patch)}
          />
        </div>
      </section>
    );
  };

  return (
    <div className="w-full h-screen min-h-[600px] bg-[#100f15] text-[#e6e8f2] flex flex-col box-border overflow-hidden">
      <input
        ref={fileInputRef}
        type="file"
        accept=".wav,.aif,.aiff,.flac,.ogg,.mp3,audio/*"
        onChange={handleFileChange}
        className="hidden"
      />

      {/* Top Snapback Navigation Bar (3 clean zones) */}
      <header className="h-[38px] shrink-0 bg-[#15141d] border-b border-[#282636] px-4 flex items-center justify-between gap-8">
        {/* Left: Audio Input Trigger & Reset */}
        <div className="flex items-center gap-2 shrink-0">
          <button
            type="button"
            onClick={() => void handleToggleMic()}
            aria-pressed={micActive}
            title="Toggle live audio input threshold triggering (3% threshold, 25 ms guard)"
            className="h-6 px-2.5 rounded-[3px] border text-[11px] font-medium cursor-pointer transition-colors whitespace-nowrap"
            style={{
              backgroundColor: micActive ? '#233252' : '#1b1a26',
              borderColor: micActive ? '#56ccf2' : '#323044',
              color: micActive ? '#ffffff' : '#b7bad9',
            }}
          >
            ⏻ Audio In
          </button>

          {monitorMode && (
            <>
              <button
                type="button"
                onClick={() =>
                  setMonitorParams((p) => ({ ...p, freeze: !p.freeze }))
                }
                aria-pressed={monitorParams.freeze}
                className={`h-6 px-2 rounded-[3px] border text-[11px] font-medium cursor-pointer whitespace-nowrap ${
                  monitorParams.freeze
                    ? 'bg-[#362c54] border-[#cc6bff] text-white'
                    : 'bg-[#1b1a26] border-[#323044] text-[#b7bad9] hover:bg-[#252434]'
                }`}
              >
                Freeze
              </button>
              <button
                type="button"
                onClick={() =>
                  setMonitorParams((p) => ({ ...p, hold: !p.hold }))
                }
                aria-pressed={monitorParams.hold}
                className={`h-6 px-2 rounded-[3px] border text-[11px] font-medium cursor-pointer whitespace-nowrap ${
                  monitorParams.hold
                    ? 'bg-[#362c54] border-[#cc6bff] text-white'
                    : 'bg-[#1b1a26] border-[#323044] text-[#b7bad9] hover:bg-[#252434]'
                }`}
              >
                Hold
              </button>
            </>
          )}

          <button
            type="button"
            onClick={() => engine.requestReset()}
            className="h-6 px-2 rounded-[3px] border border-[#323044] bg-[#1b1a26] hover:bg-[#252434] text-[11px] font-medium text-[#b7bad9] cursor-pointer whitespace-nowrap"
          >
            Reset
          </button>
        </div>

        {/* Center: Stylized SNAPBACK Brand Wordmark */}
        <div className="flex items-center justify-center shrink-0">
          <span
            className="text-[18px] font-extrabold tracking-[0.22em] select-none bg-gradient-to-r from-[#7d92ff] via-[#a79bff] to-[#c77dff] bg-clip-text text-transparent whitespace-nowrap"
          >
            SNAPBACK
          </span>
        </div>

        {/* Right: Tempo BPM for 1-Bar Grid & Active Stack Count */}
        <div className="flex items-center gap-3 shrink-0">
          <div className="flex items-center gap-1 text-[11px] text-[#9fa4c4] whitespace-nowrap">
            <button
              type="button"
              aria-label="Decrease BPM"
              onClick={() => setBpm((v) => Math.max(40, v - 5))}
              className="w-5 h-5 rounded-[2px] border border-[#323044] bg-[#1b1a26] hover:bg-[#262536] text-[#d6d9ef] cursor-pointer"
            >
              −
            </button>
            <span className="font-mono tabular-nums px-1.5 text-[#e6e8f8] min-w-[60px] text-center">
              {bpm} BPM
            </span>
            <button
              type="button"
              aria-label="Increase BPM"
              onClick={() => setBpm((v) => Math.min(240, v + 5))}
              className="w-5 h-5 rounded-[2px] border border-[#323044] bg-[#1b1a26] hover:bg-[#262536] text-[#d6d9ef] cursor-pointer"
            >
              +
            </button>
          </div>

          <span className="text-[11px] font-mono tabular-nums text-[#8e92b0] whitespace-nowrap">
            {loadedLayerCount}/6 Loaded
          </span>
        </div>
      </header>

      {errorMsg && (
        <div className="mx-3 mt-1.5 px-3 py-1 rounded bg-[#4b1a24] border border-[#ef4444] text-[#ffd4da] text-[11px] flex items-center justify-between shrink-0">
          <span>{errorMsg}</span>
          <button
            type="button"
            onClick={() => setErrorMsg(null)}
            className="font-bold cursor-pointer ml-2"
          >
            ×
          </button>
        </div>
      )}

      {/* TOP HALF (~48%): 1-Bar Stacked Waveform Canvas (or Live Scope/FFT Monitor when Output active) */}
      <div
        className="flex-[1.04] min-h-[220px] relative bg-[#0f0e14]"
        data-testid="top-master-viewport"
      >
        {monitorMode ? (
          <div className="w-full h-full p-2 flex flex-col gap-1.5">
            <div className="h-[30px] shrink-0 bg-[#1a1924] border border-[#2d2b3c] rounded-[3px] px-2.5 flex items-center gap-4">
              <label className="flex items-center gap-1.5 text-[10.5px] font-semibold text-[#c9cce2]">
                <span>Display</span>
                <select
                  value={monitorParams.view}
                  onChange={(e) =>
                    setMonitorParams((p) => ({
                      ...p,
                      view: parseInt(e.target.value, 10) as ScopeViewMode,
                    }))
                  }
                  className="h-[20px] bg-[#14131b] text-[#e5e8f5] border border-[#343246] rounded-[2px] px-1.5 text-[10.5px] cursor-pointer"
                >
                  <option value={0}>Waveform</option>
                  <option value={1}>Spectrum</option>
                </select>
              </label>

              <label className="flex items-center gap-1.5 text-[10.5px] font-semibold text-[#c9cce2]">
                <span>Layout</span>
                <select
                  disabled={loadedLayerCount <= 1}
                  value={monitorParams.layout}
                  onChange={(e) =>
                    setMonitorParams((p) => ({
                      ...p,
                      layout: parseInt(e.target.value, 10) as LayoutMode,
                    }))
                  }
                  className="h-[20px] bg-[#14131b] text-[#e5e8f5] border border-[#343246] rounded-[2px] px-1.5 text-[10.5px] cursor-pointer disabled:opacity-45"
                >
                  <option value={0}>Overlay</option>
                  <option value={1}>Split</option>
                </select>
              </label>

              <div className="flex items-center gap-1">
                <span className="text-[10.5px] font-semibold text-[#c9cce2] mr-1">
                  Traces
                </span>
                {Array.from({ length: NUM_LAYERS }, (_, n) => {
                  const loaded = (loadedMask & (1 << n)) !== 0;
                  const active = monitorParams.showSig[n];
                  const col = SIGNAL_COLOURS[n];
                  return (
                    <button
                      key={n}
                      type="button"
                      disabled={!loaded}
                      onClick={() =>
                        setMonitorParams((p) => ({
                          ...p,
                          showSig: p.showSig.map((v, idx) =>
                            idx === n ? !v : v
                          ),
                        }))
                      }
                      className="h-[20px] px-1.5 rounded-[2px] text-[9.5px] font-bold border border-[#343246] cursor-pointer disabled:opacity-35"
                      style={{
                        backgroundColor: loaded && active ? col : '#16151e',
                        color: loaded && active ? '#111118' : '#8f92ab',
                      }}
                    >
                      {layerShortChip(n)}
                    </button>
                  );
                })}
              </div>
            </div>

            <div className="flex-1 min-h-0 flex gap-1.5">
              <div className="flex-1 min-w-0 min-h-0">
                <ScopeView
                  monitorParams={monitorParams}
                  loadedMask={loadedMask}
                  getSnapshot={getSnapshot}
                />
              </div>
              <MeterView
                mode={monitorParams.meter}
                onCycleMode={() =>
                  setMonitorParams((p) => ({
                    ...p,
                    meter: ((p.meter + 1) % 3) as MeterMode,
                  }))
                }
                getSnapshot={getSnapshot}
              />
            </div>
          </div>
        ) : (
          <TopMasterBarCanvas
            layers={layers}
            selectedSnapback={selectedSnapback}
            selectedTransient={selectedTransient}
            bpm={bpm}
            timelineMode={timelineMode}
            viewport={masterViewport}
            onChangeViewport={setMasterViewport}
            onSelectLayer={selectLayer}
            triggerPulseAt={triggerPulseAt}
          />
        )}
      </div>

      {/* CENTER DIVIDER STRIP (Limiter | TRIGGER tab | Grid/Seconds + Detail + Zoom + Output) */}
      <div className="h-[34px] shrink-0 bg-[#171620] border-y border-[#2d2b3c] px-4 relative flex items-center justify-between gap-4">
        {/* Left: Output Peak / Limiter Readout */}
        <div className="flex items-center gap-2 text-[11px] text-[#9fa4c4] whitespace-nowrap shrink-0">
          <span className="text-[#8e92b0]">Limiter</span>
          <span className="font-mono tabular-nums text-[#dfe2f8]">
            {peakDbLabel}
          </span>
          <span aria-hidden="true" className="text-[#4a4862]">
            ·
          </span>
          <span className="text-[10.5px] font-mono text-[#8e92b0] hidden sm:inline">
            {layerSlotLabel(selectedSnapback)} + {layerSlotLabel(selectedTransient)}
          </span>
        </div>

        {/* Center Raised Trapezoid-Style TRIGGER Tab */}
        <button
          type="button"
          data-testid="master-trigger-btn"
          onClick={() => void handleTriggerAll()}
          title="Trigger all loaded SNAPBACK and TRANSIENT stacks (Spacebar or MIDI Note-On)"
          className="h-[28px] -mt-[8px] px-7 rounded-t-[6px] rounded-b-[3px] border border-[#4a4768] bg-[#242233] hover:bg-[#2f2c44] active:translate-y-[0.5px] text-[11px] font-bold tracking-[0.16em] text-[#eef0ff] cursor-pointer transition-colors shadow-md whitespace-nowrap"
        >
          TRIGGER
        </button>

        {/* Right: Grid / Seconds Toggle + Detail + Zoom Controls + Output Scope Toggle */}
        <div className="flex items-center gap-2.5 shrink-0">
          {/* Grid | Seconds Segmented Control */}
          <div
            className="flex items-center p-0.5 rounded-[3px] bg-[#111018] border border-[#2d2b3c]"
            role="group"
            aria-label="Top canvas ruler mode"
          >
            <button
              type="button"
              data-testid="timeline-mode-grid"
              aria-pressed={timelineMode === 'grid'}
              onClick={() => setTimelineMode('grid')}
              className={`h-[20px] px-2 rounded-[2px] text-[10.5px] font-semibold cursor-pointer transition-colors whitespace-nowrap ${
                timelineMode === 'grid'
                  ? 'bg-[#2a2840] text-[#b4c0ff]'
                  : 'text-[#888ca8] hover:text-[#d5d8f0]'
              }`}
            >
              Grid
            </button>
            <button
              type="button"
              data-testid="timeline-mode-seconds"
              aria-pressed={timelineMode === 'seconds'}
              onClick={() => setTimelineMode('seconds')}
              className={`h-[20px] px-2 rounded-[2px] text-[10.5px] font-semibold cursor-pointer transition-colors whitespace-nowrap ${
                timelineMode === 'seconds'
                  ? 'bg-[#2a2840] text-[#c79bff]'
                  : 'text-[#888ca8] hover:text-[#d5d8f0]'
              }`}
            >
              Seconds
            </button>
          </div>

          <button
            type="button"
            data-testid="master-detail-btn"
            onClick={() =>
              setMasterViewport((prev) =>
                prev.zoom > 1.2
                  ? { viewStart: 0, zoom: 1 }
                  : focusViewportAt(prev, 0.5, 4)
              )
            }
            title="Focus 4× zoom around the Trigger line"
            className="h-[22px] px-2 rounded-[3px] border border-[#323044] bg-[#1d1c28] hover:bg-[#2a283a] text-[10.5px] font-medium text-[#cfd2ec] cursor-pointer whitespace-nowrap"
          >
            Detail
          </button>

          {/* 1-Bar Zoom − / factor / + */}
          <div className="flex items-center rounded-[3px] border border-[#323044] bg-[#13121a] overflow-hidden">
            <button
              type="button"
              data-testid="master-zoom-out"
              disabled={masterViewport.zoom <= 1.01}
              onClick={() => handleMasterZoom(1 / 1.6)}
              aria-label="Zoom out 1-bar canvas"
              className="h-[22px] w-[22px] flex items-center justify-center text-[12px] font-bold text-[#e3e5f8] hover:bg-[#262536] disabled:opacity-35 cursor-pointer"
            >
              −
            </button>
            <button
              type="button"
              data-testid="master-zoom-reset"
              onClick={() => setMasterViewport({ viewStart: 0, zoom: 1 })}
              aria-label="Reset 1-bar canvas zoom"
              className="h-[22px] px-2 text-[10px] font-mono tabular-nums border-x border-[#323044] text-[#c8cce8] hover:bg-[#222130] cursor-pointer whitespace-nowrap"
            >
              {formatZoomFactor(masterViewport.zoom)}
            </button>
            <button
              type="button"
              data-testid="master-zoom-in"
              disabled={masterViewport.zoom >= MAX_WAVEFORM_ZOOM - 0.01}
              onClick={() => handleMasterZoom(1.6)}
              aria-label="Zoom in 1-bar canvas"
              className="h-[22px] w-[22px] flex items-center justify-center text-[12px] font-bold text-[#e3e5f8] hover:bg-[#262536] disabled:opacity-35 cursor-pointer"
            >
              +
            </button>
          </div>

          <button
            type="button"
            data-testid="output-monitor-toggle"
            aria-pressed={monitorMode}
            onClick={() => setMonitorMode((v) => !v)}
            title="Toggle real-time oscilloscope / FFT spectrum monitor"
            className={`h-[22px] px-2.5 rounded-[3px] border text-[10.5px] font-semibold cursor-pointer transition-colors whitespace-nowrap ${
              monitorMode
                ? 'bg-[#2c2546] border-[#cc6bff] text-white'
                : 'bg-[#1d1c28] border-[#323044] text-[#cfd2ec] hover:bg-[#2a283a]'
            }`}
          >
            Output
          </button>
        </div>
      </div>

      {/* BOTTOM HALF (~48%): Dual Side-by-Side Stacks — Left SNAPBACK (3 Circles) | Right TRANSIENT (3 Circles) */}
      <main className="flex-1 min-h-[250px] bg-[#14131c] p-2 grid grid-cols-1 lg:grid-cols-2 gap-2 overflow-hidden">
        {renderDeck('snapback', selectedSnapback, setSelectedSnapback)}
        {renderDeck('transient', selectedTransient, setSelectedTransient)}
      </main>
    </div>
  );
};

export default App;
