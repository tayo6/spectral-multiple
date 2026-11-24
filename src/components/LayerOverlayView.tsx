import React, { useEffect, useRef } from 'react';
import {
  clamp,
  computeEnvelopeSchedule,
  evaluateCompleteEnvelopeAtMs,
  formatTimeTickLabel,
} from '../engine/envelopeMath';
import {
  LayerParameters,
  LayerSampleInfo,
  MAX_PRE_ROLL_MS,
  NUM_LAYERS,
  PRE_LAYERS,
  PREVIEW_POINTS,
  SIGNAL_COLORS,
} from '../engine/types';

interface LayerOverlayViewProps {
  samples: LayerSampleInfo[];
  layerParams: LayerParameters[];
  selectedSnapback: number;
  selectedTransient: number;
  bpm: number;
  preRollMs: number;
  gridMode: boolean;
  zoomFactor: number;
  viewStartNorm: number;
  onViewportChange: (nextStart: number, nextZoom: number) => void;
}

export const LayerOverlayView: React.FC<LayerOverlayViewProps> = ({
  samples,
  layerParams,
  selectedSnapback,
  selectedTransient,
  bpm,
  preRollMs,
  gridMode,
  zoomFactor,
  viewStartNorm,
  onViewportChange,
}) => {
  const containerRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const dragRef = useRef<{ startX: number; startView: number } | null>(null);

  const clampViewport = (nextStart: number, nextZoom: number) => {
    const z = clamp(nextZoom, 1.0, 32.0);
    const span = 1.0 / z;
    const vs = z <= 1.0001 ? 0.0 : clamp(nextStart, 0.0, Math.max(0.0, 1.0 - span));
    onViewportChange(vs, z <= 1.0001 ? 1.0 : z);
  };

  useEffect(() => {
    const container = containerRef.current;
    const canvas = canvasRef.current;
    if (!container || !canvas) return;

    const draw = () => {
      const rect = container.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      const w = Math.max(1, Math.floor(rect.width));
      const h = Math.max(1, Math.floor(rect.height));

      if (canvas.width !== Math.floor(w * dpr) || canvas.height !== Math.floor(h * dpr)) {
        canvas.width = Math.floor(w * dpr);
        canvas.height = Math.floor(h * dpr);
      }

      const ctx = canvas.getContext('2d');
      if (!ctx) return;

      ctx.save();
      ctx.scale(dpr, dpr);

      // Background gradient
      const bgGrad = ctx.createLinearGradient(0, 0, 0, h);
      bgGrad.addColorStop(0, '#0f0e14');
      bgGrad.addColorStop(1, '#13121a');
      ctx.fillStyle = bgGrad;
      ctx.fillRect(0, 0, w, h);

      const rulerH = 22;
      ctx.fillStyle = '#16151f';
      ctx.fillRect(0, 0, w, rulerH);
      ctx.strokeStyle = 'rgba(155, 160, 210, 0.14)';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(0, rulerH - 0.5);
      ctx.lineTo(w, rulerH - 0.5);
      ctx.stroke();

      const plotX = 0;
      const plotY = rulerH;
      const plotW = w;
      const plotH = h - rulerH;
      if (plotW < 32 || plotH < 32) {
        ctx.restore();
        return;
      }

      const span = 1.0 / Math.max(1.0, zoomFactor);
      const viewEnd = Math.min(1.0, viewStartNorm + span);
      const barMs = (4.0 * 60000.0) / bpm;

      const normToX = (norm: number) => plotX + ((norm - viewStartNorm) / span) * plotW;
      const xToNorm = (x: number) =>
        clamp(viewStartNorm + ((x - plotX) / Math.max(1, plotW)) * span, 0, 1);

      ctx.font = '10px Inter, sans-serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';

      if (gridMode) {
        let divisions = 8;
        let majorStep = 2;
        if (zoomFactor >= 12.0) {
          divisions = 64;
          majorStep = 4;
        } else if (zoomFactor >= 4.5) {
          divisions = 32;
          majorStep = 2;
        } else if (zoomFactor >= 1.8) {
          divisions = 16;
          majorStep = 4;
        }

        const firstIdx = Math.max(0, Math.floor(viewStartNorm * divisions));
        const lastIdx = Math.min(divisions, Math.ceil(viewEnd * divisions));

        for (let i = firstIdx; i <= lastIdx; i++) {
          const norm = i / divisions;
          const tx = Math.round(normToX(norm)) + 0.5;
          const major = i % majorStep === 0;

          ctx.strokeStyle = major
            ? 'rgba(160, 166, 224, 0.13)'
            : 'rgba(160, 166, 224, 0.05)';
          ctx.beginPath();
          ctx.moveTo(tx, plotY);
          ctx.lineTo(tx, plotY + plotH);
          ctx.stroke();

          ctx.strokeStyle = major
            ? 'rgba(196, 202, 240, 0.45)'
            : 'rgba(196, 202, 240, 0.18)';
          ctx.beginPath();
          ctx.moveTo(tx, rulerH - (major ? 7 : 4));
          ctx.lineTo(tx, rulerH);
          ctx.stroke();

          if (major) {
            let lbl = '';
            if (i === divisions) {
              lbl = '2.1';
            } else {
              const beatFloat = norm * 4.0;
              const beatNum = Math.floor(beatFloat) + 1;
              const sixteenths = Math.round((beatFloat - Math.floor(beatFloat)) * 4.0) + 1;
              lbl =
                divisions >= 32
                  ? `1.${beatNum}.${sixteenths}`
                  : `1.${beatNum}`;
            }
            ctx.fillStyle = 'rgba(198, 203, 232, 0.72)';
            ctx.fillText(lbl, tx, rulerH / 2);
          }
        }
      } else {
        const visibleMs = barMs / zoomFactor;
        const candidates = [1000, 500, 250, 100, 50, 25, 10, 5, 2, 1];
        let stepMs = 250;
        for (const c of candidates) {
          const count = visibleMs / c;
          if (count >= 4 && count <= 10) {
            stepMs = c;
            break;
          }
        }
        const minorStep = stepMs * 0.5;
        const firstIdx = Math.max(0, Math.floor((viewStartNorm * barMs) / minorStep));
        const lastIdx = Math.min(
          Math.ceil(barMs / minorStep),
          Math.ceil((viewEnd * barMs) / minorStep)
        );
        for (let i = firstIdx; i <= lastIdx; i++) {
          const ms = i * minorStep;
          const norm = clamp(ms / barMs, 0, 1);
          const tx = Math.round(normToX(norm)) + 0.5;
          const major = i % 2 === 0;
          ctx.strokeStyle = major
            ? 'rgba(160, 166, 224, 0.13)'
            : 'rgba(160, 166, 224, 0.05)';
          ctx.beginPath();
          ctx.moveTo(tx, plotY);
          ctx.lineTo(tx, plotY + plotH);
          ctx.stroke();

          if (major) {
            ctx.fillStyle = 'rgba(198, 203, 232, 0.72)';
            ctx.fillText(formatTimeTickLabel(ms), tx, rulerH / 2);
          }
        }
      }

      // Clip plot area
      ctx.save();
      ctx.beginPath();
      ctx.rect(plotX, plotY, plotW, plotH);
      ctx.clip();

      const centre = plotY + plotH * 0.5;
      const halfHeight = plotH * 0.43;

      // Center zero line
      ctx.strokeStyle = 'rgba(180, 186, 235, 0.16)';
      ctx.beginPath();
      ctx.moveTo(plotX, Math.round(centre) + 0.5);
      ctx.lineTo(plotX + plotW, Math.round(centre) + 0.5);
      ctx.stroke();

      // Trigger anchor line at 0.5
      const trigX = normToX(0.5);
      if (trigX >= plotX - 2 && trigX <= plotX + plotW + 2) {
        ctx.save();
        ctx.strokeStyle = 'rgba(154, 174, 255, 0.34)';
        ctx.setLineDash([4, 3]);
        ctx.beginPath();
        ctx.moveTo(trigX, plotY);
        ctx.lineTo(trigX, plotY + plotH);
        ctx.stroke();
        ctx.restore();
      }

      const anySolo = layerParams.some((p) => p.solo);
      let loadedCount = 0;

      for (let layer = 0; layer < NUM_LAYERS; layer++) {
        const sample = samples[layer];
        if (!sample.loaded || !sample.preview.loaded) continue;
        loadedCount++;

        const p = layerParams[layer];
        const audible = !p.muted && (!anySolo || p.solo);
        const focused = layer === selectedSnapback || layer === selectedTransient;
        const trimStart = clamp(p.trimStart, 0, 0.99);
        const trimEnd = clamp(p.trimEnd, trimStart + 0.005, 1.0);
        const layerPreLead = layer < PRE_LAYERS ? preRollMs : 0;
        const effectiveOffsetMs = Math.max(
          -MAX_PRE_ROLL_MS,
          p.delayShiftMs - layerPreLead
        );
        const trimmedMs = Math.max(1.0, sample.preview.durationMs * (trimEnd - trimStart));
        const playbackMs = Math.max(
          1.0,
          trimmedMs / Math.pow(2.0, p.pitchSemitones / 12.0)
        );
        const startNorm = 0.5 + effectiveOffsetMs / barMs;
        const spanNorm = playbackMs / barMs;
        const x0 = normToX(startNorm);
        const x1 = normToX(startNorm + spanNorm);
        if (x1 < plotX - 2 || x0 > plotX + plotW + 2) continue;

        const visX0 = Math.max(plotX, x0);
        const visX1 = Math.min(plotX + plotW, x1);
        const steps = clamp(Math.round((visX1 - visX0) * 1.5), 48, 420);

        const sched = computeEnvelopeSchedule(p, playbackMs);
        const upperPts: Array<{ x: number; y: number }> = [];
        const lowerPts: Array<{ x: number; y: number }> = [];

        for (let s = 0; s <= steps; s++) {
          const px = visX0 + (s / steps) * (visX1 - visX0);
          const u = clamp((xToNorm(px) - startNorm) / Math.max(1e-6, spanNorm), 0, 1);
          const posInWave = p.reverse
            ? trimEnd - u * (trimEnd - trimStart)
            : trimStart + u * (trimEnd - trimStart);
          const idx = clamp(
            Math.round(posInWave * (PREVIEW_POINTS - 1)),
            0,
            PREVIEW_POINTS - 1
          );
          const elapsedMs = u * playbackMs;
          const remainingMs = (1.0 - u) * playbackMs;
          const fadeInG = p.fadeInMs > 0.5 ? clamp(elapsedMs / p.fadeInMs, 0, 1) : 1.0;
          const fadeOutG = p.fadeOutMs > 0.5 ? clamp(remainingMs / p.fadeOutMs, 0, 1) : 1.0;
          const flEnv = evaluateCompleteEnvelopeAtMs(p, sched, elapsedMs);
          const env = clamp(p.gain, 0, 2) * fadeInG * fadeOutG * flEnv;

          const top = centre - clamp(sample.preview.maximum[idx] * env, -1, 1) * halfHeight;
          const bot = centre - clamp(sample.preview.minimum[idx] * env, -1, 1) * halfHeight;
          upperPts.push({ x: px, y: top });
          lowerPts.push({ x: px, y: bot });
        }

        const alpha = !audible ? 0.16 : focused ? 1.0 : 0.58;
        const col = SIGNAL_COLORS[layer];

        // Fill
        ctx.save();
        ctx.globalAlpha = alpha * (focused ? 0.27 : 0.14);
        ctx.fillStyle = col;
        ctx.beginPath();
        upperPts.forEach((pt, idx) => {
          if (idx === 0) ctx.moveTo(pt.x, pt.y);
          else ctx.lineTo(pt.x, pt.y);
        });
        for (let s = lowerPts.length - 1; s >= 0; s--) {
          ctx.lineTo(lowerPts[s].x, lowerPts[s].y);
        }
        ctx.closePath();
        ctx.fill();

        // Upper stroke
        ctx.globalAlpha = alpha;
        ctx.strokeStyle = col;
        ctx.lineWidth = focused ? 1.6 : 1.1;
        ctx.beginPath();
        upperPts.forEach((pt, idx) => {
          if (idx === 0) ctx.moveTo(pt.x, pt.y);
          else ctx.lineTo(pt.x, pt.y);
        });
        ctx.stroke();

        // Lower stroke
        ctx.globalAlpha = alpha * 0.67;
        ctx.lineWidth = focused ? 1.15 : 0.85;
        ctx.beginPath();
        lowerPts.forEach((pt, idx) => {
          if (idx === 0) ctx.moveTo(pt.x, pt.y);
          else ctx.lineTo(pt.x, pt.y);
        });
        ctx.stroke();
        ctx.restore();
      }

      if (loadedCount === 0) {
        ctx.fillStyle = 'rgba(202, 206, 232, 0.56)';
        ctx.font = '13px Inter, sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(
          `1 Bar Stacked Canvas (${(barMs / 1000).toFixed(2)}s at ${Math.round(bpm)} BPM) - Load samples into SNAPBACK or TRANSIENT stacks below`,
          plotX + plotW * 0.5,
          plotY + plotH * 0.5
        );
      }

      ctx.restore();
      ctx.restore();
    };

    draw();
    const ro = new ResizeObserver(draw);
    ro.observe(container);
    return () => ro.disconnect();
  }, [
    samples,
    layerParams,
    selectedSnapback,
    selectedTransient,
    bpm,
    preRollMs,
    gridMode,
    zoomFactor,
    viewStartNorm,
  ]);

  const handleWheel = (e: React.WheelEvent<HTMLDivElement>) => {
    if (Math.abs(e.deltaY) < 0.01) return;
    const rect = containerRef.current?.getBoundingClientRect();
    if (!rect) return;
    const span = 1.0 / Math.max(1.0, zoomFactor);
    const anchor = clamp(
      viewStartNorm + ((e.clientX - rect.left) / Math.max(1, rect.width)) * span,
      0,
      1
    );
    const factor = e.deltaY < 0 ? 1.28 : 1 / 1.28;
    const nextZoom = clamp(zoomFactor * factor, 1.0, 32.0);
    const nextSpan = 1.0 / nextZoom;
    const anchorRatio = clamp((anchor - viewStartNorm) / span, 0, 1);
    clampViewport(anchor - anchorRatio * nextSpan, nextZoom);
  };

  return (
    <div
      ref={containerRef}
      className="relative w-full h-full overflow-hidden cursor-ew-resize"
      onWheel={handleWheel}
      onDoubleClick={() => clampViewport(0, 1)}
      onPointerDown={(e) => {
        if (e.button !== 0 || zoomFactor <= 1.01) return;
        (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
        dragRef.current = { startX: e.clientX, startView: viewStartNorm };
      }}
      onPointerMove={(e) => {
        if (!dragRef.current || zoomFactor <= 1.01) return;
        const rect = containerRef.current?.getBoundingClientRect();
        if (!rect) return;
        const dx = e.clientX - dragRef.current.startX;
        const span = 1.0 / zoomFactor;
        clampViewport(
          dragRef.current.startView - (dx / Math.max(1, rect.width)) * span,
          zoomFactor
        );
      }}
      onPointerUp={(e) => {
        dragRef.current = null;
        try {
          (e.currentTarget as HTMLElement).releasePointerCapture(e.pointerId);
        } catch {
          // ignore
        }
      }}
    >
      <canvas ref={canvasRef} className="block w-full h-full" />
    </div>
  );
};
