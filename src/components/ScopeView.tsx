import React, { useEffect, useRef } from 'react';
import { clamp, formatMs } from '../audio/curves';
import {
  BARS_TABLE,
  BAR_LABELS,
  GlobalMonitorParams,
  MonitorSnapshot,
  NUM_LAYERS,
  SCOPE_COLS,
  SIGNAL_COLOURS,
  SPECTRUM_BINS,
  layerShortChip,
  layerSlotLabel,
} from '../audio/types';

interface ScopeViewProps {
  monitorParams: GlobalMonitorParams;
  loadedMask: number;
  getSnapshot: () => MonitorSnapshot;
}

export const ScopeView: React.FC<ScopeViewProps> = ({
  monitorParams,
  loadedMask,
  getSnapshot,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const containerRef = useRef<HTMLDivElement | null>(null);
  const paramsRef = useRef(monitorParams);
  paramsRef.current = monitorParams;
  const maskRef = useRef(loadedMask);
  maskRef.current = loadedMask;

  useEffect(() => {
    let rafId = 0;

    const windowLabel = (p: GlobalMonitorParams) => {
      if (p.capture === 0) {
        return `Tempo  ${BAR_LABELS[clamp(p.bars, 0, 6)]}`;
      }
      return `${p.capture === 1 ? 'Live  ' : 'Scroll  '}${formatMs(p.timeMs)}`;
    };

    const drawTrace = (
      ctx: CanvasRenderingContext2D,
      lane: { x: number; y: number; w: number; h: number },
      ch: number,
      colour: string,
      s: MonitorSnapshot,
      gain: number,
      held: boolean,
      fillAlpha: number,
      captureMode: number
    ) => {
      const scroll = captureMode === 2;
      const N = SCOPE_COLS;
      let first = 0;
      let count = 0;

      if (held) {
        if (scroll || !s.heldValid) return;
        first = s.heldFrom;
        count = s.heldTo - s.heldFrom + 1;
      } else {
        if (!s.hasData) return;
        if (scroll) {
          count = clamp(s.filled, 0, N);
          first = (((s.head - count + 1) % N) + N) % N;
        } else {
          first = s.passStart;
          count = s.head - s.passStart + 1;
        }
      }

      if (count < 2) return;

      const colW = lane.w / N;
      const centre = lane.y + lane.h * 0.5;
      const half = lane.h * 0.5 * 0.94;
      const maxBuf = held ? s.heldMax : s.colMax;
      const minBuf = held ? s.heldMin : s.colMin;

      const xOf = (j: number) =>
        scroll
          ? lane.x + lane.w - (count - j - 0.5) * colW
          : lane.x + (first + j + 0.5) * colW;
      const yOf = (v: number) => centre - clamp(v * gain, -1, 1) * half;
      const colOf = (j: number) => (first + j) % N;

      ctx.beginPath();
      for (let j = 0; j < count; j++) {
        const y = yOf(maxBuf[ch * N + colOf(j)]);
        if (j === 0) ctx.moveTo(xOf(j), y);
        else ctx.lineTo(xOf(j), y);
      }
      for (let j = count - 1; j >= 0; j--) {
        ctx.lineTo(xOf(j), yOf(minBuf[ch * N + colOf(j)]));
      }
      ctx.closePath();

      ctx.globalAlpha = held ? fillAlpha * 0.35 : fillAlpha;
      ctx.fillStyle = colour;
      ctx.fill();

      ctx.globalAlpha = held ? 0.35 : 0.95;
      ctx.strokeStyle = colour;
      ctx.lineWidth = 1;
      ctx.stroke();
      ctx.globalAlpha = 1;
    };

    const drawLane = (
      ctx: CanvasRenderingContext2D,
      lane: { x: number; y: number; w: number; h: number },
      traces: Array<{ ch: number; colour: string }>,
      s: MonitorSnapshot,
      gain: number,
      showHeld: boolean,
      title: string,
      p: GlobalMonitorParams
    ) => {
      ctx.save();
      ctx.beginPath();
      ctx.rect(lane.x, lane.y, lane.w, lane.h);
      ctx.clip();

      let divisions = 10;
      if (p.capture === 0) {
        const bars = BARS_TABLE[clamp(p.bars, 0, 6)];
        divisions = bars >= 1 ? Math.round(bars * 4) : 4;
      }

      ctx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
      ctx.lineWidth = 1;
      for (let i = 1; i < divisions; i++) {
        const gx = Math.round(lane.x + (lane.w * i) / divisions) + 0.5;
        ctx.beginPath();
        ctx.moveTo(gx, lane.y);
        ctx.lineTo(gx, lane.y + lane.h);
        ctx.stroke();
      }

      const centre = lane.y + lane.h * 0.5;
      const half = lane.h * 0.5 * 0.94;
      for (const lvl of [-1, -0.5, 0.5, 1]) {
        const gy = Math.round(centre - lvl * half) + 0.5;
        ctx.beginPath();
        ctx.moveTo(lane.x, gy);
        ctx.lineTo(lane.x + lane.w, gy);
        ctx.stroke();
      }

      ctx.strokeStyle = 'rgba(255, 255, 255, 0.14)';
      const cy = Math.round(centre) + 0.5;
      ctx.beginPath();
      ctx.moveTo(lane.x, cy);
      ctx.lineTo(lane.x + lane.w, cy);
      ctx.stroke();

      const fillAlpha = traces.length > 1 ? 0.1 : 0.3;

      if (showHeld) {
        for (let i = traces.length - 1; i >= 0; i--) {
          drawTrace(ctx, lane, traces[i].ch, traces[i].colour, s, gain, true, fillAlpha, p.capture);
        }
      }
      for (let i = traces.length - 1; i >= 0; i--) {
        drawTrace(ctx, lane, traces[i].ch, traces[i].colour, s, gain, false, fillAlpha, p.capture);
      }

      if (p.capture !== 2 && s.hasData) {
        const cx = Math.round(lane.x + ((s.head + 0.5) * lane.w) / SCOPE_COLS) + 0.5;
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.25)';
        ctx.beginPath();
        ctx.moveTo(cx, lane.y);
        ctx.lineTo(cx, lane.y + lane.h);
        ctx.stroke();
      }

      if (title) {
        ctx.fillStyle = '#aeb8c0';
        ctx.font = '11px -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif';
        ctx.textAlign = 'left';
        ctx.textBaseline = 'top';
        ctx.fillText(title, lane.x + 8, lane.y + 4);
      }

      ctx.restore();
    };

    const drawSpectrum = (
      ctx: CanvasRenderingContext2D,
      lane: { x: number; y: number; w: number; h: number },
      traces: Array<{ ch: number; colour: string }>,
      s: MonitorSnapshot,
      showHeld: boolean
    ) => {
      const plotX = lane.x + 38;
      const plotY = lane.y + 18;
      const plotW = Math.max(10, lane.w - 76);
      const plotH = Math.max(10, lane.h - 56);

      ctx.font = '10px -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif';
      for (const db of [-96, -72, -48, -24, 0]) {
        const y = plotY + plotH - ((db + 96) / 96) * plotH;
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(plotX, Math.round(y) + 0.5);
        ctx.lineTo(plotX + plotW, Math.round(y) + 0.5);
        ctx.stroke();

        ctx.fillStyle = '#e0e3e5';
        ctx.textAlign = 'right';
        ctx.textBaseline = 'middle';
        ctx.fillText(`${db}`, plotX - 4, y);
      }

      const lr = Math.log(1000);
      for (const hz of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
        const x = plotX + (Math.log(hz / 20) / lr) * plotW;
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.08)';
        ctx.beginPath();
        ctx.moveTo(Math.round(x) + 0.5, plotY);
        ctx.lineTo(Math.round(x) + 0.5, plotY + plotH);
        ctx.stroke();

        ctx.fillStyle = '#e0e3e5';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'top';
        const lbl =
          hz >= 1000 ? `${(hz / 1000).toFixed(hz >= 10000 ? 0 : 1)}k` : `${hz}`;
        ctx.fillText(lbl, x, plotY + plotH + 3);
      }

      const strokeSpec = (ch: number, colour: string, held: boolean) => {
        const buf = held ? s.heldSpectrumDb : s.spectrumDb;
        const offset = ch * SPECTRUM_BINS;
        ctx.beginPath();
        for (let b = 0; b < SPECTRUM_BINS; b++) {
          const db = buf[offset + b];
          const n = clamp((db + 96) / 96, 0, 1);
          const x = plotX + (b / (SPECTRUM_BINS - 1)) * plotW;
          const y = plotY + plotH - n * plotH;
          if (b === 0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        }
        ctx.strokeStyle = colour;
        ctx.globalAlpha = held ? 0.35 : 0.98;
        ctx.lineWidth = held ? 1.0 : 1.5;
        ctx.stroke();
        ctx.globalAlpha = 1;
      };

      if (showHeld) {
        for (let i = traces.length - 1; i >= 0; i--) {
          strokeSpec(traces[i].ch, traces[i].colour, true);
        }
      }
      for (let i = traces.length - 1; i >= 0; i--) {
        strokeSpec(traces[i].ch, traces[i].colour, false);
      }
    };

    const draw = () => {
      const canvas = canvasRef.current;
      const container = containerRef.current;
      if (canvas && container) {
        const rect = container.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        const w = Math.max(1, Math.floor(rect.width));
        const h = Math.max(1, Math.floor(rect.height));
        if (canvas.width !== Math.floor(w * dpr) || canvas.height !== Math.floor(h * dpr)) {
          canvas.width = Math.floor(w * dpr);
          canvas.height = Math.floor(h * dpr);
        }

        const ctx = canvas.getContext('2d');
        if (ctx) {
          const p = paramsRef.current;
          const mask = maskRef.current;
          const s = getSnapshot();

          ctx.save();
          ctx.scale(dpr, dpr);

          ctx.fillStyle = '#151a1f';
          ctx.beginPath();
          ctx.roundRect(0.5, 0.5, w - 1, h - 1, 6);
          ctx.fill();
          ctx.strokeStyle = '#424b54';
          ctx.lineWidth = 1;
          ctx.stroke();

          const gain = Math.pow(10, p.gainDb / 20);
          const showHeld = p.hold;
          const active: number[] = [];
          for (let n = 0; n < NUM_LAYERS; n++) {
            if ((mask & (1 << n)) !== 0 && p.showSig[n]) {
              active.push(n);
            }
          }

          const split = active.length > 1 && p.layout === 1;
          const area = { x: 6, y: 6, w: w - 12, h: h - 28 };
          const spectrum = p.view === 1;

          if (split) {
            const gap = 4;
            const laneH = (area.h - gap * (active.length - 1)) / active.length;
            active.forEach((n, idx) => {
              const lane = {
                x: area.x,
                y: area.y + idx * (laneH + gap),
                w: area.w,
                h: laneH,
              };
              const trace = [{ ch: n, colour: SIGNAL_COLOURS[n] }];
              if (spectrum) drawSpectrum(ctx, lane, trace, s, showHeld);
              else drawLane(ctx, lane, trace, s, gain, showHeld, layerSlotLabel(n), p);
            });
          } else {
            const traces = active.map((n) => ({ ch: n, colour: SIGNAL_COLOURS[n] }));
            if (spectrum) drawSpectrum(ctx, area, traces, s, showHeld);
            else drawLane(ctx, area, traces, s, gain, showHeld, '', p);

            if (traces.length > 1) {
              ctx.font = 'bold 11px -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif';
              let chipX = area.x + area.w - 8;
              for (let k = active.length - 1; k >= 0; k--) {
                const n = active[k];
                ctx.fillStyle = SIGNAL_COLOURS[n];
                ctx.globalAlpha = 0.9;
                ctx.beginPath();
                ctx.roundRect(chipX - 24, area.y + 6, 24, 14, 3);
                ctx.fill();
                ctx.globalAlpha = 1;
                ctx.fillStyle = '#04121a';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText(layerShortChip(n), chipX - 12, area.y + 13);
                chipX -= 28;
              }
            }
          }

          // Footer
          const footerY = h - 10;
          ctx.font = '11px -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif';
          ctx.textBaseline = 'middle';
          ctx.fillStyle = '#aeb8c0';
          ctx.textAlign = 'left';
          ctx.fillText(windowLabel(p), 10, footerY);

          ctx.textAlign = 'center';
          if (mask === 0) {
            ctx.fillText(
              'Load samples into the PRE and MAIN slots to view their waveforms and spectra',
              w * 0.5,
              footerY
            );
          } else {
            ctx.fillText(
              `${active.length} ${active.length === 1 ? 'layer' : 'layers'}`,
              w * 0.5,
              footerY
            );
          }

          if (p.freeze) {
            ctx.fillStyle = '#ffb04a';
            ctx.textAlign = 'right';
            ctx.fillText('FROZEN', w - 10, footerY);
          }

          ctx.restore();
        }
      }
      rafId = requestAnimationFrame(draw);
    };

    rafId = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(rafId);
  }, [getSnapshot]);

  return (
    <div ref={containerRef} className="w-full h-full relative">
      <canvas ref={canvasRef} className="block w-full h-full rounded-[6px]" />
    </div>
  );
};
