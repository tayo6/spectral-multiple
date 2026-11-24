import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  clamp,
  computeEnvelopeSchedule,
  envelopeLevelAtMs,
  evaluateCompleteEnvelopeAtMs,
  flSegmentLevel,
  formatEnvelopeTime,
  formatTrimPercent,
} from '../engine/envelopeMath';
import {
  FL_CURVE_NAMES,
  LayerParameters,
  LayerSampleInfo,
  PREVIEW_POINTS,
  SIGNAL_COLORS,
} from '../engine/types';
import { RotaryKnob } from './RotaryKnob';

interface LayerEnvelopeComponentProps {
  layer: number;
  params: LayerParameters;
  sample: LayerSampleInfo;
  onChangeParams: (patch: Partial<LayerParameters>) => void;
  onDropAudioFile: (file: File) => void;
}

type DragHandle =
  | 'None'
  | 'TrimStart'
  | 'TrimEnd'
  | 'Attack'
  | 'Hold'
  | 'Decay'
  | 'Sustain'
  | 'Release'
  | 'TensionAttack'
  | 'TensionDecay'
  | 'TensionRelease'
  | 'Pan'
  | 'OverviewPan';

type Segment = 'Attack' | 'Decay' | 'Release';

interface Point {
  x: number;
  y: number;
}

interface PlotRect {
  x: number;
  y: number;
  w: number;
  h: number;
}

interface Geometry {
  plot: PlotRect;
  trimX0: number;
  trimX1: number;
  trimW: number;
  trimmedDurationMs: number;
  attackEndX: number;
  holdEndX: number;
  releaseStartX: number;
  attack: Point;
  hold: Point;
  decay: Point;
  sustain: Point;
  release: Point;
  tensionAttack: Point;
  tensionDecay: Point;
  tensionRelease: Point;
  tensionAttackVisible: boolean;
  tensionDecayVisible: boolean;
  tensionReleaseVisible: boolean;
  overviewRect: PlotRect | null;
}

const MAX_ZOOM = 32.0;
const HEADER_H = 22;
const RULER_H = 16;
const OVERVIEW_H = 14;
const HANDLE_R = 4.6;
const TENSION_R = 4.6;
const CURVE_STEPS = 96;

export const LayerEnvelopeComponent: React.FC<LayerEnvelopeComponentProps> = ({
  layer,
  params,
  sample,
  onChangeParams,
  onDropAudioFile,
}) => {
  const isPre = layer < 3;
  const traceColor = SIGNAL_COLORS[layer];
  const flCurveColour = isPre ? '#a78bfa' : '#d08aff';
  const flControlDot = isPre ? '#e0d6ff' : '#ffdfff';
  const simWave = isPre ? '#a99aff' : '#d18cff';
  const secondaryAccent = isPre ? '#7866d8' : '#a95fca';

  const waveWrapRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);

  const [zoomFactor, setZoomFactor] = useState(1.0);
  const [viewStartNorm, setViewStartNorm] = useState(0.0);
  const [contextMenu, setContextMenu] = useState<{
    x: number;
    y: number;
    segment: Segment;
    title: string;
  } | null>(null);
  const [isDragOver, setIsDragOver] = useState(false);

  const dragStateRef = useRef<{
    handle: DragHandle;
    startX: number;
    startY: number;
    startTension: number;
    startView: number;
  }>({
    handle: 'None',
    startX: 0,
    startY: 0,
    startTension: 0,
    startView: 0,
  });

  const totalDurationMs = sample.loaded ? sample.durationMs : 1000.0;

  const clampViewport = useCallback((nextStart: number, nextZoom: number) => {
    const safeZoom = clamp(nextZoom, 1.0, MAX_ZOOM);
    if (safeZoom <= 1.0001) {
      setZoomFactor(1.0);
      setViewStartNorm(0.0);
    } else {
      setZoomFactor(safeZoom);
      const maxStart = Math.max(0.0, 1.0 - 1.0 / safeZoom);
      setViewStartNorm(clamp(nextStart, 0.0, maxStart));
    }
  }, []);

  const zoomAroundNormalized = useCallback(
    (anchorNorm: number, factor: number) => {
      const curSpan = 1.0 / Math.max(1.0, zoomFactor);
      const safeAnchor = clamp(anchorNorm, 0.0, 1.0);
      const anchorRatio = clamp((safeAnchor - viewStartNorm) / curSpan, 0.0, 1.0);
      const nextZoom = clamp(zoomFactor * (factor > 0 ? factor : 1.0), 1.0, MAX_ZOOM);
      const nextSpan = 1.0 / nextZoom;
      clampViewport(safeAnchor - anchorRatio * nextSpan, nextZoom);
    },
    [zoomFactor, viewStartNorm, clampViewport]
  );

  const focusViewportAt = useCallback(
    (targetNorm: number, defaultZoomIfUnzoomed = 4.0) => {
      const nextZoom =
        zoomFactor <= 1.05 ? clamp(defaultZoomIfUnzoomed, 1.0, MAX_ZOOM) : zoomFactor;
      const span = 1.0 / nextZoom;
      clampViewport(clamp(targetNorm, 0.0, 1.0) - span * 0.5, nextZoom);
    },
    [zoomFactor, clampViewport]
  );

  const zoomToFitTrim = useCallback(() => {
    const t0 = clamp(params.trimStart, 0, 0.99);
    const t1 = clamp(params.trimEnd, t0 + 0.005, 1.0);
    const span = Math.max(0.01, t1 - t0);
    if (span >= 0.96) {
      clampViewport(0, 1);
      return;
    }
    const pad = span * 0.08;
    const paddedStart = Math.max(0, t0 - pad);
    const paddedEnd = Math.min(1, t1 + pad);
    const targetSpan = Math.max(1 / MAX_ZOOM, paddedEnd - paddedStart);
    clampViewport(paddedStart, clamp(1 / targetSpan, 1, MAX_ZOOM));
  }, [params.trimStart, params.trimEnd, clampViewport]);

  const normToPlotX = useCallback(
    (norm: number, plot: PlotRect) => {
      const span = 1.0 / Math.max(1.0, zoomFactor);
      return plot.x + ((norm - viewStartNorm) / span) * plot.w;
    },
    [zoomFactor, viewStartNorm]
  );

  const plotXToNorm = useCallback(
    (x: number, plot: PlotRect) => {
      const span = 1.0 / Math.max(1.0, zoomFactor);
      const rel = (x - plot.x) / Math.max(1.0, plot.w);
      return clamp(viewStartNorm + rel * span, 0.0, 1.0);
    },
    [zoomFactor, viewStartNorm]
  );

  const getGeometry = useCallback(
    (w: number, h: number): Geometry => {
      const isZoomed = zoomFactor > 1.01;
      const plotY = HEADER_H;
      const plotH = Math.max(24, h - HEADER_H - RULER_H - (isZoomed ? OVERVIEW_H : 0));
      const plot: PlotRect = { x: 0, y: plotY, w, h: plotH };
      const overviewRect: PlotRect | null = isZoomed
        ? { x: 0, y: plotY + plotH, w, h: OVERVIEW_H }
        : null;

      const trimStart = clamp(params.trimStart, 0, 0.99);
      const trimEnd = clamp(params.trimEnd, trimStart + 0.005, 1.0);
      const trimX0 = normToPlotX(trimStart, plot);
      const trimX1 = normToPlotX(trimEnd, plot);
      const trimW = Math.max(4.0, trimX1 - trimX0);

      const safeTotal = Math.max(1.0, totalDurationMs);
      const pitchSpeed = Math.pow(2.0, params.pitchSemitones / 12.0);
      const trimmedDurationMs = Math.max(
        1.0,
        (safeTotal * (trimEnd - trimStart)) / Math.max(0.01, pitchSpeed)
      );

      const envPreviewParams: LayerParameters = { ...params, envEnabled: true };
      const sched = computeEnvelopeSchedule(envPreviewParams, trimmedDurationMs);
      const attackEnd = sched.attackEndMs;
      const holdEnd = sched.holdEndMs;
      const decayEnd = sched.decayEndMs;
      const releaseStart = sched.releaseStartMs;
      const sustain = clamp(params.sustain, 0, 1);

      const xAt = (ms: number) =>
        trimX0 + (trimW * clamp(ms, 0, trimmedDurationMs)) / trimmedDurationMs;
      const envTop = plot.y + 7.0;
      const envBot = plot.y + plot.h - 15.0;
      const envH = Math.max(12.0, envBot - envTop);
      const yAt = (level: number) => envBot - envH * clamp(level, 0, 1);
      const levelAt = (ms: number) => envelopeLevelAtMs(envPreviewParams, sched, ms);

      const attack: Point = { x: xAt(attackEnd), y: yAt(levelAt(attackEnd)) };
      const hold: Point = { x: xAt(holdEnd), y: yAt(levelAt(holdEnd)) };
      const decay: Point = { x: xAt(decayEnd), y: yAt(levelAt(decayEnd)) };
      const sustainMid = decayEnd + (releaseStart - decayEnd) * 0.5;
      const sustainPt: Point = { x: xAt(sustainMid), y: yAt(sustain) };
      const relStartLevel = levelAt(releaseStart);
      const release: Point = { x: xAt(releaseStart), y: yAt(relStartLevel) };

      const attMidMs = attackEnd * 0.5;
      const tensionAttack: Point = {
        x: xAt(attMidMs),
        y: yAt(flSegmentLevel(0.5, 0.0, 1.0, params.attackCurve, params.attackTension)),
      };
      const decMidMs = holdEnd + (decayEnd - holdEnd) * 0.5;
      const tensionDecay: Point = {
        x: xAt(decMidMs),
        y: yAt(flSegmentLevel(0.5, 1.0, sustain, params.decayCurve, params.decayTension)),
      };
      const relMidMs = releaseStart + sched.releaseMs * 0.5;
      const tensionRelease: Point = {
        x: xAt(relMidMs),
        y: yAt(
          flSegmentLevel(0.5, relStartLevel, 0.0, params.releaseCurve, params.releaseTension)
        ),
      };

      return {
        plot,
        trimX0,
        trimX1,
        trimW,
        trimmedDurationMs,
        attackEndX: attack.x,
        holdEndX: hold.x,
        releaseStartX: release.x,
        attack,
        hold,
        decay,
        sustain: sustainPt,
        release,
        tensionAttack,
        tensionDecay,
        tensionRelease,
        tensionAttackVisible: attack.x - trimX0 >= 10.0,
        tensionDecayVisible: decay.x - hold.x >= 10.0,
        tensionReleaseVisible: trimX1 - release.x >= 10.0,
        overviewRect,
      };
    },
    [params, zoomFactor, totalDurationMs, normToPlotX]
  );

  // Canvas draw
  useEffect(() => {
    const wrap = waveWrapRef.current;
    const canvas = canvasRef.current;
    if (!wrap || !canvas) return;

    const draw = () => {
      const rect = wrap.getBoundingClientRect();
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
      bgGrad.addColorStop(0, isPre ? '#251a3d' : '#2b1938');
      bgGrad.addColorStop(1, isPre ? '#171127' : '#1a1025');
      ctx.fillStyle = bgGrad;
      ctx.fillRect(0, 0, w, h);

      const g = getGeometry(w, h);
      const plot = g.plot;
      const vpSpan = 1.0 / Math.max(1.0, zoomFactor);
      const vpEnd = Math.min(1.0, viewStartNorm + vpSpan);

      if (plot.w >= 24 && plot.h >= 24) {
        ctx.save();
        ctx.beginPath();
        ctx.rect(plot.x, plot.y, plot.w, plot.h);
        ctx.clip();

        // Overlay tint
        ctx.fillStyle = params.envEnabled
          ? 'rgba(42, 33, 64, 0.42)'
          : 'rgba(21, 16, 34, 0.34)';
        ctx.fillRect(plot.x, plot.y, plot.w, plot.h);

        // Adaptive grid lines
        ctx.strokeStyle = 'rgba(186, 164, 255, 0.11)';
        ctx.lineWidth = 1;
        const gridDivs = clamp(
          8 * Math.pow(2, Math.floor(Math.log2(Math.max(1.0, zoomFactor)))),
          8,
          256
        );
        const firstGrid = Math.max(1, Math.ceil(viewStartNorm * gridDivs));
        const lastGrid = Math.min(gridDivs - 1, Math.floor(vpEnd * gridDivs));
        for (let idx = firstGrid; idx <= lastGrid; idx++) {
          const x = Math.round(normToPlotX(idx / gridDivs, plot)) + 0.5;
          ctx.beginPath();
          ctx.moveTo(x, plot.y);
          ctx.lineTo(x, plot.y + plot.h);
          ctx.stroke();
        }
        for (let r = 1; r < 4; r++) {
          ctx.strokeStyle =
            r === 2 ? 'rgba(212, 189, 255, 0.21)' : 'rgba(186, 164, 255, 0.09)';
          const y = Math.round(plot.y + (plot.h * r) / 4) + 0.5;
          ctx.beginPath();
          ctx.moveTo(plot.x, y);
          ctx.lineTo(plot.x + plot.w, y);
          ctx.stroke();
        }

        // Waveform
        if (sample.loaded && sample.preview.loaded) {
          const centre = plot.y + plot.h * 0.5;
          const amplitude = plot.h * 0.44;
          ctx.strokeStyle = simWave;
          ctx.globalAlpha = 0.88;
          ctx.lineWidth = 1;
          const cols = Math.max(1, Math.floor(plot.w));
          ctx.beginPath();
          for (let col = 0; col < cols; col++) {
            const n0 = viewStartNorm + (col / cols) * vpSpan;
            const n1 = viewStartNorm + ((col + 1) / cols) * vpSpan;
            const b0 = clamp(Math.floor(n0 * PREVIEW_POINTS), 0, PREVIEW_POINTS - 1);
            const b1 = clamp(Math.floor(n1 * PREVIEW_POINTS), b0, PREVIEW_POINTS - 1);
            let minVal = 1.0;
            let maxVal = -1.0;
            for (let b = b0; b <= b1; b++) {
              const srcIdx = params.reverse ? PREVIEW_POINTS - 1 - b : b;
              if (sample.preview.minimum[srcIdx] < minVal) minVal = sample.preview.minimum[srcIdx];
              if (sample.preview.maximum[srcIdx] > maxVal) maxVal = sample.preview.maximum[srcIdx];
            }
            const x = plot.x + col + 0.5;
            const top = centre - maxVal * amplitude;
            const bot = Math.max(top + 1.0, centre - minVal * amplitude);
            ctx.moveTo(x, top);
            ctx.lineTo(x, bot);
          }
          ctx.stroke();
          ctx.globalAlpha = 1.0;
        } else {
          ctx.fillStyle = 'rgba(215, 226, 240, 0.54)';
          ctx.font = '12px Inter, sans-serif';
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          ctx.fillText(
            'Drop Sample Here or Click Load...',
            plot.x + plot.w * 0.5,
            plot.y + plot.h * 0.5
          );
        }

        // Dim trimmed-out front and back regions
        ctx.fillStyle = 'rgba(8, 10, 14, 0.62)';
        const leftDimEnd = Math.min(plot.x + plot.w, g.trimX0);
        if (leftDimEnd > plot.x) {
          ctx.fillRect(plot.x, plot.y, leftDimEnd - plot.x, plot.h);
        }
        const rightDimStart = Math.max(plot.x, g.trimX1);
        if (rightDimStart < plot.x + plot.w) {
          ctx.fillRect(rightDimStart, plot.y, plot.x + plot.w - rightDimStart, plot.h);
        }

        // Fade-in & fade-out slopes
        const fadeInPx = clamp((params.fadeInMs / g.trimmedDurationMs) * g.trimW, 0, g.trimW);
        const fadeOutPx = clamp((params.fadeOutMs / g.trimmedDurationMs) * g.trimW, 0, g.trimW);
        if (fadeInPx > 1.0) {
          ctx.fillStyle = 'rgba(0,0,0,0.34)';
          ctx.beginPath();
          ctx.moveTo(g.trimX0, plot.y);
          ctx.lineTo(g.trimX0 + fadeInPx, plot.y);
          ctx.lineTo(g.trimX0, plot.y + plot.h);
          ctx.closePath();
          ctx.fill();

          ctx.strokeStyle = 'rgba(212, 185, 255, 0.85)';
          ctx.lineWidth = 1.25;
          ctx.beginPath();
          ctx.moveTo(g.trimX0, plot.y + plot.h);
          ctx.lineTo(g.trimX0 + fadeInPx, plot.y);
          ctx.stroke();
        }
        if (fadeOutPx > 1.0) {
          ctx.fillStyle = 'rgba(0,0,0,0.34)';
          ctx.beginPath();
          ctx.moveTo(g.trimX1, plot.y);
          ctx.lineTo(g.trimX1 - fadeOutPx, plot.y);
          ctx.lineTo(g.trimX1, plot.y + plot.h);
          ctx.closePath();
          ctx.fill();

          ctx.strokeStyle = 'rgba(212, 185, 255, 0.85)';
          ctx.lineWidth = 1.25;
          ctx.beginPath();
          ctx.moveTo(g.trimX1 - fadeOutPx, plot.y);
          ctx.lineTo(g.trimX1, plot.y + plot.h);
          ctx.stroke();
        }

        // FL Envelope Curve
        const envPreviewParams: LayerParameters = { ...params, envEnabled: true };
        const sched = computeEnvelopeSchedule(envPreviewParams, g.trimmedDurationMs);
        const xAt = (ms: number) =>
          g.trimX0 + (g.trimW * clamp(ms, 0, g.trimmedDurationMs)) / g.trimmedDurationMs;
        const envTop = plot.y + 7.0;
        const envBot = plot.y + plot.h - 15.0;
        const envH = Math.max(12.0, envBot - envTop);
        const yAt = (level: number) => envBot - envH * clamp(level, 0, 1);

        const envPts: Point[] = [];
        envPts.push({
          x: xAt(0),
          y: yAt(evaluateCompleteEnvelopeAtMs(envPreviewParams, sched, 0)),
        });
        for (let i = 1; i <= CURVE_STEPS; i++) {
          const tMs = sched.attackEndMs * (i / CURVE_STEPS);
          envPts.push({ x: xAt(tMs), y: yAt(envelopeLevelAtMs(envPreviewParams, sched, tMs)) });
        }
        envPts.push({
          x: xAt(sched.holdEndMs),
          y: yAt(envelopeLevelAtMs(envPreviewParams, sched, sched.holdEndMs)),
        });
        for (let i = 1; i <= CURVE_STEPS; i++) {
          const tMs =
            sched.holdEndMs + (sched.decayEndMs - sched.holdEndMs) * (i / CURVE_STEPS);
          envPts.push({ x: xAt(tMs), y: yAt(envelopeLevelAtMs(envPreviewParams, sched, tMs)) });
        }
        const relStartLevel = envelopeLevelAtMs(envPreviewParams, sched, sched.releaseStartMs);
        envPts.push({ x: xAt(sched.releaseStartMs), y: yAt(relStartLevel) });
        for (let i = 1; i <= CURVE_STEPS; i++) {
          const tMs = sched.releaseStartMs + sched.releaseMs * (i / CURVE_STEPS);
          envPts.push({
            x: xAt(tMs),
            y: yAt(evaluateCompleteEnvelopeAtMs(envPreviewParams, sched, tMs)),
          });
        }

        // Section Markers [D] and [S]
        const drawSectionMarker = (mx: number, tag: string) => {
          if (mx < plot.x - 6 || mx > plot.x + plot.w + 6) return;
          ctx.strokeStyle = params.envEnabled
            ? 'rgba(235, 242, 250, 0.55)'
            : 'rgba(200, 210, 224, 0.25)';
          ctx.lineWidth = 1;
          ctx.beginPath();
          ctx.moveTo(Math.round(mx) + 0.5, plot.y);
          ctx.lineTo(Math.round(mx) + 0.5, plot.y + plot.h);
          ctx.stroke();

          const bx = clamp(mx + 1, plot.x + 2, plot.x + plot.w - 14);
          const by = plot.y + plot.h - 13;
          ctx.fillStyle = params.envEnabled ? '#b9c3cf' : '#58626e';
          ctx.beginPath();
          ctx.roundRect(bx, by, 12, 12, 2);
          ctx.fill();
          ctx.fillStyle = '#1d172b';
          ctx.font = 'bold 8.5px Inter, sans-serif';
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          ctx.fillText(tag, bx + 6, by + 6.5);
        };

        drawSectionMarker(g.holdEndX, 'D');
        drawSectionMarker(g.releaseStartX, 'S');

        // Fill under envelope
        ctx.save();
        ctx.beginPath();
        envPts.forEach((pt, idx) => {
          if (idx === 0) ctx.moveTo(pt.x, pt.y);
          else ctx.lineTo(pt.x, pt.y);
        });
        ctx.lineTo(g.trimX1, envBot);
        ctx.lineTo(g.trimX0, envBot);
        ctx.closePath();
        ctx.fillStyle = params.envEnabled ? flCurveColour : 'rgba(160, 170, 185, 0.06)';
        ctx.globalAlpha = params.envEnabled ? 0.16 : 1.0;
        ctx.fill();
        ctx.restore();

        // Envelope stroke
        ctx.save();
        ctx.strokeStyle = params.envEnabled ? flCurveColour : 'rgba(180, 192, 206, 0.45)';
        ctx.lineWidth = params.envEnabled ? 2.0 : 1.4;
        ctx.lineJoin = 'round';
        ctx.lineCap = 'round';
        ctx.beginPath();
        envPts.forEach((pt, idx) => {
          if (idx === 0) ctx.moveTo(pt.x, pt.y);
          else ctx.lineTo(pt.x, pt.y);
        });
        ctx.stroke();
        ctx.restore();

        // Trim bars & flags
        ctx.strokeStyle = '#b79aff';
        ctx.fillStyle = '#b79aff';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(g.trimX0, plot.y);
        ctx.lineTo(g.trimX0, plot.y + plot.h);
        ctx.moveTo(g.trimX1, plot.y);
        ctx.lineTo(g.trimX1, plot.y + plot.h);
        ctx.stroke();

        ctx.beginPath();
        ctx.moveTo(g.trimX0, plot.y);
        ctx.lineTo(g.trimX0 + 11, plot.y + 6);
        ctx.lineTo(g.trimX0, plot.y + 12);
        ctx.closePath();
        ctx.fill();

        ctx.beginPath();
        ctx.moveTo(g.trimX1, plot.y);
        ctx.lineTo(g.trimX1 - 11, plot.y + 6);
        ctx.lineTo(g.trimX1, plot.y + 12);
        ctx.closePath();
        ctx.fill();

        // Tension handles (hollow circles)
        const drawTensionHandle = (pt: Point, visible: boolean) => {
          if (!visible) return;
          ctx.fillStyle = '#201a30';
          ctx.beginPath();
          ctx.arc(pt.x, pt.y, TENSION_R, 0, Math.PI * 2);
          ctx.fill();
          ctx.strokeStyle = params.envEnabled ? flCurveColour : 'rgba(180, 192, 206, 0.5)';
          ctx.lineWidth = 1.7;
          ctx.stroke();
        };
        drawTensionHandle(g.tensionAttack, g.tensionAttackVisible);
        drawTensionHandle(g.tensionDecay, g.tensionDecayVisible);
        drawTensionHandle(g.tensionRelease, g.tensionReleaseVisible);

        // Control points (A, H, D, S, R)
        const drawControlPoint = (pt: Point, label: string) => {
          if (pt.x < plot.x - 12 || pt.x > plot.x + plot.w + 12) return;
          ctx.fillStyle = '#181321';
          ctx.beginPath();
          ctx.arc(pt.x, pt.y, HANDLE_R + 1.5, 0, Math.PI * 2);
          ctx.fill();

          ctx.fillStyle = params.envEnabled ? flControlDot : '#8a95a5';
          ctx.beginPath();
          ctx.arc(pt.x, pt.y, HANDLE_R, 0, Math.PI * 2);
          ctx.fill();

          ctx.fillStyle = '#eef3fa';
          ctx.font = 'bold 9px Inter, sans-serif';
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          const nearTop = pt.y - plot.y < 18;
          const labelX = clamp(pt.x, plot.x + 8, plot.x + plot.w - 8);
          const labelY = nearTop ? pt.y + HANDLE_R + 7 : pt.y - HANDLE_R - 7;
          ctx.fillText(label, labelX, labelY);
        };
        drawControlPoint(g.attack, 'A');
        drawControlPoint(g.hold, 'H');
        drawControlPoint(g.decay, 'D');
        drawControlPoint(g.sustain, 'S');
        drawControlPoint(g.release, 'R');

        ctx.restore();
      }

      // Overview scrollbar when zoomed
      if (g.overviewRect) {
        const ov = g.overviewRect;
        ctx.fillStyle = 'rgba(13, 17, 23, 0.9)';
        ctx.fillRect(ov.x, ov.y, ov.w, ov.h);
        ctx.strokeStyle = 'rgba(255,255,255,0.1)';
        ctx.beginPath();
        ctx.moveTo(ov.x, ov.y + 0.5);
        ctx.lineTo(ov.x + ov.w, ov.y + 0.5);
        ctx.stroke();

        const trimStart = clamp(params.trimStart, 0, 0.99);
        const trimEnd = clamp(params.trimEnd, trimStart + 0.005, 1.0);
        const trimBoxX = ov.x + trimStart * ov.w;
        const trimBoxW = Math.max(2, (trimEnd - trimStart) * ov.w);
        ctx.fillStyle = 'rgba(192, 156, 255, 0.3)';
        ctx.fillRect(trimBoxX, ov.y + 4, trimBoxW, ov.h - 8);

        const thumbX = ov.x + viewStartNorm * ov.w;
        const thumbW = Math.max(10, vpSpan * ov.w);
        ctx.fillStyle = 'rgba(255,255,255,0.2)';
        ctx.beginPath();
        ctx.roundRect(thumbX, ov.y + 1.5, thumbW, ov.h - 3, 2);
        ctx.fill();
        ctx.strokeStyle = 'rgba(220, 230, 242, 0.8)';
        ctx.lineWidth = 1;
        ctx.stroke();
      }

      // Bottom time ruler
      const rulerY = h - RULER_H;
      ctx.fillStyle = 'rgba(10, 14, 20, 0.52)';
      ctx.fillRect(0, rulerY, w, RULER_H);
      ctx.fillStyle = '#b9c6d6';
      ctx.font = '9.5px Inter, sans-serif';
      ctx.textBaseline = 'middle';
      for (let i = 0; i <= 4; i++) {
        const norm = viewStartNorm + vpSpan * (i / 4);
        const ms = Math.round(totalDurationMs * norm);
        const lbl =
          ms === 0
            ? '0:00'
            : `0:${String(Math.floor(ms / 1000)).padStart(2, '0')}:${String(ms % 1000).padStart(3, '0')}`;
        const tx = (w * i) / 4;
        if (i === 0) {
          ctx.textAlign = 'left';
          ctx.fillText(lbl, tx + 4, rulerY + RULER_H / 2);
        } else if (i === 4) {
          ctx.textAlign = 'right';
          ctx.fillText(lbl, tx - 4, rulerY + RULER_H / 2);
        } else {
          ctx.textAlign = 'center';
          ctx.fillText(lbl, tx, rulerY + RULER_H / 2);
        }
      }

      ctx.restore();
    };

    draw();
    const ro = new ResizeObserver(draw);
    ro.observe(wrap);
    return () => ro.disconnect();
  }, [
    isPre,
    simWave,
    flCurveColour,
    flControlDot,
    params,
    sample,
    zoomFactor,
    viewStartNorm,
    totalDurationMs,
    getGeometry,
    normToPlotX,
  ]);

  const hitTestHandle = (
    pos: Point,
    g: Geometry,
    maxRadius = 15.0
  ): DragHandle => {
    const candidates: Array<{ h: DragHandle; pt: Point; active: boolean }> = [
      { h: 'TensionAttack', pt: g.tensionAttack, active: g.tensionAttackVisible },
      { h: 'TensionDecay', pt: g.tensionDecay, active: g.tensionDecayVisible },
      { h: 'TensionRelease', pt: g.tensionRelease, active: g.tensionReleaseVisible },
      { h: 'Attack', pt: g.attack, active: true },
      { h: 'Hold', pt: g.hold, active: true },
      { h: 'Decay', pt: g.decay, active: true },
      { h: 'Sustain', pt: g.sustain, active: true },
      { h: 'Release', pt: g.release, active: true },
    ];

    let bestDist = maxRadius;
    let best: DragHandle = 'None';
    for (const c of candidates) {
      if (!c.active) continue;
      const d = Math.hypot(pos.x - c.pt.x, pos.y - c.pt.y);
      if (d < bestDist) {
        bestDist = d;
        best = c.h;
      }
    }

    if (best === 'None') {
      const startVisible = g.trimX0 >= g.plot.x - 10 && g.trimX0 <= g.plot.x + g.plot.w + 10;
      const endVisible = g.trimX1 >= g.plot.x - 10 && g.trimX1 <= g.plot.x + g.plot.w + 10;
      const distStart = startVisible ? Math.abs(pos.x - g.trimX0) : 1e6;
      const distEnd = endVisible ? Math.abs(pos.x - g.trimX1) : 1e6;
      if (distStart <= 12 && distStart <= distEnd) best = 'TrimStart';
      else if (distEnd <= 12) best = 'TrimEnd';
    }

    return best;
  };

  const applyDrag = (pos: Point, w: number, h: number) => {
    const st = dragStateRef.current;
    const g = getGeometry(w, h);

    if (st.handle === 'OverviewPan' && g.overviewRect && g.overviewRect.w > 1) {
      const norm = clamp((pos.x - g.overviewRect.x) / g.overviewRect.w, 0, 1);
      const span = 1.0 / Math.max(1.0, zoomFactor);
      clampViewport(norm - span * 0.5, zoomFactor);
      return;
    }

    if (st.handle === 'Pan') {
      const span = 1.0 / Math.max(1.0, zoomFactor);
      const dx = pos.x - st.startX;
      clampViewport(st.startView - (dx / Math.max(1.0, g.plot.w)) * span, zoomFactor);
      return;
    }

    if (
      st.handle === 'TensionAttack' ||
      st.handle === 'TensionDecay' ||
      st.handle === 'TensionRelease'
    ) {
      const delta = (st.startY - pos.y) / 85.0;
      const nextTen = clamp(st.startTension + delta, -1.0, 1.0);
      if (st.handle === 'TensionAttack') onChangeParams({ attackTension: nextTen });
      else if (st.handle === 'TensionDecay') onChangeParams({ decayTension: nextTen });
      else onChangeParams({ releaseTension: nextTen });
      return;
    }

    if (st.handle === 'TrimStart') {
      const rawNorm = plotXToNorm(pos.x, g.plot);
      onChangeParams({ trimStart: clamp(rawNorm, 0, Math.max(0, params.trimEnd - 0.005)) });
      return;
    }

    if (st.handle === 'TrimEnd') {
      const rawNorm = plotXToNorm(pos.x, g.plot);
      onChangeParams({ trimEnd: clamp(rawNorm, Math.min(1, params.trimStart + 0.005), 1.0) });
      return;
    }

    const duration = Math.max(1.0, g.trimmedDurationMs);
    const xNorm = clamp((pos.x - g.trimX0) / Math.max(1.0, g.trimW), 0, 1);
    const timeMs = xNorm * duration;

    switch (st.handle) {
      case 'Attack':
        onChangeParams({ attackMs: clamp(timeMs, 0.1, 2000.0) });
        break;
      case 'Hold':
        onChangeParams({ holdMs: clamp(timeMs - params.attackMs, 0.0, 2000.0) });
        break;
      case 'Decay':
        onChangeParams({
          decayMs: clamp(timeMs - params.attackMs - params.holdMs, 0.1, 5000.0),
        });
        break;
      case 'Sustain': {
        const envTop = g.plot.y + 7.0;
        const envBot = g.plot.y + g.plot.h - 15.0;
        const envH = Math.max(12.0, envBot - envTop);
        onChangeParams({ sustain: clamp((envBot - pos.y) / envH, 0.0, 1.0) });
        break;
      }
      case 'Release':
        onChangeParams({ releaseMs: clamp(duration - timeMs, 0.1, 10000.0) });
        break;
      default:
        break;
    }
  };

  const getLocalPos = (clientX: number, clientY: number) => {
    const rect = waveWrapRef.current?.getBoundingClientRect();
    if (!rect) return { pos: { x: 0, y: 0 }, w: 1, h: 1 };
    return {
      pos: { x: clientX - rect.left, y: clientY - rect.top },
      w: rect.width,
      h: rect.height,
    };
  };

  const handlePointerDown = (e: React.PointerEvent<HTMLCanvasElement>) => {
    setContextMenu(null);
    const { pos, w, h } = getLocalPos(e.clientX, e.clientY);
    const g = getGeometry(w, h);

    if (e.button === 2) return;

    if (
      g.overviewRect &&
      pos.x >= g.overviewRect.x &&
      pos.x <= g.overviewRect.x + g.overviewRect.w &&
      pos.y >= g.overviewRect.y &&
      pos.y <= g.overviewRect.y + g.overviewRect.h
    ) {
      (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
      dragStateRef.current = {
        handle: 'OverviewPan',
        startX: pos.x,
        startY: pos.y,
        startTension: 0,
        startView: viewStartNorm,
      };
      applyDrag(pos, w, h);
      return;
    }

    if ((e.button === 1 || e.altKey) && zoomFactor > 1.01) {
      (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
      dragStateRef.current = {
        handle: 'Pan',
        startX: pos.x,
        startY: pos.y,
        startTension: 0,
        startView: viewStartNorm,
      };
      return;
    }

    const best = hitTestHandle(pos, g);
    if (best !== 'None') {
      (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
      let startTension = 0;
      if (best === 'TensionAttack') startTension = params.attackTension;
      else if (best === 'TensionDecay') startTension = params.decayTension;
      else if (best === 'TensionRelease') startTension = params.releaseTension;

      dragStateRef.current = {
        handle: best,
        startX: pos.x,
        startY: pos.y,
        startTension,
        startView: viewStartNorm,
      };
    }
  };

  const handlePointerMove = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (dragStateRef.current.handle === 'None') return;
    const { pos, w, h } = getLocalPos(e.clientX, e.clientY);
    applyDrag(pos, w, h);
  };

  const handlePointerUp = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (dragStateRef.current.handle === 'None') return;
    dragStateRef.current.handle = 'None';
    try {
      (e.currentTarget as HTMLElement).releasePointerCapture(e.pointerId);
    } catch {
      // ignore
    }
  };

  const handleDoubleClick = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const { pos, w, h } = getLocalPos(e.clientX, e.clientY);
    const g = getGeometry(w, h);
    const hit = hitTestHandle(pos, g);
    if (hit === 'TensionAttack') onChangeParams({ attackTension: 0 });
    else if (hit === 'TensionDecay') onChangeParams({ decayTension: 0 });
    else if (hit === 'TensionRelease') onChangeParams({ releaseTension: 0 });
  };

  const handleContextMenu = (e: React.MouseEvent<HTMLCanvasElement>) => {
    e.preventDefault();
    const { pos, w, h } = getLocalPos(e.clientX, e.clientY);
    const g = getGeometry(w, h);
    if (pos.y < g.plot.y || pos.y > g.plot.y + g.plot.h) return;

    const hit = hitTestHandle(pos, g, 18);
    let segment: Segment = 'Decay';
    let title = 'Decay Curve';
    if (hit === 'Attack' || hit === 'TensionAttack') {
      segment = 'Attack';
      title = hit === 'Attack' ? 'Point A (Attack)' : 'Attack Tension';
    } else if (hit === 'Hold' || hit === 'Decay' || hit === 'Sustain' || hit === 'TensionDecay') {
      segment = 'Decay';
      title = 'Decay Curve';
    } else if (hit === 'Release' || hit === 'TensionRelease') {
      segment = 'Release';
      title = hit === 'Release' ? 'Point R (Release)' : 'Release Tension';
    } else if (pos.x <= g.attackEndX + 8) {
      segment = 'Attack';
      title = 'Attack Curve';
    } else if (pos.x >= g.releaseStartX - 8) {
      segment = 'Release';
      title = 'Release Curve';
    }

    setContextMenu({ x: pos.x, y: pos.y, segment, title });
  };

  const handleWheel = (e: React.WheelEvent<HTMLDivElement>) => {
    const { pos, w, h } = getLocalPos(e.clientX, e.clientY);
    const g = getGeometry(w, h);
    if (pos.y < g.plot.y || pos.y > g.plot.y + g.plot.h) return;

    const span = 1.0 / Math.max(1.0, zoomFactor);
    if (e.shiftKey || Math.abs(e.deltaX) > Math.abs(e.deltaY)) {
      const d = Math.abs(e.deltaX) > Math.abs(e.deltaY) ? e.deltaX : e.deltaY;
      clampViewport(viewStartNorm + Math.sign(d) * span * 0.15, zoomFactor);
      return;
    }

    if (Math.abs(e.deltaY) > 0.01) {
      const anchorNorm = plotXToNorm(pos.x, g.plot);
      const factor = e.deltaY < 0 ? 1.35 : 1.0 / 1.35;
      zoomAroundNormalized(anchorNorm, factor);
    }
  };

  const zoomAnchor = () => {
    const span = 1.0 / Math.max(1.0, zoomFactor);
    const v0 = viewStartNorm;
    const v1 = v0 + span;
    const startIn = params.trimStart >= v0 - 0.001 && params.trimStart <= v1 + 0.001;
    const endIn = params.trimEnd >= v0 - 0.001 && params.trimEnd <= v1 + 0.001;
    if (startIn && !endIn) return params.trimStart;
    if (endIn && !startIn) return params.trimEnd;
    return v0 + span * 0.5;
  };

  const zoomText =
    zoomFactor <= 1.01
      ? '1.0x'
      : zoomFactor >= 10
        ? `${Math.round(zoomFactor)}x`
        : `${zoomFactor.toFixed(1)}x`;

  const fadeStr =
    params.fadeInMs > 0.5 || params.fadeOutMs > 0.5
      ? `  |  FI ${formatEnvelopeTime(params.fadeInMs)} FO ${formatEnvelopeTime(params.fadeOutMs)}`
      : '';

  const readoutText =
    `A ${formatEnvelopeTime(params.attackMs)}  H ${formatEnvelopeTime(params.holdMs)}  ` +
    `D ${formatEnvelopeTime(params.decayMs)}  S ${Math.round(params.sustain * 100)}%  ` +
    `R ${formatEnvelopeTime(params.releaseMs)}   |   Trim ${formatTrimPercent(params.trimStart)}-${formatTrimPercent(params.trimEnd)}` +
    fadeStr +
    (params.reverse ? '  |  REV' : '') +
    (!params.envEnabled ? '  |  ENV OFF' : '');

  const formatHz = (v: number) => (v >= 1000 ? `${(v / 1000).toFixed(1)}k` : `${Math.round(v)}`);

  const currentCtxCurve =
    contextMenu?.segment === 'Attack'
      ? params.attackCurve
      : contextMenu?.segment === 'Decay'
        ? params.decayCurve
        : params.releaseCurve;

  return (
    <div
      className={`flex flex-col w-full h-full gap-1 select-none ${isDragOver ? 'ring-1 ring-[#a78bfa]' : ''}`}
      onDragOver={(e) => {
        e.preventDefault();
        setIsDragOver(true);
      }}
      onDragLeave={() => setIsDragOver(false)}
      onDrop={(e) => {
        e.preventDefault();
        setIsDragOver(false);
        const file = e.dataTransfer.files?.[0];
        if (file) onDropAudioFile(file);
      }}
    >
      {/* Waveform + Envelope Plot Area */}
      <div
        ref={waveWrapRef}
        className="relative flex-1 min-h-0 rounded-[3px] border border-[#2b2b2b] overflow-hidden"
        onWheel={handleWheel}
      >
        <canvas
          ref={canvasRef}
          className="block w-full h-full cursor-crosshair"
          onPointerDown={handlePointerDown}
          onPointerMove={handlePointerMove}
          onPointerUp={handlePointerUp}
          onDoubleClick={handleDoubleClick}
          onContextMenu={handleContextMenu}
        />

        {/* Top Readout + Zoom Header Bar */}
        <div
          className="absolute top-0 left-0 right-0 flex items-center justify-between px-2 bg-[#0c1016]/75"
          style={{ height: HEADER_H }}
        >
          <span className="text-[10px] text-[#e7edf5] truncate pr-2 tabular-nums">
            {readoutText}
          </span>
          <div className="flex items-center gap-[2px] shrink-0">
            <button
              type="button"
              title="Center zoom on Start (Trim F)"
              onClick={() => focusViewportAt(params.trimStart, 4.0)}
              className="h-[16px] px-1.5 text-[10px] font-semibold bg-[#242033] text-[#c0a6ff] rounded-sm hover:bg-[#342e4a] cursor-pointer"
            >
              |&lt;
            </button>
            <button
              type="button"
              title="Center zoom on End (Trim B)"
              onClick={() => focusViewportAt(params.trimEnd, 4.0)}
              className="h-[16px] px-1.5 text-[10px] font-semibold bg-[#242033] text-[#c0a6ff] rounded-sm hover:bg-[#342e4a] cursor-pointer"
            >
              &gt;|
            </button>
            <button
              type="button"
              title="Zoom to fit Start-End trim range"
              onClick={zoomToFitTrim}
              className="h-[16px] px-1.5 ml-[2px] text-[10px] font-semibold bg-[#242033] text-[#d2dce8] rounded-sm hover:bg-[#342e4a] cursor-pointer"
            >
              Fit
            </button>
            <button
              type="button"
              title="Zoom out waveform"
              disabled={zoomFactor <= 1.01}
              onClick={() => zoomAroundNormalized(zoomAnchor(), 1 / 1.6)}
              className="h-[16px] w-[18px] ml-[2px] text-[10px] font-bold bg-[#242033] text-[#e7edf5] rounded-sm hover:bg-[#342e4a] disabled:opacity-40 cursor-pointer"
            >
              -
            </button>
            <button
              type="button"
              title="Reset waveform zoom to 1.0x"
              onClick={() => clampViewport(0, 1)}
              className="h-[16px] min-w-[34px] px-1 text-[10px] font-semibold bg-[#242033] rounded-sm hover:bg-[#342e4a] cursor-pointer tabular-nums"
              style={{ color: zoomFactor > 1.01 ? '#c0a6ff' : '#aebacc' }}
            >
              {zoomText}
            </button>
            <button
              type="button"
              title="Zoom in waveform"
              disabled={zoomFactor >= MAX_ZOOM - 0.01}
              onClick={() => zoomAroundNormalized(zoomAnchor(), 1.6)}
              className="h-[16px] w-[18px] text-[10px] font-bold bg-[#242033] text-[#e7edf5] rounded-sm hover:bg-[#342e4a] disabled:opacity-40 cursor-pointer"
            >
              +
            </button>
          </div>
        </div>

        {/* Right-click FL Curve Context Menu */}
        {contextMenu && (
          <div
            className="absolute z-30 bg-[#1f1e2c] border border-[#51456f] rounded shadow-xl py-1 min-w-[148px] text-[11px]"
            style={{
              left: Math.min(contextMenu.x, (waveWrapRef.current?.clientWidth ?? 300) - 160),
              top: Math.min(contextMenu.y, (waveWrapRef.current?.clientHeight ?? 200) - 210),
            }}
            onMouseLeave={() => setContextMenu(null)}
          >
            <div className="px-2.5 py-1 text-[10px] font-bold text-[#9fa4c4] border-b border-[#2d2b3c]">
              {contextMenu.title}
            </div>
            {FL_CURVE_NAMES.map((name, idx) => (
              <button
                key={name}
                type="button"
                onClick={() => {
                  if (contextMenu.segment === 'Attack') onChangeParams({ attackCurve: idx });
                  else if (contextMenu.segment === 'Decay') onChangeParams({ decayCurve: idx });
                  else onChangeParams({ releaseCurve: idx });
                  setContextMenu(null);
                }}
                className="w-full text-left px-2.5 py-0.5 hover:bg-[#312f48] flex items-center justify-between text-[#e6e8f2] cursor-pointer"
              >
                <span>{name}</span>
                {currentCtxCurve === idx && <span className="text-[#a78bfa]">✓</span>}
              </button>
            ))}
            <div className="my-0.5 border-t border-[#2d2b3c]" />
            <button
              type="button"
              onClick={() => {
                if (contextMenu.segment === 'Attack') onChangeParams({ attackTension: 0 });
                else if (contextMenu.segment === 'Decay') onChangeParams({ decayTension: 0 });
                else onChangeParams({ releaseTension: 0 });
                setContextMenu(null);
              }}
              className="w-full text-left px-2.5 py-0.5 hover:bg-[#312f48] text-[#e6e8f2] cursor-pointer"
            >
              Reset Tension (0%)
            </button>
            <button
              type="button"
              onClick={() => {
                onChangeParams({ envEnabled: !params.envEnabled });
                setContextMenu(null);
              }}
              className="w-full text-left px-2.5 py-0.5 hover:bg-[#312f48] text-[#e6e8f2] cursor-pointer"
            >
              {params.envEnabled ? 'Disable Envelope' : 'Enable Envelope'}
            </button>
          </div>
        )}
      </div>

      {/* FL Studio Envelope Toolbar Strip */}
      <div className="h-[28px] shrink-0 bg-[#211d31] border border-[#43385f] rounded-[3px] px-1.5 flex items-center gap-1.5">
        <button
          type="button"
          title="Enable or bypass FL envelope"
          onClick={() => onChangeParams({ envEnabled: !params.envEnabled })}
          style={{
            backgroundColor: params.envEnabled ? flCurveColour : '#252235',
            color: params.envEnabled ? '#14101c' : '#c2bad8',
          }}
          className="h-[20px] px-2 text-[10px] font-bold rounded-sm border border-[#51456f] cursor-pointer shrink-0"
        >
          ENV
        </button>

        {(
          [
            {
              lbl: 'ATT',
              curve: params.attackCurve,
              tension: params.attackTension,
              onCurve: (v: number) => onChangeParams({ attackCurve: v }),
            },
            {
              lbl: 'DEC',
              curve: params.decayCurve,
              tension: params.decayTension,
              onCurve: (v: number) => onChangeParams({ decayCurve: v }),
            },
            {
              lbl: 'REL',
              curve: params.releaseCurve,
              tension: params.releaseTension,
              onCurve: (v: number) => onChangeParams({ releaseCurve: v }),
            },
          ] as const
        ).map((seg) => {
          const pct = Math.round(seg.tension * 100);
          return (
            <div key={seg.lbl} className="flex items-center gap-1 flex-1 min-w-0">
              <span className="text-[9.5px] font-bold text-[#95a4b8] shrink-0">
                {seg.lbl}
              </span>
              <select
                value={seg.curve}
                onChange={(e) => seg.onCurve(Number(e.target.value))}
                className="flex-1 min-w-0 h-[20px] px-1 text-[10px] bg-[#211d31] text-[#e5def4] border border-[#51456f] rounded-sm outline-none cursor-pointer"
              >
                {FL_CURVE_NAMES.map((name, idx) => (
                  <option key={name} value={idx}>
                    {name}
                  </option>
                ))}
              </select>
              <span
                className="text-[9.5px] font-bold w-[31px] text-left tabular-nums shrink-0"
                style={{ color: Math.abs(pct) > 0 ? flCurveColour : '#8a96a6' }}
              >
                {pct > 0 ? `+${pct}%` : `${pct}%`}
              </span>
            </div>
          );
        })}
      </div>

      {/* Sample-Control Strip (2 Rows of Knobs) */}
      <div className="h-[118px] shrink-0 bg-[#171522] border border-[#39324f] rounded-[3px] px-2 py-1 flex flex-col justify-between">
        {/* Row 1: REV + Gain, Pitch, Pogo, Trim F, Trim B, Fade In, Fade Out */}
        <div className="flex items-center justify-between gap-1">
          <button
            type="button"
            title="Reverse sample playback"
            onClick={() => onChangeParams({ reverse: !params.reverse })}
            style={{
              backgroundColor: params.reverse ? flCurveColour : '#252235',
              color: params.reverse ? '#14101c' : '#e7e2f5',
            }}
            className="w-[40px] h-[24px] shrink-0 text-[10px] font-bold rounded-sm border border-[#51456f] cursor-pointer"
          >
            REV
          </button>

          <RotaryKnob
            label="Gain"
            value={params.gain}
            min={0}
            max={2}
            defaultValue={1}
            step={0.01}
            accentColor={traceColor}
            formatValue={(v) => `${Math.round(v * 100)}%`}
            onChange={(v) => onChangeParams({ gain: v })}
            title="Layer gain (0% to 200%)"
          />
          <RotaryKnob
            label="Pitch"
            value={params.pitchSemitones}
            min={-24}
            max={24}
            defaultValue={0}
            step={0.1}
            accentColor={traceColor}
            formatValue={(v) => `${v > 0 ? '+' : ''}${v.toFixed(1)}st`}
            onChange={(v) => onChangeParams({ pitchSemitones: v })}
            title="Layer pitch (-24 to +24 semitones)"
          />
          <RotaryKnob
            label="Pogo"
            value={params.pogo}
            min={-1}
            max={1}
            defaultValue={0}
            step={0.01}
            accentColor={secondaryAccent}
            formatValue={(v) => `${v > 0 ? '+' : ''}${Math.round(v * 100)}%`}
            onChange={(v) => onChangeParams({ pogo: v })}
            title="FL Sampler style pitch-bend envelope (-100% to +100%)"
          />
          <RotaryKnob
            label="Trim F"
            value={params.trimStart}
            min={0}
            max={0.98}
            defaultValue={0}
            step={0.005}
            accentColor={secondaryAccent}
            formatValue={(v) => `${Math.round(v * 100)}%`}
            onChange={(v) =>
              onChangeParams({ trimStart: Math.min(v, Math.max(0, params.trimEnd - 0.02)) })
            }
            title="Sample front trim (0% to 98%)"
          />
          <RotaryKnob
            label="Trim B"
            value={params.trimEnd}
            min={0.02}
            max={1}
            defaultValue={1}
            step={0.005}
            accentColor={secondaryAccent}
            formatValue={(v) => `${Math.round(v * 100)}%`}
            onChange={(v) =>
              onChangeParams({ trimEnd: Math.max(v, Math.min(1, params.trimStart + 0.02)) })
            }
            title="Sample back trim (2% to 100%)"
          />
          <RotaryKnob
            label="Fade In"
            value={params.fadeInMs}
            min={0}
            max={2000}
            defaultValue={0}
            step={1}
            skew={0.4}
            accentColor={traceColor}
            formatValue={(v) => formatEnvelopeTime(v)}
            onChange={(v) => onChangeParams({ fadeInMs: v })}
            title="Sample fade-in duration (0 ms to 2000 ms)"
          />
          <RotaryKnob
            label="Fade Out"
            value={params.fadeOutMs}
            min={0}
            max={5000}
            defaultValue={0}
            step={1}
            skew={0.4}
            accentColor={traceColor}
            formatValue={(v) => formatEnvelopeTime(v)}
            onChange={(v) => onChangeParams({ fadeOutMs: v })}
            title="Sample fade-out duration (0 ms to 5000 ms)"
          />
        </div>

        {/* Row 2: Spacer + Timing, Pan, HPF, LPF aligned under Row 1 */}
        <div className="flex items-center justify-between gap-1">
          <div className="w-[40px] shrink-0" />
          <RotaryKnob
            label="Timing"
            value={params.delayShiftMs}
            min={-100}
            max={100}
            defaultValue={0}
            step={0.5}
            accentColor={traceColor}
            formatValue={(v) => `${v > 0 ? '+' : ''}${Math.round(v)}ms`}
            onChange={(v) => onChangeParams({ delayShiftMs: v })}
            title="Timing shift (-100 ms to +100 ms)"
          />
          <RotaryKnob
            label="Pan"
            value={params.pan}
            min={-1}
            max={1}
            defaultValue={0}
            step={0.01}
            accentColor={traceColor}
            formatValue={(v) =>
              Math.abs(v) < 0.005 ? 'C' : `${Math.round(Math.abs(v) * 100)}${v < 0 ? 'L' : 'R'}`
            }
            onChange={(v) => onChangeParams({ pan: v })}
            title="Layer pan: left / centre / right"
          />
          <RotaryKnob
            label="HPF"
            value={params.highPassHz}
            min={20}
            max={1000}
            defaultValue={20}
            step={1}
            skew={0.35}
            accentColor={traceColor}
            formatValue={formatHz}
            onChange={(v) => onChangeParams({ highPassHz: v })}
            title="Layer high-pass filter: 20 Hz to 1 kHz"
          />
          <RotaryKnob
            label="LPF"
            value={params.lowPassHz}
            min={1000}
            max={20000}
            defaultValue={20000}
            step={10}
            skew={0.35}
            accentColor={traceColor}
            formatValue={formatHz}
            onChange={(v) => onChangeParams({ lowPassHz: v })}
            title="Layer low-pass filter: 1 kHz to 20 kHz"
          />
          <div className="w-[52px] shrink-0" />
          <div className="w-[52px] shrink-0" />
          <div className="w-[52px] shrink-0" />
        </div>
      </div>
    </div>
  );
};
