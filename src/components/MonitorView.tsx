import React, { useEffect, useRef } from 'react';
import { clamp, formatDb } from '../engine/envelopeMath';
import {
  MeterMode,
  MonitorLayoutMode,
  MonitorViewMode,
  NUM_LAYERS,
  SIGNAL_COLORS,
} from '../engine/types';
import { MeterSnapshot, WebAudioSampleEngine } from '../engine/WebAudioSampleEngine';

interface MonitorViewProps {
  engine: WebAudioSampleEngine;
  loadedMask: number;
  viewMode: MonitorViewMode;
  layoutMode: MonitorLayoutMode;
  freeze: boolean;
  hold: boolean;
  meterMode: MeterMode;
  showSig: [boolean, boolean, boolean, boolean, boolean, boolean];
  meterSnapshot: MeterSnapshot;
  onViewModeChange: (v: MonitorViewMode) => void;
  onLayoutModeChange: (l: MonitorLayoutMode) => void;
  onToggleSig: (layer: number) => void;
  onCycleMeterMode: () => void;
}

const SCOPE_POINTS = 256;
const SPEC_BINS = 256;

export const MonitorView: React.FC<MonitorViewProps> = ({
  engine,
  loadedMask,
  viewMode,
  layoutMode,
  freeze,
  hold,
  meterMode,
  showSig,
  meterSnapshot,
  onViewModeChange,
  onLayoutModeChange,
  onToggleSig,
  onCycleMeterMode,
}) => {
  const scopeContainerRef = useRef<HTMLDivElement>(null);
  const scopeCanvasRef = useRef<HTMLCanvasElement>(null);

  // Rolling waveform buffers & peak-hold traces
  const liveWaveRef = useRef<Float32Array[]>(
    Array.from({ length: NUM_LAYERS }, () => new Float32Array(SCOPE_POINTS))
  );
  const heldWaveRef = useRef<Float32Array[]>(
    Array.from({ length: NUM_LAYERS }, () => new Float32Array(SCOPE_POINTS))
  );
  const liveSpecRef = useRef<Float32Array[]>(
    Array.from({ length: NUM_LAYERS }, () => new Float32Array(SPEC_BINS).fill(-120))
  );
  const heldSpecRef = useRef<Float32Array[]>(
    Array.from({ length: NUM_LAYERS }, () => new Float32Array(SPEC_BINS).fill(-120))
  );

  // Clear held data on reset when loadedMask changes or when meter reset sets peakHold to 0
  useEffect(() => {
    if (meterSnapshot.peakHold === 0 && meterSnapshot.peakNow === 0) {
      for (let i = 0; i < NUM_LAYERS; i++) {
        heldWaveRef.current[i].fill(0);
        heldSpecRef.current[i].fill(-120);
      }
    }
  }, [meterSnapshot.peakHold, meterSnapshot.peakNow]);

  useEffect(() => {
    let rafId = 0;

    const renderFrame = () => {
      const container = scopeContainerRef.current;
      const canvas = scopeCanvasRef.current;
      if (container && canvas) {
        const rect = container.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        const w = Math.max(1, Math.floor(rect.width));
        const h = Math.max(1, Math.floor(rect.height));

        if (canvas.width !== Math.floor(w * dpr) || canvas.height !== Math.floor(h * dpr)) {
          canvas.width = Math.floor(w * dpr);
          canvas.height = Math.floor(h * dpr);
        }

        if (!freeze) {
          const sr = engine.getSampleRate();
          const nyquist = sr * 0.5;
          for (let layer = 0; layer < NUM_LAYERS; layer++) {
            const td = engine.getLayerTimeDomain(layer);
            if (td) {
              const dst = liveWaveRef.current[layer];
              const held = heldWaveRef.current[layer];
              const step = Math.floor(td.length / SCOPE_POINTS);
              for (let i = 0; i < SCOPE_POINTS; i++) {
                const start = i * step;
                let maxAbs = 0;
                let signedPeak = 0;
                for (let j = 0; j < step; j++) {
                  const v = td[start + j];
                  if (Math.abs(v) > maxAbs) {
                    maxAbs = Math.abs(v);
                    signedPeak = v;
                  }
                }
                dst[i] = signedPeak;
                if (Math.abs(signedPeak) > Math.abs(held[i])) {
                  held[i] = signedPeak;
                }
              }
            }

            const fd = engine.getLayerSpectrum(layer);
            if (fd) {
              const dstS = liveSpecRef.current[layer];
              const heldS = heldSpecRef.current[layer];
              for (let b = 0; b < SPEC_BINS; b++) {
                const hz = 20.0 * Math.pow(1000.0, b / (SPEC_BINS - 1));
                const binFloat = clamp((hz / nyquist) * fd.length, 0, fd.length - 1);
                const lo = Math.floor(binFloat);
                const hi = Math.min(fd.length - 1, lo + 1);
                const mix = binFloat - lo;
                const db = fd[lo] * (1 - mix) + fd[hi] * mix;
                const clampedDb = Number.isFinite(db) ? clamp(db, -120, 0) : -120;
                dstS[b] = dstS[b] + (clampedDb - dstS[b]) * (clampedDb > dstS[b] ? 0.68 : 0.25);
                if (clampedDb > heldS[b]) {
                  heldS[b] = clampedDb;
                }
              }
            }
          }
        }

        const ctx = canvas.getContext('2d');
        if (ctx) {
          ctx.save();
          ctx.scale(dpr, dpr);

          ctx.fillStyle = '#0f0e14';
          ctx.fillRect(0, 0, w, h);
          ctx.strokeStyle = '#2d2b3c';
          ctx.lineWidth = 1;
          ctx.strokeRect(0.5, 0.5, w - 1, h - 1);

          const active: number[] = [];
          for (let n = 0; n < NUM_LAYERS; n++) {
            if ((loadedMask & (1 << n)) !== 0 && showSig[n]) {
              active.push(n);
            }
          }

          const split = active.length > 1 && layoutMode === 'split';
          const pad = 6;
          const footerH = 16;
          const areaX = pad;
          const areaY = pad;
          const areaW = Math.max(10, w - pad * 2);
          const areaH = Math.max(10, h - pad * 2 - footerH);

          const drawWaveformLane = (
            lx: number,
            ly: number,
            lw: number,
            lh: number,
            layers: number[],
            title?: string
          ) => {
            ctx.save();
            ctx.beginPath();
            ctx.rect(lx, ly, lw, lh);
            ctx.clip();

            ctx.strokeStyle = 'rgba(255,255,255,0.05)';
            for (let i = 1; i < 8; i++) {
              const gx = Math.round(lx + (lw * i) / 8) + 0.5;
              ctx.beginPath();
              ctx.moveTo(gx, ly);
              ctx.lineTo(gx, ly + lh);
              ctx.stroke();
            }

            const cy = ly + lh * 0.5;
            ctx.strokeStyle = 'rgba(255,255,255,0.14)';
            ctx.beginPath();
            ctx.moveTo(lx, Math.round(cy) + 0.5);
            ctx.lineTo(lx + lw, Math.round(cy) + 0.5);
            ctx.stroke();

            const half = lh * 0.47;
            const drawTrace = (layer: number, isHeld: boolean) => {
              const buf = isHeld ? heldWaveRef.current[layer] : liveWaveRef.current[layer];
              ctx.save();
              ctx.strokeStyle = SIGNAL_COLORS[layer];
              ctx.globalAlpha = isHeld ? 0.35 : 0.95;
              ctx.lineWidth = isHeld ? 1.0 : 1.3;
              ctx.beginPath();
              for (let i = 0; i < SCOPE_POINTS; i++) {
                const px = lx + (i / (SCOPE_POINTS - 1)) * lw;
                const py = cy - clamp(buf[i], -1, 1) * half;
                if (i === 0) ctx.moveTo(px, py);
                else ctx.lineTo(px, py);
              }
              ctx.stroke();
              ctx.restore();
            };

            if (hold) {
              for (let i = layers.length - 1; i >= 0; i--) drawTrace(layers[i], true);
            }
            for (let i = layers.length - 1; i >= 0; i--) drawTrace(layers[i], false);

            if (title) {
              ctx.fillStyle = '#e6e8f2';
              ctx.font = '11px Inter, sans-serif';
              ctx.textAlign = 'left';
              ctx.textBaseline = 'top';
              ctx.fillText(title, lx + 8, ly + 4);
            }

            ctx.restore();
          };

          const drawSpectrumLane = (
            lx: number,
            ly: number,
            lw: number,
            lh: number,
            layers: number[]
          ) => {
            const plotX = lx + 36;
            const plotY = ly + 12;
            const plotW = Math.max(20, lw - 48);
            const plotH = Math.max(20, lh - 32);

            ctx.font = '10px Inter, sans-serif';
            for (const db of [-96, -72, -48, -24, 0]) {
              const y = plotY + plotH - ((db + 96) / 96) * plotH;
              ctx.strokeStyle = 'rgba(255,255,255,0.10)';
              ctx.beginPath();
              ctx.moveTo(plotX, Math.round(y) + 0.5);
              ctx.lineTo(plotX + plotW, Math.round(y) + 0.5);
              ctx.stroke();
              ctx.fillStyle = '#9fa4c4';
              ctx.textAlign = 'right';
              ctx.textBaseline = 'middle';
              ctx.fillText(String(db), plotX - 4, y);
            }

            const lr = Math.log(1000.0);
            for (const hz of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
              const x = plotX + (Math.log(hz / 20.0) / lr) * plotW;
              ctx.strokeStyle = 'rgba(255,255,255,0.08)';
              ctx.beginPath();
              ctx.moveTo(Math.round(x) + 0.5, plotY);
              ctx.lineTo(Math.round(x) + 0.5, plotY + plotH);
              ctx.stroke();
              ctx.fillStyle = '#9fa4c4';
              ctx.textAlign = 'center';
              ctx.textBaseline = 'top';
              const lbl =
                hz >= 1000 ? `${hz >= 10000 ? hz / 1000 : (hz / 1000).toFixed(1)}k` : String(hz);
              ctx.fillText(lbl, x, plotY + plotH + 3);
            }

            const drawCurve = (layer: number, isHeld: boolean) => {
              const buf = isHeld ? heldSpecRef.current[layer] : liveSpecRef.current[layer];
              ctx.save();
              ctx.strokeStyle = SIGNAL_COLORS[layer];
              ctx.globalAlpha = isHeld ? 0.35 : 0.98;
              ctx.lineWidth = isHeld ? 1.0 : 1.5;
              ctx.beginPath();
              for (let b = 0; b < SPEC_BINS; b++) {
                const n = clamp((buf[b] + 96) / 96, 0, 1);
                const x = plotX + (b / (SPEC_BINS - 1)) * plotW;
                const y = plotY + plotH - n * plotH;
                if (b === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
              }
              ctx.stroke();
              ctx.restore();
            };

            if (hold) {
              for (let i = layers.length - 1; i >= 0; i--) drawCurve(layers[i], true);
            }
            for (let i = layers.length - 1; i >= 0; i--) drawCurve(layers[i], false);
          };

          if (split) {
            const gap = 4;
            const laneH = (areaH - gap * (active.length - 1)) / active.length;
            active.forEach((n, idx) => {
              const ly = areaY + idx * (laneH + gap);
              if (viewMode === 'spectrum') {
                drawSpectrumLane(areaX, ly, areaW, laneH, [n]);
              } else {
                const title = n < 3 ? `SNAPBACK ${n + 1}` : `TRANSIENT ${n - 2}`;
                drawWaveformLane(areaX, ly, areaW, laneH, [n], title);
              }
            });
          } else {
            if (viewMode === 'spectrum') {
              drawSpectrumLane(areaX, areaY, areaW, areaH, active);
            } else {
              drawWaveformLane(areaX, areaY, areaW, areaH, active);
            }
          }

          // Footer status
          ctx.fillStyle = '#9fa4c4';
          ctx.font = '11px Inter, sans-serif';
          ctx.textBaseline = 'middle';
          const footerY = h - 9;
          ctx.textAlign = 'left';
          ctx.fillText('Live Web Audio Monitor', 10, footerY);
          ctx.textAlign = 'center';
          ctx.fillText(
            `${active.length} ${active.length === 1 ? 'active trace' : 'active traces'}`,
            w * 0.5,
            footerY
          );

          ctx.restore();
        }
      }

      rafId = requestAnimationFrame(renderFrame);
    };

    rafId = requestAnimationFrame(renderFrame);
    return () => cancelAnimationFrame(rafId);
  }, [engine, loadedMask, viewMode, layoutMode, freeze, hold, showSig]);

  // Meter bar calculations
  const toDb = (v: number) => 20.0 * Math.log10(Math.max(v, 1.0e-6));
  let nowVal = meterSnapshot.peakNow;
  let holdVal = meterSnapshot.peakHold;
  let lo = -60;
  let hi = 0;
  if (meterMode === 'RMS') {
    nowVal = meterSnapshot.rmsNow;
    holdVal = meterSnapshot.rmsHold;
  } else if (meterMode === 'CREST') {
    nowVal = meterSnapshot.crestNow;
    holdVal = meterSnapshot.crestHold;
    lo = 0;
    hi = 24;
  }
  const meterPct = clamp((toDb(nowVal) - lo) / (hi - lo), 0, 1) * 100;

  return (
    <div className="flex flex-col w-full h-full p-2 gap-1.5 bg-[#100f15]">
      {/* Monitor Toolbar */}
      <div className="flex items-center gap-1.5 h-[28px] px-1 shrink-0">
        <select
          value={viewMode}
          onChange={(e) => onViewModeChange(e.target.value as MonitorViewMode)}
          className="h-[22px] px-2 text-[11px] bg-[#1b1a26] text-[#e6e8f2] border border-[#2d2b3c] rounded outline-none cursor-pointer"
        >
          <option value="waveform">Waveform</option>
          <option value="spectrum">Spectrum</option>
        </select>

        <select
          value={layoutMode}
          disabled={IntegerPopCount(loadedMask) <= 1}
          onChange={(e) => onLayoutModeChange(e.target.value as MonitorLayoutMode)}
          className="h-[22px] px-2 text-[11px] bg-[#1b1a26] text-[#e6e8f2] border border-[#2d2b3c] rounded outline-none cursor-pointer disabled:opacity-45"
        >
          <option value="overlay">Overlay</option>
          <option value="split">Split</option>
        </select>

        <div className="w-1" />

        {Array.from({ length: NUM_LAYERS }).map((_, n) => {
          const loaded = (loadedMask & (1 << n)) !== 0;
          const active = showSig[n];
          const label = n < 3 ? `SB${n + 1}` : `TR${n - 2}`;
          return (
            <button
              key={n}
              type="button"
              disabled={!loaded}
              onClick={() => onToggleSig(n)}
              style={{
                backgroundColor: active && loaded ? SIGNAL_COLORS[n] : '#1b1a26',
                color: active && loaded ? '#111118' : '#9fa4c4',
              }}
              className="h-[22px] w-[36px] text-[10.5px] font-semibold rounded border border-[#2d2b3c] transition-opacity disabled:opacity-35 cursor-pointer"
            >
              {label}
            </button>
          );
        })}
      </div>

      {/* Scope + Meter Row */}
      <div className="flex flex-1 min-h-0 gap-1.5">
        <div ref={scopeContainerRef} className="flex-1 min-w-0 h-full relative rounded overflow-hidden">
          <canvas ref={scopeCanvasRef} className="block w-full h-full" />
        </div>

        {/* MeterView */}
        <button
          type="button"
          onClick={onCycleMeterMode}
          title="Click to cycle PEAK / RMS / CREST"
          className="w-[64px] h-full shrink-0 bg-[#1a1923] border border-[#2d2b3c] rounded flex flex-col items-center justify-between p-1.5 cursor-pointer hover:border-[#4b4766]"
        >
          <span className="text-[10.5px] font-bold text-[#e6e8f2] tracking-wide">
            {meterMode}
          </span>
          <div className="w-[22px] flex-1 my-1 bg-[#100f15] rounded-sm relative overflow-hidden flex items-end">
            <div
              className="w-full rounded-sm transition-[height] duration-75"
              style={{
                height: `${meterPct}%`,
                background: 'linear-gradient(to bottom, #ff4e42 0%, #ffd24a 25%, #58e058 100%)',
              }}
            />
          </div>
          <div className="flex flex-col items-center leading-tight">
            <span className="text-[10px] font-bold text-[#e6e8f2] tabular-nums">
              {meterMode === 'CREST' ? toDb(nowVal).toFixed(1) : formatDb(nowVal)}
            </span>
            <span className="text-[9.5px] text-[#9fa4c4] tabular-nums">
              {meterMode === 'CREST' ? toDb(holdVal).toFixed(1) : formatDb(holdVal)}
            </span>
          </div>
        </button>
      </div>
    </div>
  );
};

function IntegerPopCount(n: number): number {
  let c = 0;
  let v = n;
  while (v > 0) {
    c += v & 1;
    v >>= 1;
  }
  return c;
}
