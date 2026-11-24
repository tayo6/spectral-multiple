import React, { useEffect, useRef, useState } from 'react';
import { clamp, flSegmentLevel } from '../audio/curves';
import { computeEnvelopeSchedule } from '../audio/dspMath';
import {
  barDurationMs,
  computeGridTicks,
  computeLayerBarPlacement,
  computeSecondsTicks,
  TimelineMode,
} from '../audio/timelineMath';
import {
  LayerState,
  NUM_LAYERS,
  PREVIEW_POINTS,
  SIGNAL_COLOURS,
} from '../audio/types';
import {
  clampViewport,
  getViewportSpan,
  normToPlotX,
  panViewport,
  plotXToNorm,
  WaveformViewport,
  zoomAroundNormalized,
} from '../audio/waveformZoom';

interface TopMasterBarCanvasProps {
  layers: LayerState[];
  selectedSnapback: number;
  selectedTransient: number;
  bpm: number;
  timelineMode: TimelineMode;
  viewport: WaveformViewport;
  onChangeViewport: (next: WaveformViewport) => void;
  onSelectLayer: (layerIndex: number) => void;
  triggerPulseAt: number;
}

const RULER_H = 22;
const OVERVIEW_H = 12;

export const TopMasterBarCanvas: React.FC<TopMasterBarCanvasProps> = ({
  layers,
  selectedSnapback,
  selectedTransient,
  bpm,
  timelineMode,
  viewport,
  onChangeViewport,
  onSelectLayer,
  triggerPulseAt,
}) => {
  const containerRef = useRef<HTMLDivElement | null>(null);
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const overviewRef = useRef<HTMLDivElement | null>(null);
  const [draggingPan, setDraggingPan] = useState(false);
  const [panOverview, setPanOverview] = useState(false);
  const dragRef = useRef<{ startX: number; startViewStart: number } | null>(null);

  const viewportRef = useRef(viewport);
  viewportRef.current = viewport;

  useEffect(() => {
    const container = containerRef.current;
    const canvas = canvasRef.current;
    if (!container || !canvas) return;

    const render = () => {
      const rect = container.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      const w = Math.max(1, Math.floor(rect.width));
      const h = Math.max(1, Math.floor(rect.height));

      if (
        canvas.width !== Math.floor(w * dpr) ||
        canvas.height !== Math.floor(h * dpr)
      ) {
        canvas.width = Math.floor(w * dpr);
        canvas.height = Math.floor(h * dpr);
      }

      const ctx = canvas.getContext('2d');
      if (!ctx) return;

      const vp = viewportRef.current;
      const isZoomed = vp.zoom > 1.01;
      const plotX = 0;
      const plotY = RULER_H;
      const plotW = w;
      const plotH = Math.max(36, h - RULER_H - (isZoomed ? OVERVIEW_H : 0));

      ctx.save();
      ctx.scale(dpr, dpr);

      // Deep obsidian Snapback canvas gradient
      const bgGrad = ctx.createLinearGradient(0, 0, 0, h);
      bgGrad.addColorStop(0, '#0f0e14');
      bgGrad.addColorStop(0.5, '#13121a');
      bgGrad.addColorStop(1, '#0d0c12');
      ctx.fillStyle = bgGrad;
      ctx.fillRect(0, 0, w, h);

      // Top ruler bar
      ctx.fillStyle = '#16151f';
      ctx.fillRect(0, 0, w, RULER_H);
      ctx.strokeStyle = 'rgba(155, 160, 210, 0.14)';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(0, RULER_H - 0.5);
      ctx.lineTo(w, RULER_H - 0.5);
      ctx.stroke();

      // Grid or Seconds vertical lines & ruler labels across 1 Bar
      const ticks =
        timelineMode === 'grid'
          ? computeGridTicks(vp.viewStart, vp.zoom)
          : computeSecondsTicks(vp.viewStart, vp.zoom, bpm);

      for (const tick of ticks) {
        const tx = normToPlotX(tick.norm, plotX, plotW, vp);
        if (tx < -2 || tx > w + 2) continue;
        const px = Math.round(tx) + 0.5;

        ctx.strokeStyle = tick.major
          ? 'rgba(160, 166, 224, 0.13)'
          : 'rgba(160, 166, 224, 0.045)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(px, plotY);
        ctx.lineTo(px, plotY + plotH);
        ctx.stroke();

        // Ruler tick mark
        ctx.strokeStyle = tick.major
          ? 'rgba(196, 202, 240, 0.45)'
          : 'rgba(196, 202, 240, 0.18)';
        ctx.beginPath();
        ctx.moveTo(px, tick.major ? RULER_H - 7 : RULER_H - 4);
        ctx.lineTo(px, RULER_H);
        ctx.stroke();

        if (tick.label) {
          ctx.fillStyle = 'rgba(198, 203, 232, 0.72)';
          ctx.font = '500 10px "JetBrains Mono", ui-monospace, monospace';
          ctx.textBaseline = 'middle';
          const nearLeft = tx < 24;
          const nearRight = tx > w - 34;
          ctx.textAlign = nearLeft ? 'left' : nearRight ? 'right' : 'center';
          const lx = nearLeft ? tx + 4 : nearRight ? tx - 4 : tx;
          ctx.fillText(tick.label, lx, RULER_H * 0.44);
        }
      }

      // Horizontal amplitude center line + subtle ±50% lines
      const centre = plotY + plotH * 0.5;
      const halfHeight = plotH * 0.43;

      ctx.strokeStyle = 'rgba(160, 166, 224, 0.04)';
      ctx.beginPath();
      ctx.moveTo(0, Math.round(centre - halfHeight * 0.5) + 0.5);
      ctx.lineTo(w, Math.round(centre - halfHeight * 0.5) + 0.5);
      ctx.moveTo(0, Math.round(centre + halfHeight * 0.5) + 0.5);
      ctx.lineTo(w, Math.round(centre + halfHeight * 0.5) + 0.5);
      ctx.stroke();

      ctx.strokeStyle = 'rgba(180, 186, 235, 0.16)';
      ctx.beginPath();
      ctx.moveTo(0, Math.round(centre) + 0.5);
      ctx.lineTo(w, Math.round(centre) + 0.5);
      ctx.stroke();

      // Trigger Anchor Line (Beat 1.3 = 0.50 on 1-Bar grid, directly above center TRIGGER button)
      const triggerNorm = 0.5;
      const trigX = normToPlotX(triggerNorm, plotX, plotW, vp);
      const pulseFresh = Date.now() - triggerPulseAt < 380;
      if (trigX >= -2 && trigX <= w + 2) {
        const tx = Math.round(trigX) + 0.5;
        ctx.save();
        ctx.strokeStyle = pulseFresh
          ? 'rgba(195, 168, 255, 0.85)'
          : 'rgba(154, 174, 255, 0.34)';
        ctx.lineWidth = pulseFresh ? 1.75 : 1;
        ctx.setLineDash([4, 3]);
        ctx.beginPath();
        ctx.moveTo(tx, plotY);
        ctx.lineTo(tx, plotY + plotH);
        ctx.stroke();
        ctx.restore();
      }

      // Plot all 6 loaded sample stacks inside the 1-Bar window
      ctx.save();
      ctx.beginPath();
      ctx.rect(plotX, plotY, plotW, plotH);
      ctx.clip();

      const anySolo = layers.some((l) => l.params.solo);
      let loadedCount = 0;

      // Render non-focused layers first, then focused Snapback & Transient layers on top
      const drawOrder = Array.from({ length: NUM_LAYERS }, (_, i) => i).sort((a, b) => {
        const aFocus = a === selectedSnapback || a === selectedTransient ? 1 : 0;
        const bFocus = b === selectedSnapback || b === selectedTransient ? 1 : 0;
        return aFocus - bFocus;
      });

      for (const layerIdx of drawOrder) {
        const layer = layers[layerIdx];
        const preview = layer?.preview;
        if (!preview?.loaded) continue;
        loadedCount++;

        const p = layer.params;
        const isAudible = !p.muted && (!anySolo || p.solo);
        const isFocused =
          layerIdx === selectedSnapback || layerIdx === selectedTransient;
        const colour = SIGNAL_COLOURS[layerIdx];

        const trimStart = clamp(p.trimStart, 0, 0.99);
        const trimEnd = clamp(p.trimEnd, trimStart + 0.005, 1);
        const placement = computeLayerBarPlacement(
          preview.durationMs,
          trimStart,
          trimEnd,
          p.pitchSemitones,
          p.delayShiftMs,
          bpm,
          triggerNorm
        );

        const x0 = normToPlotX(placement.startNorm, plotX, plotW, vp);
        const x1 = normToPlotX(placement.endNorm, plotX, plotW, vp);
        if (x1 < plotX - 2 || x0 > plotX + plotW + 2) continue;

        const visibleX0 = Math.max(plotX, Math.floor(x0));
        const visibleX1 = Math.min(plotX + plotW, Math.ceil(x1));
        const pxSpan = Math.max(1, visibleX1 - visibleX0);
        const stepCount = Math.min(512, Math.max(48, Math.round(pxSpan * 1.5)));

        const gainScale = clamp(p.gain, 0, 2);
        const sched = p.envEnabled
          ? computeEnvelopeSchedule(
              p.attackMs,
              p.holdMs,
              p.decayMs,
              p.releaseMs,
              placement.trimmedMs
            )
          : null;
        const sustain = clamp(p.sustain, 0, 1);

        const upperPts: Array<{ x: number; y: number }> = [];
        const lowerPts: Array<{ x: number; y: number }> = [];

        for (let s = 0; s <= stepCount; s++) {
          const px = visibleX0 + (s / stepCount) * pxSpan;
          const normInBar = plotXToNorm(px, plotX, plotW, vp);
          const u = clamp(
            (normInBar - placement.startNorm) / Math.max(1e-6, placement.spanNorm),
            0,
            1
          );
          const posInWave = p.reverse
            ? trimEnd - u * (trimEnd - trimStart)
            : trimStart + u * (trimEnd - trimStart);
          const idx = clamp(
            Math.round(posInWave * (PREVIEW_POINTS - 1)),
            0,
            PREVIEW_POINTS - 1
          );

          const elapsedMs = u * placement.trimmedMs;
          const remainingMs = (1 - u) * placement.trimmedMs;
          const fadeInG =
            p.fadeInMs > 0.5 ? clamp(elapsedMs / p.fadeInMs, 0, 1) : 1;
          const fadeOutG =
            p.fadeOutMs > 0.5 ? clamp(remainingMs / p.fadeOutMs, 0, 1) : 1;

          let flEnv = 1;
          if (sched) {
            if (elapsedMs < sched.attackEndMs) {
              flEnv = flSegmentLevel(
                elapsedMs / Math.max(0.05, sched.attackEndMs),
                0,
                1,
                p.attackCurve,
                p.attackTension
              );
            } else if (elapsedMs < sched.holdEndMs) {
              flEnv = 1;
            } else if (elapsedMs < sched.decayEndMs) {
              flEnv = flSegmentLevel(
                (elapsedMs - sched.holdEndMs) /
                  Math.max(0.05, sched.decayEndMs - sched.holdEndMs),
                1,
                sustain,
                p.decayCurve,
                p.decayTension
              );
            } else if (elapsedMs < sched.releaseStartMs) {
              flEnv = sustain;
            } else {
              flEnv = flSegmentLevel(
                (elapsedMs - sched.releaseStartMs) /
                  Math.max(0.1, sched.releaseMs),
                sustain,
                0,
                p.releaseCurve,
                p.releaseTension
              );
            }
          }

          const env = gainScale * fadeInG * fadeOutG * flEnv;
          const maxV = clamp(preview.maximum[idx] * env, -1, 1);
          const minV = clamp(preview.minimum[idx] * env, -1, 1);
          upperPts.push({ x: px, y: centre - maxV * halfHeight });
          lowerPts.push({ x: px, y: centre - minV * halfHeight });
        }

        const alphaScale = !isAudible ? 0.16 : isFocused ? 1.0 : 0.58;
        ctx.save();
        ctx.globalAlpha = alphaScale;

        // Filled translucent body
        ctx.beginPath();
        upperPts.forEach((pt, i) => {
          if (i === 0) ctx.moveTo(pt.x, pt.y);
          else ctx.lineTo(pt.x, pt.y);
        });
        for (let i = lowerPts.length - 1; i >= 0; i--) {
          ctx.lineTo(lowerPts[i].x, lowerPts[i].y);
        }
        ctx.closePath();
        ctx.fillStyle = colour + (isFocused ? '45' : '24');
        ctx.fill();

        // Crisp upper contour
        ctx.beginPath();
        upperPts.forEach((pt, i) => {
          if (i === 0) ctx.moveTo(pt.x, pt.y);
          else ctx.lineTo(pt.x, pt.y);
        });
        ctx.strokeStyle = colour;
        ctx.lineWidth = isFocused ? 1.6 : 1.1;
        ctx.stroke();

        // Crisp lower contour
        ctx.beginPath();
        lowerPts.forEach((pt, i) => {
          if (i === 0) ctx.moveTo(pt.x, pt.y);
          else ctx.lineTo(pt.x, pt.y);
        });
        ctx.strokeStyle = colour + 'aa';
        ctx.lineWidth = isFocused ? 1.15 : 0.85;
        ctx.stroke();

        ctx.restore();
      }

      if (loadedCount === 0) {
        const barSec = (barDurationMs(bpm) / 1000).toFixed(2);
        ctx.fillStyle = 'rgba(202, 206, 232, 0.56)';
        ctx.font = '500 13px "Plus Jakarta Sans", -apple-system, sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(
          `1 Bar Stacked Canvas (${barSec}s at ${bpm} BPM) — load samples into SNAPBACK or TRANSIENT stacks below`,
          plotX + plotW * 0.5,
          centre - 6
        );
        ctx.fillStyle = 'rgba(152, 156, 188, 0.44)';
        ctx.font = '400 11px "JetBrains Mono", monospace';
        ctx.fillText(
          'Scroll or use Zoom to inspect Grid / Seconds · Click colored circles below to edit each stack envelope',
          plotX + plotW * 0.5,
          centre + 16
        );
      }

      ctx.restore();

      // Compact bottom-left stack legend dots inside plot corner (clickable layers are below, this just identifies waves when > 0 loaded)
      if (loadedCount > 0) {
        let lx = 12;
        const ly = plotY + plotH - 12;
        for (let i = 0; i < NUM_LAYERS; i++) {
          if (!layers[i]?.preview.loaded) continue;
          const isFocus = i === selectedSnapback || i === selectedTransient;
          const label = i < 3 ? `SB${i + 1}` : `TR${i - 2}`;
          ctx.fillStyle = SIGNAL_COLOURS[i];
          ctx.beginPath();
          ctx.arc(lx + 4, ly, isFocus ? 4 : 3, 0, Math.PI * 2);
          ctx.fill();
          ctx.fillStyle = isFocus ? '#f3f4ff' : 'rgba(204, 208, 234, 0.65)';
          ctx.font = '600 9.5px "JetBrains Mono", monospace';
          ctx.textAlign = 'left';
          ctx.textBaseline = 'middle';
          ctx.fillText(label, lx + 11, ly + 0.5);
          lx += 40;
        }
      }

      ctx.restore();
    };

    render();
    const ro = new ResizeObserver(render);
    ro.observe(container);
    return () => ro.disconnect();
  }, [
    layers,
    selectedSnapback,
    selectedTransient,
    bpm,
    timelineMode,
    viewport,
    triggerPulseAt,
  ]);

  const handleWheel = (e: React.WheelEvent<HTMLCanvasElement>) => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    e.preventDefault();
    const rect = canvas.getBoundingClientRect();
    const anchorNorm = plotXToNorm(
      e.clientX - rect.left,
      0,
      Math.max(1, rect.width),
      viewportRef.current
    );
    const factor = e.deltaY < 0 ? 1.28 : 1 / 1.28;
    onChangeViewport(zoomAroundNormalized(viewportRef.current, anchorNorm, factor));
  };

  const handlePointerDown = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (e.button === 1 || (e.button === 0 && viewportRef.current.zoom > 1.01)) {
      dragRef.current = {
        startX: e.clientX,
        startViewStart: viewportRef.current.viewStart,
      };
      setDraggingPan(true);
      e.currentTarget.setPointerCapture(e.pointerId);
    } else if (e.button === 0 && canvasRef.current) {
      // Clicking near a loaded wave selects its closest layer
      const rect = canvasRef.current.getBoundingClientRect();
      const normX = plotXToNorm(
        e.clientX - rect.left,
        0,
        Math.max(1, rect.width),
        viewportRef.current
      );
      let bestLayer = -1;
      let bestDist = Infinity;
      for (let i = 0; i < NUM_LAYERS; i++) {
        const l = layers[i];
        if (!l?.preview.loaded) continue;
        const p = computeLayerBarPlacement(
          l.preview.durationMs,
          l.params.trimStart,
          l.params.trimEnd,
          l.params.pitchSemitones,
          l.params.delayShiftMs,
          bpm,
          0.5
        );
        if (normX >= p.startNorm && normX <= p.endNorm) {
          const mid = (p.startNorm + p.endNorm) * 0.5;
          const d = Math.abs(normX - mid);
          if (d < bestDist) {
            bestDist = d;
            bestLayer = i;
          }
        }
      }
      if (bestLayer >= 0) onSelectLayer(bestLayer);
    }
  };

  const handlePointerMove = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (!dragRef.current || !canvasRef.current) return;
    const rect = canvasRef.current.getBoundingClientRect();
    const dx = e.clientX - dragRef.current.startX;
    const span = getViewportSpan(viewportRef.current.zoom);
    const deltaNorm = -(dx / Math.max(1, rect.width)) * span;
    onChangeViewport(
      panViewport(
        { viewStart: dragRef.current.startViewStart, zoom: viewportRef.current.zoom },
        deltaNorm
      )
    );
  };

  const handlePointerUp = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (dragRef.current) {
      dragRef.current = null;
      setDraggingPan(false);
      if (e.currentTarget.hasPointerCapture(e.pointerId)) {
        e.currentTarget.releasePointerCapture(e.pointerId);
      }
    }
  };

  const updateOverviewFromClientX = (clientX: number) => {
    const bar = overviewRef.current;
    if (!bar) return;
    const rect = bar.getBoundingClientRect();
    const norm = clamp((clientX - rect.left) / Math.max(1, rect.width), 0, 1);
    const span = getViewportSpan(viewportRef.current.zoom);
    onChangeViewport(clampViewport(norm - span * 0.5, viewportRef.current.zoom));
  };

  const isZoomed = viewport.zoom > 1.01;
  const vpSpan = getViewportSpan(viewport.zoom);

  return (
    <div ref={containerRef} className="w-full h-full relative select-none">
      <canvas
        ref={canvasRef}
        data-testid="master-bar-canvas"
        onWheel={handleWheel}
        onDoubleClick={() => onChangeViewport({ viewStart: 0, zoom: 1 })}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onPointerCancel={handlePointerUp}
        className={`block w-full h-full touch-none ${
          draggingPan
            ? 'cursor-grabbing'
            : isZoomed
              ? 'cursor-grab'
              : 'cursor-crosshair'
        }`}
      />
      {isZoomed && (
        <div
          ref={overviewRef}
          data-testid="master-overview-bar"
          onPointerDown={(e) => {
            if (e.button !== 0) return;
            setPanOverview(true);
            updateOverviewFromClientX(e.clientX);
            e.currentTarget.setPointerCapture(e.pointerId);
          }}
          onPointerMove={(e) => {
            if (panOverview) updateOverviewFromClientX(e.clientX);
          }}
          onPointerUp={(e) => {
            if (panOverview) {
              setPanOverview(false);
              if (e.currentTarget.hasPointerCapture(e.pointerId)) {
                e.currentTarget.releasePointerCapture(e.pointerId);
              }
            }
          }}
          onPointerCancel={() => setPanOverview(false)}
          style={{ height: OVERVIEW_H }}
          title="Drag to pan across 1 Bar"
          className="absolute bottom-0 left-0 right-0 z-10 bg-[#14131b]/95 border-t border-[#2b2938] cursor-ew-resize"
        >
          <div
            data-testid="master-overview-thumb"
            style={{
              left: `${viewport.viewStart * 100}%`,
              width: `${Math.max(3, vpSpan * 100)}%`,
            }}
            className="absolute top-[1px] bottom-[1px] rounded-[2px] border border-[#9aaeff]/75 bg-[#7d92ff]/25"
          />
        </div>
      )}
    </div>
  );
};
