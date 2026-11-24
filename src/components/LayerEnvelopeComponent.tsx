import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  clamp,
  flSegmentLevel,
  formatEnvelopeTime,
  formatHzShort,
  formatPan,
  formatPitch,
  skewedToValue,
  valueToSkewed,
} from '../audio/curves';
import { computeEnvelopeSchedule } from '../audio/dspMath';
import {
  CURVE_NAMES,
  LayerParameters,
  PREVIEW_POINTS,
  SIGNAL_COLOURS,
  WaveformPreview,
} from '../audio/types';
import {
  clampViewport,
  focusViewportAt,
  formatZoomFactor,
  getViewportSpan,
  MAX_WAVEFORM_ZOOM,
  normToPlotX,
  panViewport,
  plotXToNorm,
  WaveformViewport,
  zoomAroundNormalized,
  zoomToFitTrim,
} from '../audio/waveformZoom';
import { RotaryKnob } from './RotaryKnob';

interface LayerEnvelopeComponentProps {
  layer: number;
  deckId: 'snapback' | 'transient';
  preview: WaveformPreview;
  params: LayerParameters;
  onChangeParams: (next: Partial<LayerParameters>) => void;
}

type HandleType =
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
  | 'Pan';

type EnvelopeSegment = 'attack' | 'decay' | 'release';

interface ContextMenuState {
  x: number;
  y: number;
  caretX: number;
  flipUp: boolean;
  segment: EnvelopeSegment;
  handle: HandleType;
  label: string;
}

interface Geometry {
  plotX: number;
  plotY: number;
  plotW: number;
  plotH: number;
  envTop: number;
  envBot: number;
  envH: number;
  trimX0: number;
  trimX1: number;
  trimW: number;
  trimmedDurationMs: number;
  attackEndMs: number;
  holdEndMs: number;
  decayEndMs: number;
  releaseStartMs: number;
  releaseMs: number;
  attackEndX: number;
  holdEndX: number;
  decayEndX: number;
  releaseStartX: number;
  origin: { x: number; y: number };
  attack: { x: number; y: number };
  hold: { x: number; y: number; visible: boolean };
  decay: { x: number; y: number; visible: boolean };
  sustain: { x: number; y: number };
  release: { x: number; y: number; visible: boolean };
  endPt: { x: number; y: number };
  tensionAttack: { x: number; y: number; visible: boolean };
  tensionDecay: { x: number; y: number; visible: boolean };
  tensionRelease: { x: number; y: number; visible: boolean };
}

const TENSION_RADIUS = 4.6;
const CURVE_STEPS = 96;
const HEADER_H = 22;
const RULER_H = 16;
const OVERVIEW_H = 14;

export const LayerEnvelopeComponent: React.FC<LayerEnvelopeComponentProps> = ({
  layer,
  deckId,
  preview,
  params,
  onChangeParams,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const plotWrapRef = useRef<HTMLDivElement | null>(null);
  const overviewRef = useRef<HTMLDivElement | null>(null);
  const activeHandleRef = useRef<HandleType>('None');
  const dragOriginRef = useRef<{
    startX: number;
    startY: number;
    startTension: number;
    startViewStart: number;
  }>({
    startX: 0,
    startY: 0,
    startTension: 0,
    startViewStart: 0,
  });
  const paramsRef = useRef(params);
  paramsRef.current = params;

  const [viewport, setViewport] = useState<WaveformViewport>({
    viewStart: 0,
    zoom: 1,
  });
  const viewportRef = useRef(viewport);
  viewportRef.current = viewport;

  const [hoveredHandle, setHoveredHandle] = useState<HandleType>('None');
  const [draggingHandle, setDraggingHandle] = useState<HandleType>('None');
  const [contextMenu, setContextMenu] = useState<ContextMenuState | null>(null);
  const [panDragging, setPanDragging] = useState(false);

  const isPre = layer < 3;
  const colour = SIGNAL_COLOURS[layer] ?? '#7d92ff';
  const simWaveColour = colour;
  const flCurveColour = '#57d6ff';
  const flControlDot = '#dcf6ff';

  const getGeometry = useCallback(
    (
      w: number,
      h: number,
      p: LayerParameters,
      vp: WaveformViewport = viewportRef.current
    ): Geometry => {
      const isZoomed = vp.zoom > 1.01;
      const plotX = 0;
      const plotY = HEADER_H;
      const plotW = Math.max(24, w);
      const plotH = Math.max(24, h - HEADER_H - RULER_H - (isZoomed ? OVERVIEW_H : 0));

      const trimStart = clamp(p.trimStart, 0, 0.99);
      const trimEnd = clamp(p.trimEnd, trimStart + 0.005, 1);
      const trimX0 = normToPlotX(trimStart, plotX, plotW, vp);
      const trimX1 = normToPlotX(trimEnd, plotX, plotW, vp);
      const trimW = Math.max(4, trimX1 - trimX0);

      const totalMs = Math.max(1, preview.loaded ? preview.durationMs : 1000);
      const trimmedDurationMs = Math.max(1, totalMs * (trimEnd - trimStart));

      const sched = computeEnvelopeSchedule(
        p.attackMs,
        p.holdMs,
        p.decayMs,
        p.releaseMs,
        trimmedDurationMs
      );
      const sustain = clamp(p.sustain, 0, 1);

      const xAt = (ms: number) =>
        trimX0 + (trimW * clamp(ms, 0, trimmedDurationMs)) / trimmedDurationMs;
      const envTop = plotY + 8;
      const envBot = plotY + plotH - 8;
      const envH = Math.max(12, envBot - envTop);
      const yAt = (lvl: number) => envBot - envH * clamp(lvl, 0, 1);

      const attSpan = Math.max(0.05, sched.attackEndMs);
      const decSpan = Math.max(0.05, sched.decayEndMs - sched.holdEndMs);
      const levelAt = (ms: number) => {
        if (ms < sched.attackEndMs) {
          return flSegmentLevel(
            ms / attSpan,
            0,
            1,
            p.attackCurve,
            p.attackTension
          );
        }
        if (ms < sched.holdEndMs) {
          return 1.0;
        }
        if (ms < sched.decayEndMs) {
          return flSegmentLevel(
            (ms - sched.holdEndMs) / decSpan,
            1.0,
            sustain,
            p.decayCurve,
            p.decayTension
          );
        }
        return sustain;
      };

      const origin = { x: xAt(0), y: yAt(0) };
      const attackPt = { x: xAt(sched.attackEndMs), y: yAt(1.0) };
      const holdPt = {
        x: xAt(sched.holdEndMs),
        y: yAt(1.0),
        visible: sched.holdEndMs - sched.attackEndMs > 1.5,
      };
      const sustainPt = { x: xAt(sched.decayEndMs), y: yAt(sustain) };
      const hasSustainShelf = sched.releaseStartMs - sched.decayEndMs > 4.0;
      const releasePt = {
        x: xAt(sched.releaseStartMs),
        y: yAt(levelAt(sched.releaseStartMs)),
        visible: hasSustainShelf,
      };
      const decayPt = {
        x: sustainPt.x,
        y: sustainPt.y,
        visible: false,
      };
      const endPt = { x: xAt(trimmedDurationMs), y: yAt(0) };

      // Midpoint tension handles at t = 0.5 on Attack, Decay, and Release segments
      const attMidMs = sched.attackEndMs * 0.5;
      const attMidLevel = flSegmentLevel(
        0.5,
        0,
        1,
        p.attackCurve,
        p.attackTension
      );
      const tensionAttack = {
        x: xAt(attMidMs),
        y: yAt(attMidLevel),
        visible: attackPt.x - trimX0 >= 12,
      };

      const decMidMs = sched.holdEndMs + (sched.decayEndMs - sched.holdEndMs) * 0.5;
      const decMidLevel = flSegmentLevel(
        0.5,
        1.0,
        sustain,
        p.decayCurve,
        p.decayTension
      );
      const tensionDecay = {
        x: xAt(decMidMs),
        y: yAt(decMidLevel),
        visible: sustainPt.x - holdPt.x >= 12,
      };

      const relMidMs = sched.releaseStartMs + sched.releaseMs * 0.5;
      const relMidLevel = flSegmentLevel(
        0.5,
        levelAt(sched.releaseStartMs),
        0,
        p.releaseCurve,
        p.releaseTension
      );
      const tensionRelease = {
        x: xAt(relMidMs),
        y: yAt(relMidLevel),
        visible: trimX1 - releasePt.x >= 12,
      };

      return {
        plotX,
        plotY,
        plotW,
        plotH,
        envTop,
        envBot,
        envH,
        trimX0,
        trimX1,
        trimW,
        trimmedDurationMs,
        attackEndMs: sched.attackEndMs,
        holdEndMs: sched.holdEndMs,
        decayEndMs: sched.decayEndMs,
        releaseStartMs: sched.releaseStartMs,
        releaseMs: sched.releaseMs,
        attackEndX: attackPt.x,
        holdEndX: holdPt.x,
        decayEndX: sustainPt.x,
        releaseStartX: releasePt.x,
        origin,
        attack: attackPt,
        hold: holdPt,
        decay: decayPt,
        sustain: sustainPt,
        release: releasePt,
        endPt,
        tensionAttack,
        tensionDecay,
        tensionRelease,
      };
    },
    [preview.loaded, preview.durationMs]
  );

  const renderCanvas = useCallback(() => {
    const canvas = canvasRef.current;
    const wrap = plotWrapRef.current;
    if (!canvas || !wrap) return;

    const rect = wrap.getBoundingClientRect();
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

    ctx.save();
    ctx.scale(dpr, dpr);

    // Image-Line dark slate-blue container backdrop
    const grad = ctx.createLinearGradient(0, 0, 0, h);
    grad.addColorStop(0, '#36404d');
    grad.addColorStop(1, '#28313c');
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, w, h);

    // Top header backdrop (actual text + zoom controls live cleanly in HTML header)
    ctx.fillStyle = 'rgba(14, 19, 25, 0.86)';
    ctx.fillRect(0, 0, w, HEADER_H);

    const vp = viewport;
    const vpSpan = getViewportSpan(vp.zoom);
    const vpEnd = Math.min(1, vp.viewStart + vpSpan);
    const geom = getGeometry(w, h, params, vp);
    const {
      plotX,
      plotY,
      plotW,
      plotH,
      envBot,
      trimX0,
      trimX1,
      trimW,
      trimmedDurationMs,
    } = geom;

    if (plotW >= 24 && plotH >= 24) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(plotX, plotY, plotW, plotH);
      ctx.clip();

      // Subtle tint on dark Image-Line slate-blue grid when sample layer is loaded
      if (preview.loaded) {
        ctx.fillStyle = isPre
          ? 'rgba(32, 42, 74, 0.28)'
          : 'rgba(54, 28, 72, 0.26)';
        ctx.fillRect(plotX, plotY, plotW, plotH);
      }

      // Crisp Image-Line fine 16x8 grid (scales cleanly as you zoom)
      ctx.lineWidth = 1;
      const gridCols = Math.max(
        16,
        Math.min(512, 16 * Math.pow(2, Math.floor(Math.log2(vp.zoom))))
      );
      const firstGrid = Math.max(1, Math.ceil(vp.viewStart * gridCols));
      const lastGrid = Math.min(gridCols - 1, Math.floor(vpEnd * gridCols));
      for (let gIdx = firstGrid; gIdx <= lastGrid; gIdx++) {
        const norm = gIdx / gridCols;
        const x = Math.round(normToPlotX(norm, plotX, plotW, vp)) + 0.5;
        ctx.strokeStyle =
          gIdx % 4 === 0
            ? 'rgba(220, 234, 250, 0.14)'
            : 'rgba(205, 222, 240, 0.06)';
        ctx.beginPath();
        ctx.moveTo(x, plotY);
        ctx.lineTo(x, plotY + plotH);
        ctx.stroke();
      }
      for (let r = 1; r < 8; r++) {
        const y = Math.round(plotY + (plotH * r) / 8) + 0.5;
        ctx.strokeStyle =
          r === 4
            ? 'rgba(220, 234, 250, 0.17)'
            : r % 2 === 0
              ? 'rgba(205, 222, 240, 0.10)'
              : 'rgba(205, 222, 240, 0.05)';
        ctx.beginPath();
        ctx.moveTo(plotX, y);
        ctx.lineTo(plotX + plotW, y);
        ctx.stroke();
      }

      // Sample waveform rendered column-by-column across the visible [viewStart, vpEnd] window
      if (preview.loaded) {
        const centre = plotY + plotH * 0.5;
        const amplitude = plotH * 0.44;
        ctx.strokeStyle = simWaveColour;
        ctx.globalAlpha = 0.85;
        ctx.lineWidth = 1.25;
        const cols = Math.max(1, Math.floor(plotW));
        for (let col = 0; col < cols; col++) {
          const n0 = vp.viewStart + (col / cols) * vpSpan;
          const n1 = vp.viewStart + ((col + 1) / cols) * vpSpan;
          let minVal = 1.0;
          let maxVal = -1.0;
          const b0 = clamp(Math.floor(n0 * PREVIEW_POINTS), 0, PREVIEW_POINTS - 1);
          const b1 = clamp(Math.floor(n1 * PREVIEW_POINTS), b0, PREVIEW_POINTS - 1);
          for (let b = b0; b <= b1; b++) {
            const srcIdx = params.reverse ? PREVIEW_POINTS - 1 - b : b;
            if (preview.minimum[srcIdx] < minVal) minVal = preview.minimum[srcIdx];
            if (preview.maximum[srcIdx] > maxVal) maxVal = preview.maximum[srcIdx];
          }
          const x = plotX + col + 0.5;
          const top = centre - maxVal * amplitude;
          const bottom = centre - minVal * amplitude;
          ctx.beginPath();
          ctx.moveTo(x, top);
          ctx.lineTo(x, Math.max(top + 1, bottom));
          ctx.stroke();
        }
        ctx.globalAlpha = 1;
      }

      // Dim trimmed-out front and back regions cleanly inside visible plot bounds
      ctx.fillStyle = 'rgba(8, 10, 14, 0.62)';
      const leftDimEnd = Math.min(plotX + plotW, trimX0);
      if (leftDimEnd > plotX) {
        ctx.fillRect(plotX, plotY, leftDimEnd - plotX, plotH);
      }
      const rightDimStart = Math.max(plotX, trimX1);
      if (rightDimStart < plotX + plotW) {
        ctx.fillRect(rightDimStart, plotY, plotX + plotW - rightDimStart, plotH);
      }

      // Fade-in & Fade-out visual slopes inside trimmed region
      const fadeInPx = clamp(
        (params.fadeInMs / trimmedDurationMs) * trimW,
        0,
        trimW
      );
      const fadeOutPx = clamp(
        (params.fadeOutMs / trimmedDurationMs) * trimW,
        0,
        trimW
      );
      if (fadeInPx > 1) {
        ctx.beginPath();
        ctx.moveTo(trimX0, plotY);
        ctx.lineTo(trimX0 + fadeInPx, plotY);
        ctx.lineTo(trimX0, plotY + plotH);
        ctx.closePath();
        ctx.fillStyle = 'rgba(0, 0, 0, 0.34)';
        ctx.fill();
        ctx.beginPath();
        ctx.moveTo(trimX0, plotY + plotH);
        ctx.lineTo(trimX0 + fadeInPx, plotY);
        ctx.strokeStyle = 'rgba(255, 210, 100, 0.85)';
        ctx.lineWidth = 1.25;
        ctx.stroke();
      }
      if (fadeOutPx > 1) {
        ctx.beginPath();
        ctx.moveTo(trimX1, plotY);
        ctx.lineTo(trimX1 - fadeOutPx, plotY);
        ctx.lineTo(trimX1, plotY + plotH);
        ctx.closePath();
        ctx.fillStyle = 'rgba(0, 0, 0, 0.34)';
        ctx.fill();
        ctx.beginPath();
        ctx.moveTo(trimX1 - fadeOutPx, plotY);
        ctx.lineTo(trimX1, plotY + plotH);
        ctx.strokeStyle = 'rgba(255, 210, 100, 0.85)';
        ctx.lineWidth = 1.25;
        ctx.stroke();
      }

      // Build FL Studio AHDSR Envelope curve using proportional schedule matching DSP
      const sustain = clamp(params.sustain, 0, 1);
      const attSpan = Math.max(0.05, geom.attackEndMs);
      const decSpan = Math.max(0.05, geom.decayEndMs - geom.holdEndMs);
      const envelopeLevel = (timeMs: number) => {
        if (timeMs < geom.attackEndMs) {
          return flSegmentLevel(
            timeMs / attSpan,
            0,
            1,
            params.attackCurve,
            params.attackTension
          );
        }
        if (timeMs < geom.holdEndMs) {
          return 1.0;
        }
        if (timeMs < geom.decayEndMs) {
          return flSegmentLevel(
            (timeMs - geom.holdEndMs) / decSpan,
            1.0,
            sustain,
            params.decayCurve,
            params.decayTension
          );
        }
        return sustain;
      };
      const xAt = (ms: number) =>
        trimX0 + (trimW * clamp(ms, 0, trimmedDurationMs)) / trimmedDurationMs;
      const yAt = (lvl: number) => geom.envBot - geom.envH * clamp(lvl, 0, 1);

      const points: Array<{ x: number; y: number }> = [];
      points.push({ x: xAt(0), y: yAt(0) });

      for (let i = 1; i <= CURVE_STEPS; i++) {
        const timeMs = geom.attackEndMs * (i / CURVE_STEPS);
        points.push({ x: xAt(timeMs), y: yAt(envelopeLevel(timeMs)) });
      }

      if (geom.holdEndMs - geom.attackEndMs > 0.5) {
        points.push({ x: xAt(geom.holdEndMs), y: yAt(1.0) });
      }

      for (let i = 1; i <= CURVE_STEPS; i++) {
        const timeMs =
          geom.holdEndMs + (geom.decayEndMs - geom.holdEndMs) * (i / CURVE_STEPS);
        points.push({ x: xAt(timeMs), y: yAt(envelopeLevel(timeMs)) });
      }

      if (geom.releaseStartMs > geom.decayEndMs + 0.5) {
        points.push({ x: xAt(geom.releaseStartMs), y: yAt(envelopeLevel(geom.releaseStartMs)) });
      }

      const releaseStartLevel = envelopeLevel(geom.releaseStartMs);
      for (let i = 1; i <= CURVE_STEPS; i++) {
        const t = i / CURVE_STEPS;
        const level = flSegmentLevel(
          t,
          releaseStartLevel,
          0,
          params.releaseCurve,
          params.releaseTension
        );
        points.push({
          x: xAt(geom.releaseStartMs + geom.releaseMs * t),
          y: yAt(level),
        });
      }

      // FL Studio Section Markers ([D] Decay start, [S] Sustain/Release split) matching Image-Line screenshot
      const drawSectionMarker = (mx: number, tag: string) => {
        if (mx < plotX - 6 || mx > plotX + plotW + 6) return;
        const sx = Math.round(mx) + 0.5;
        ctx.save();
        ctx.strokeStyle = params.envEnabled
          ? 'rgba(240, 248, 255, 0.72)'
          : 'rgba(220, 232, 245, 0.52)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(sx, plotY);
        ctx.lineTo(sx, plotY + plotH);
        ctx.stroke();

        // Chamfered corner [D] or [S] badge matching Image-Line glossary diagram
        const badgeW = 11;
        const badgeH = 12;
        const bx = clamp(sx + 1, plotX + 2, plotX + plotW - badgeW - 2);
        const by = plotY + plotH - badgeH - 1;
        ctx.fillStyle = params.envEnabled ? '#cdd7e3' : '#9ca7b5';
        ctx.beginPath();
        ctx.moveTo(bx, by);
        ctx.lineTo(bx + badgeW - 3, by);
        ctx.lineTo(bx + badgeW, by + 3);
        ctx.lineTo(bx + badgeW, by + badgeH);
        ctx.lineTo(bx, by + badgeH);
        ctx.closePath();
        ctx.fill();
        ctx.fillStyle = '#181e26';
        ctx.font = 'bold 8.5px -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(tag, bx + badgeW * 0.48, by + badgeH * 0.54);
        ctx.restore();
      };

      drawSectionMarker(geom.holdEndX, 'D');
      drawSectionMarker(geom.releaseStartX, 'S');

      // Translucent Image-Line cyan gradient fill under curve (clearly visible even at VST open)
      ctx.save();
      ctx.beginPath();
      points.forEach((pt, idx) => {
        if (idx === 0) ctx.moveTo(pt.x, pt.y);
        else ctx.lineTo(pt.x, pt.y);
      });
      ctx.lineTo(trimX1, envBot);
      ctx.lineTo(trimX0, envBot);
      ctx.closePath();
      const fillGrad = ctx.createLinearGradient(0, geom.envTop, 0, envBot);
      if (params.envEnabled) {
        fillGrad.addColorStop(0, 'rgba(87, 214, 255, 0.28)');
        fillGrad.addColorStop(1, 'rgba(87, 214, 255, 0.05)');
      } else {
        fillGrad.addColorStop(0, 'rgba(87, 214, 255, 0.18)');
        fillGrad.addColorStop(1, 'rgba(87, 214, 255, 0.03)');
      }
      ctx.fillStyle = fillGrad;
      ctx.fill();

      // Signature Image-Line cyan envelope curve stroke (solid & clean on VST open, brighter glow when active)
      ctx.beginPath();
      points.forEach((pt, idx) => {
        if (idx === 0) ctx.moveTo(pt.x, pt.y);
        else ctx.lineTo(pt.x, pt.y);
      });
      ctx.strokeStyle = params.envEnabled
        ? flCurveColour
        : 'rgba(87, 214, 255, 0.82)';
      ctx.lineWidth = params.envEnabled ? 2.15 : 1.85;
      ctx.lineJoin = 'round';
      if (params.envEnabled) {
        ctx.shadowColor = 'rgba(87, 214, 255, 0.45)';
        ctx.shadowBlur = 6;
      }
      ctx.stroke();
      ctx.restore();

      // Subtle prompt if empty sample and/or envelope bypassed
      if (!preview.loaded) {
        ctx.fillStyle = 'rgba(218, 229, 242, 0.42)';
        ctx.font = '11px -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(
          'Drop Sample Here or Click Load Sample',
          plotX + plotW * 0.5,
          plotY + plotH - 20
        );
      }

      if (!params.envEnabled) {
        ctx.save();
        ctx.font = '600 9.5px "Plus Jakarta Sans", -apple-system, sans-serif';
        ctx.fillStyle = 'rgba(157, 223, 247, 0.72)';
        ctx.textAlign = 'right';
        ctx.textBaseline = 'top';
        ctx.fillText('BYPASSED · Click ENV', plotX + plotW - 16, plotY + 8);
        ctx.restore();
      }

      // Front Trim & Back Trim vertical lines + Ableton-style flags (shown cleanly on edges or when trimmed)
      const isTrimmed = params.trimStart > 0.001 || params.trimEnd < 0.999;
      ctx.strokeStyle = isTrimmed ? '#ff7b00' : 'rgba(255, 123, 0, 0.55)';
      ctx.lineWidth = isTrimmed ? 2 : 1.5;
      ctx.beginPath();
      ctx.moveTo(trimX0, plotY);
      ctx.lineTo(trimX0, plotY + plotH);
      ctx.moveTo(trimX1, plotY);
      ctx.lineTo(trimX1, plotY + plotH);
      ctx.stroke();

      // Front flag (top-left of trim)
      ctx.fillStyle = '#ff7b00';
      ctx.beginPath();
      ctx.moveTo(trimX0, plotY);
      ctx.lineTo(trimX0 + 10, plotY + 5.5);
      ctx.lineTo(trimX0, plotY + 11);
      ctx.closePath();
      ctx.fill();

      // Back flag (top-right of trim)
      ctx.beginPath();
      ctx.moveTo(trimX1, plotY);
      ctx.lineTo(trimX1 - 10, plotY + 5.5);
      ctx.lineTo(trimX1, plotY + 11);
      ctx.closePath();
      ctx.fill();

      // Tension Handles (hollow circles ○ at segment midpoints matching Image-Line screenshot)
      const drawTensionHandle = (
        pt: { x: number; y: number; visible: boolean },
        type: HandleType
      ) => {
        if (!pt.visible) return;
        const isCtx = contextMenu?.handle === type;
        const active = draggingHandle === type || hoveredHandle === type || isCtx;
        ctx.save();
        ctx.fillStyle = 'rgba(42, 52, 64, 0.72)';
        ctx.beginPath();
        ctx.arc(pt.x, pt.y, TENSION_RADIUS + (active ? 1.1 : -0.3), 0, Math.PI * 2);
        ctx.fill();

        if (isCtx) {
          ctx.strokeStyle = '#ffae42';
          ctx.lineWidth = 1.6;
          ctx.beginPath();
          ctx.arc(pt.x, pt.y, TENSION_RADIUS + 4, 0, Math.PI * 2);
          ctx.stroke();
        }

        ctx.strokeStyle = active
          ? '#ffffff'
          : params.envEnabled
            ? flCurveColour
            : 'rgba(87, 214, 255, 0.88)';
        ctx.lineWidth = active ? 2.0 : 1.5;
        ctx.beginPath();
        ctx.arc(pt.x, pt.y, TENSION_RADIUS + (active ? 0.9 : -0.3), 0, Math.PI * 2);
        ctx.stroke();
        ctx.restore();
      };

      drawTensionHandle(geom.tensionAttack, 'TensionAttack');
      drawTensionHandle(geom.tensionDecay, 'TensionDecay');
      drawTensionHandle(geom.tensionRelease, 'TensionRelease');

      // Envelope Control Points (crisp outlined FL dots matching Image-Line screenshot, no cluttered letters)
      const drawControlPoint = (
        pt: { x: number; y: number },
        type: HandleType,
        small = false
      ) => {
        if (pt.x < plotX - 12 || pt.x > plotX + plotW + 12) return;
        const active =
          type !== 'None' &&
          (draggingHandle === type ||
            hoveredHandle === type ||
            contextMenu?.handle === type);
        const radius = small ? 3.2 : 3.8;
        ctx.save();
        ctx.fillStyle = '#141920';
        ctx.beginPath();
        ctx.arc(pt.x, pt.y, radius + (active ? 2.4 : 1.4), 0, Math.PI * 2);
        ctx.fill();

        if (type !== 'None' && contextMenu?.handle === type) {
          ctx.strokeStyle = '#ffae42';
          ctx.lineWidth = 1.6;
          ctx.beginPath();
          ctx.arc(pt.x, pt.y, radius + 4.2, 0, Math.PI * 2);
          ctx.stroke();
        }

        ctx.fillStyle = active
          ? '#ffffff'
          : params.envEnabled
            ? flControlDot
            : '#bfefff';
        ctx.beginPath();
        ctx.arc(pt.x, pt.y, radius + (active ? 0.7 : 0), 0, Math.PI * 2);
        ctx.fill();
        ctx.restore();
      };

      drawControlPoint(geom.origin, 'None', true);
      drawControlPoint(geom.attack, 'Attack');
      if (geom.hold.visible) drawControlPoint(geom.hold, 'Hold');
      drawControlPoint(geom.sustain, 'Sustain');
      if (geom.release.visible) drawControlPoint(geom.release, 'Release');
      drawControlPoint(geom.endPt, 'Release', true);

      // Active hover / drag HUD pill so exact values show cleanly on demand
      const activeHud = draggingHandle !== 'None' ? draggingHandle : hoveredHandle;
      if (activeHud !== 'None' && activeHud !== 'Pan') {
        let hudText = '';
        let hudPt: { x: number; y: number } = { x: plotX + plotW * 0.5, y: plotY + 24 };
        if (activeHud === 'TensionAttack') {
          hudText = `Attack Curve ${params.attackTension >= 0 ? '+' : ''}${Math.round(params.attackTension * 100)}%`;
          hudPt = geom.tensionAttack;
        } else if (activeHud === 'TensionDecay') {
          hudText = `Decay Curve ${params.decayTension >= 0 ? '+' : ''}${Math.round(params.decayTension * 100)}%`;
          hudPt = geom.tensionDecay;
        } else if (activeHud === 'TensionRelease') {
          hudText = `Release Curve ${params.releaseTension >= 0 ? '+' : ''}${Math.round(params.releaseTension * 100)}%`;
          hudPt = geom.tensionRelease;
        } else if (activeHud === 'Attack') {
          hudText = `Peak (D) · Att ${formatEnvelopeTime(params.attackMs)}`;
          hudPt = geom.attack;
        } else if (activeHud === 'Hold') {
          hudText = `Hold ${formatEnvelopeTime(params.holdMs)}`;
          hudPt = geom.hold;
        } else if (activeHud === 'Sustain' || activeHud === 'Decay') {
          hudText = `Sustain ${Math.round(params.sustain * 100)}% · Dec ${formatEnvelopeTime(params.decayMs)}`;
          hudPt = geom.sustain;
        } else if (activeHud === 'Release') {
          hudText = `Release ${formatEnvelopeTime(params.releaseMs)}`;
          hudPt = geom.release.visible ? geom.release : geom.endPt;
        }
        if (hudText) {
          ctx.save();
          ctx.font = 'bold 10px -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif';
          const tw = ctx.measureText(hudText).width + 12;
          const bx = clamp(hudPt.x - tw * 0.5, plotX + 4, plotX + plotW - tw - 4);
          const by = clamp(
            hudPt.y < plotY + 34 ? hudPt.y + 12 : hudPt.y - 26,
            plotY + 4,
            plotY + plotH - 22
          );
          ctx.fillStyle = 'rgba(18, 22, 28, 0.92)';
          ctx.strokeStyle = flCurveColour;
          ctx.lineWidth = 1;
          ctx.beginPath();
          ctx.roundRect(bx, by, tw, 18, 3);
          ctx.fill();
          ctx.stroke();
          ctx.fillStyle = '#ffffff';
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          ctx.fillText(hudText, bx + tw * 0.5, by + 9);
          ctx.restore();
        }
      }

      ctx.restore();
    }

    // Bottom time ruler (shows exact timestamps for the active zoom window)
    const rulerY = h - RULER_H;
    ctx.fillStyle = 'rgba(12, 16, 22, 0.65)';
    ctx.fillRect(0, rulerY, w, RULER_H);
    ctx.fillStyle = '#b9c6d6';
    ctx.font = '9.5px monospace';
    ctx.textBaseline = 'middle';
    const durMs = preview.loaded ? preview.durationMs : 1000;
    for (let i = 0; i <= 4; i++) {
      const norm = vp.viewStart + vpSpan * (i / 4);
      const ms = durMs * norm;
      const label =
        ms < 0.5
          ? '0:00'
          : `0:${String(Math.floor(ms / 1000)).padStart(2, '0')}:${String(Math.floor(ms % 1000)).padStart(3, '0')}`;
      const tx = i === 0 ? 4 : i === 4 ? w - 4 : (w * i) / 4;
      ctx.textAlign = i === 0 ? 'left' : i === 4 ? 'right' : 'center';
      ctx.fillText(label, tx, rulerY + RULER_H * 0.5);
    }

    ctx.restore();
  }, [
    contextMenu,
    draggingHandle,
    flControlDot,
    flCurveColour,
    getGeometry,
    hoveredHandle,
    isPre,
    params,
    preview,
    simWaveColour,
    viewport,
  ]);

  useEffect(() => {
    renderCanvas();
    const wrap = plotWrapRef.current;
    if (!wrap) return;
    const ro = new ResizeObserver(renderCanvas);
    ro.observe(wrap);
    return () => ro.disconnect();
  }, [renderCanvas]);

  useEffect(() => {
    if (!contextMenu) return;
    const close = () => setContextMenu(null);
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') close();
    };
    window.addEventListener('pointerdown', close);
    window.addEventListener('keydown', onKey);
    return () => {
      window.removeEventListener('pointerdown', close);
      window.removeEventListener('keydown', onKey);
    };
  }, [contextMenu]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      const rect = canvas.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const w = Math.max(24, rect.width);
      const vp = viewportRef.current;
      const span = getViewportSpan(vp.zoom);

      if (e.shiftKey || Math.abs(e.deltaX) > Math.abs(e.deltaY)) {
        const delta = Math.abs(e.deltaX) > Math.abs(e.deltaY) ? e.deltaX : e.deltaY;
        const deltaNorm = (delta / w) * span * 0.85;
        setViewport((prev) => panViewport(prev, deltaNorm));
      } else if (e.deltaY !== 0) {
        const anchorNorm = plotXToNorm(x, 0, w, vp);
        const factor = e.deltaY < 0 ? 1.35 : 1 / 1.35;
        setViewport((prev) => zoomAroundNormalized(prev, anchorNorm, factor));
      }
    };
    canvas.addEventListener('wheel', onWheel, { passive: false });
    return () => canvas.removeEventListener('wheel', onWheel);
  }, []);

  const getLocalPos = (clientX: number, clientY: number) => {
    const canvas = canvasRef.current;
    if (!canvas) return { x: 0, y: 0, w: 1, h: 1 };
    const rect = canvas.getBoundingClientRect();
    return {
      x: clientX - rect.left,
      y: clientY - rect.top,
      w: rect.width,
      h: rect.height,
    };
  };

  const hitTestHandle = (
    pos: { x: number; y: number; w: number; h: number },
    p: LayerParameters
  ): HandleType => {
    const geom = getGeometry(pos.w, pos.h, p);
    const candidates: Array<{ type: HandleType; pt: { x: number; y: number } }> = [];
    if (geom.tensionAttack.visible) {
      candidates.push({ type: 'TensionAttack', pt: geom.tensionAttack });
    }
    if (geom.tensionDecay.visible) {
      candidates.push({ type: 'TensionDecay', pt: geom.tensionDecay });
    }
    if (geom.tensionRelease.visible) {
      candidates.push({ type: 'TensionRelease', pt: geom.tensionRelease });
    }
    candidates.push({ type: 'Attack', pt: geom.attack });
    if (geom.hold.visible) {
      candidates.push({ type: 'Hold', pt: geom.hold });
    }
    candidates.push({ type: 'Sustain', pt: geom.sustain });
    if (geom.release.visible) {
      candidates.push({ type: 'Release', pt: geom.release });
    }

    let bestDist = 14;
    let best: HandleType = 'None';
    for (const c of candidates) {
      const dist = Math.hypot(pos.x - c.pt.x, pos.y - c.pt.y);
      if (dist < bestDist) {
        bestDist = dist;
        best = c.type;
      }
    }

    if (best === 'None') {
      const startVisible =
        geom.trimX0 >= geom.plotX - 10 &&
        geom.trimX0 <= geom.plotX + geom.plotW + 10;
      const endVisible =
        geom.trimX1 >= geom.plotX - 10 &&
        geom.trimX1 <= geom.plotX + geom.plotW + 10;
      const distStart = startVisible ? Math.abs(pos.x - geom.trimX0) : Infinity;
      const distEnd = endVisible ? Math.abs(pos.x - geom.trimX1) : Infinity;
      if (distStart <= 11 && distStart <= distEnd) {
        best = 'TrimStart';
      } else if (distEnd <= 11) {
        best = 'TrimEnd';
      }
    }
    return best;
  };

  const resolveCircleForContext = (
    pos: { x: number; y: number; w: number; h: number },
    p: LayerParameters
  ): {
    handle: HandleType;
    pt: { x: number; y: number };
    segment: EnvelopeSegment;
    label: string;
  } => {
    const geom = getGeometry(pos.w, pos.h, p);
    const circles: Array<{
      handle: HandleType;
      pt: { x: number; y: number };
      segment: EnvelopeSegment;
      label: string;
    }> = [
      { handle: 'Attack', pt: geom.attack, segment: 'attack', label: 'Point D (Attack Peak)' },
      { handle: 'Sustain', pt: geom.sustain, segment: 'decay', label: 'Point S (Sustain / Decay)' },
      { handle: 'Release', pt: geom.endPt, segment: 'release', label: 'Point End (Release)' },
    ];
    if (geom.hold.visible) {
      circles.push({
        handle: 'Hold',
        pt: geom.hold,
        segment: 'decay',
        label: 'Point H (Hold End)',
      });
    }
    if (geom.release.visible) {
      circles.push({
        handle: 'Release',
        pt: geom.release,
        segment: 'release',
        label: 'Point R (Release Start)',
      });
    }
    if (geom.tensionAttack.visible) {
      circles.push({
        handle: 'TensionAttack',
        pt: geom.tensionAttack,
        segment: 'attack',
        label: 'Attack Tension ○',
      });
    }
    if (geom.tensionDecay.visible) {
      circles.push({
        handle: 'TensionDecay',
        pt: geom.tensionDecay,
        segment: 'decay',
        label: 'Decay Tension ○',
      });
    }
    if (geom.tensionRelease.visible) {
      circles.push({
        handle: 'TensionRelease',
        pt: geom.tensionRelease,
        segment: 'release',
        label: 'Release Tension ○',
      });
    }

    let bestDist = 18;
    let matched: (typeof circles)[number] | null = null;
    for (const c of circles) {
      const dist = Math.hypot(pos.x - c.pt.x, pos.y - c.pt.y);
      if (dist < bestDist) {
        bestDist = dist;
        matched = c;
      }
    }
    if (matched) return matched;

    if (pos.x <= geom.attackEndX + 8) {
      return { handle: 'None', pt: { x: pos.x, y: pos.y }, segment: 'attack', label: 'Attack Curve' };
    }
    if (pos.x >= geom.releaseStartX - 8) {
      return { handle: 'None', pt: { x: pos.x, y: pos.y }, segment: 'release', label: 'Release Curve' };
    }
    return { handle: 'None', pt: { x: pos.x, y: pos.y }, segment: 'decay', label: 'Decay Curve' };
  };

  const handlePointerDown = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const pos = getLocalPos(e.clientX, e.clientY);
    if (pos.y < HEADER_H) return;

    // Middle mouse or Alt+Left-drag pans the zoomed waveform
    if (
      (e.button === 1 || (e.button === 0 && e.altKey)) &&
      viewportRef.current.zoom > 1.01
    ) {
      e.preventDefault();
      setContextMenu(null);
      activeHandleRef.current = 'Pan';
      setDraggingHandle('Pan');
      dragOriginRef.current = {
        startX: pos.x,
        startY: pos.y,
        startTension: 0,
        startViewStart: viewportRef.current.viewStart,
      };
      e.currentTarget.setPointerCapture(e.pointerId);
      return;
    }

    if (e.button !== 0) return;
    setContextMenu(null);
    const cur = paramsRef.current;
    const best = hitTestHandle(pos, cur);

    if (best !== 'None') {
      activeHandleRef.current = best;
      setDraggingHandle(best);
      dragOriginRef.current = {
        startX: pos.x,
        startY: pos.y,
        startTension:
          best === 'TensionAttack'
            ? cur.attackTension
            : best === 'TensionDecay'
              ? cur.decayTension
              : best === 'TensionRelease'
                ? cur.releaseTension
                : 0,
        startViewStart: viewportRef.current.viewStart,
      };
      e.currentTarget.setPointerCapture(e.pointerId);
    }
  };

  const handlePointerMove = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const pos = getLocalPos(e.clientX, e.clientY);
    const cur = paramsRef.current;
    const handle = activeHandleRef.current;

    if (handle === 'Pan') {
      const vp = viewportRef.current;
      const span = getViewportSpan(vp.zoom);
      const dx = pos.x - dragOriginRef.current.startX;
      const nextStart =
        dragOriginRef.current.startViewStart - (dx / Math.max(1, pos.w)) * span;
      setViewport(clampViewport(nextStart, vp.zoom));
      return;
    }

    if (handle === 'None') {
      const hover = hitTestHandle(pos, cur);
      if (hover !== hoveredHandle) setHoveredHandle(hover);
      return;
    }

    const geom = getGeometry(pos.w, pos.h, cur);

    if (
      handle === 'TensionAttack' ||
      handle === 'TensionDecay' ||
      handle === 'TensionRelease'
    ) {
      const deltaNorm = (dragOriginRef.current.startY - pos.y) / 85;
      const nextTen =
        Math.round(
          clamp(dragOriginRef.current.startTension + deltaNorm, -1, 1) * 100
        ) / 100;
      if (handle === 'TensionAttack') {
        onChangeParams({ attackTension: nextTen });
      } else if (handle === 'TensionDecay') {
        onChangeParams({ decayTension: nextTen });
      } else {
        onChangeParams({ releaseTension: nextTen });
      }
      return;
    }

    if (handle === 'TrimStart') {
      const rawNorm = plotXToNorm(
        pos.x,
        geom.plotX,
        geom.plotW,
        viewportRef.current
      );
      const norm = clamp(rawNorm, 0, cur.trimEnd - 0.005);
      onChangeParams({ trimStart: Math.round(norm * 10000) / 10000 });
      return;
    }
    if (handle === 'TrimEnd') {
      const rawNorm = plotXToNorm(
        pos.x,
        geom.plotX,
        geom.plotW,
        viewportRef.current
      );
      const norm = clamp(rawNorm, cur.trimStart + 0.005, 1);
      onChangeParams({ trimEnd: Math.round(norm * 10000) / 10000 });
      return;
    }

    const duration = Math.max(1, geom.trimmedDurationMs);
    const xNorm = clamp((pos.x - geom.trimX0) / Math.max(1, geom.trimW), 0, 1);
    const timeMs = xNorm * duration;

    switch (handle) {
      case 'Attack': {
        const rest = Math.max(0, cur.holdMs) + Math.max(0.1, cur.decayMs) + Math.max(0.1, cur.releaseMs);
        const clampedT = clamp(timeMs, 0.1, duration - 1);
        const nextAttack =
          clampedT + rest <= duration
            ? clampedT
            : (clampedT * rest) / Math.max(1, duration - clampedT);
        onChangeParams({ attackMs: Math.round(clamp(nextAttack, 0.1, 2000) * 10) / 10 });
        break;
      }
      case 'Hold':
        onChangeParams({
          holdMs: Math.round(clamp(timeMs - geom.attackEndMs, 0, 2000) * 10) / 10,
        });
        break;
      case 'Sustain': {
        const level = clamp((geom.envBot - pos.y) / geom.envH, 0, 1);
        if (!geom.release.visible) {
          const minT = Math.min(duration - 1, geom.holdEndMs + 1);
          const splitT = clamp(timeMs, minT, duration - 1);
          const prevRestSum =
            Math.max(0.1, cur.attackMs) +
            Math.max(0, cur.holdMs) +
            Math.max(0.1, cur.decayMs) +
            Math.max(0.1, cur.releaseMs);
          const scale = prevRestSum > duration ? prevRestSum / duration : 1;
          onChangeParams({
            sustain: Math.round(level * 1000) / 1000,
            decayMs: Math.round(clamp((splitT - geom.holdEndMs) * scale, 0.1, 5000) * 10) / 10,
            releaseMs: Math.round(clamp((duration - splitT) * scale, 0.1, 10000) * 10) / 10,
          });
        } else {
          const decT = clamp(timeMs - geom.holdEndMs, 0.1, 5000);
          onChangeParams({
            sustain: Math.round(level * 1000) / 1000,
            decayMs: Math.round(decT * 10) / 10,
          });
        }
        break;
      }
      case 'Release': {
        const level = clamp((geom.envBot - pos.y) / geom.envH, 0, 1);
        onChangeParams({
          sustain: Math.round(level * 1000) / 1000,
          releaseMs: Math.round(clamp(duration - timeMs, 0.1, 10000) * 10) / 10,
        });
        break;
      }
      default:
        break;
    }
  };

  const handlePointerUp = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (activeHandleRef.current !== 'None') {
      activeHandleRef.current = 'None';
      setDraggingHandle('None');
      if (e.currentTarget.hasPointerCapture(e.pointerId)) {
        e.currentTarget.releasePointerCapture(e.pointerId);
      }
    }
  };

  const handleDoubleClick = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const pos = getLocalPos(e.clientX, e.clientY);
    const hit = hitTestHandle(pos, paramsRef.current);
    if (hit === 'TensionAttack') onChangeParams({ attackTension: 0 });
    else if (hit === 'TensionDecay') onChangeParams({ decayTension: 0 });
    else if (hit === 'TensionRelease') onChangeParams({ releaseTension: 0 });
  };

  const handleContextMenu = (e: React.MouseEvent<HTMLCanvasElement>) => {
    e.preventDefault();
    const pos = getLocalPos(e.clientX, e.clientY);
    const hit = resolveCircleForContext(pos, paramsRef.current);
    const rect = e.currentTarget.getBoundingClientRect();
    const menuW = 182;
    const menuH = 296;
    const anchorX = rect.left + hit.pt.x;
    const anchorY = rect.top + hit.pt.y;
    const vw = typeof window !== 'undefined' ? window.innerWidth : 1440;
    const vh = typeof window !== 'undefined' ? window.innerHeight : 900;
    const x = clamp(anchorX - 24, 8, Math.max(8, vw - menuW - 8));
    const spaceBelow = vh - (anchorY + 8);
    const flipUp = spaceBelow < menuH && anchorY - menuH - 8 >= 8;
    const y = flipUp
      ? Math.max(8, anchorY - menuH - 8)
      : clamp(anchorY + 8, 8, Math.max(8, vh - menuH - 8));
    const caretX = clamp(anchorX - x, 12, menuW - 12);
    setContextMenu({
      x,
      y,
      caretX,
      flipUp,
      segment: hit.segment,
      handle: hit.handle,
      label: hit.label,
    });
  };

  const hpfNorm = valueToSkewed(params.highPassHz, 20, 1000, 0.35);
  const lpfNorm = valueToSkewed(params.lowPassHz, 1000, 20000, 0.35);

  const cursorClass =
    draggingHandle === 'Pan'
      ? 'cursor-grabbing'
      : draggingHandle === 'TrimStart' ||
          draggingHandle === 'TrimEnd' ||
          hoveredHandle === 'TrimStart' ||
          hoveredHandle === 'TrimEnd'
        ? 'cursor-ew-resize'
        : draggingHandle === 'TensionAttack' ||
            draggingHandle === 'TensionDecay' ||
            draggingHandle === 'TensionRelease' ||
            hoveredHandle === 'TensionAttack' ||
            hoveredHandle === 'TensionDecay' ||
            hoveredHandle === 'TensionRelease'
          ? 'cursor-ns-resize'
          : 'cursor-crosshair';

  const activeCurveKey: Record<EnvelopeSegment, 'attackCurve' | 'decayCurve' | 'releaseCurve'> = {
    attack: 'attackCurve',
    decay: 'decayCurve',
    release: 'releaseCurve',
  };
  const activeTensionKey: Record<
    EnvelopeSegment,
    'attackTension' | 'decayTension' | 'releaseTension'
  > = {
    attack: 'attackTension',
    decay: 'decayTension',
    release: 'releaseTension',
  };

  const isZoomed = viewport.zoom > 1.01;
  const vpSpan = getViewportSpan(viewport.zoom);
  const formatTrimPct = (v: number) => {
    const pct = v * 100;
    return Math.abs(pct - Math.round(pct)) < 0.05
      ? `${Math.round(pct)}%`
      : `${pct.toFixed(1)}%`;
  };
  const fadeReadout =
    params.fadeInMs > 0.5 || params.fadeOutMs > 0.5
      ? ` | FI ${formatEnvelopeTime(params.fadeInMs)} FO ${formatEnvelopeTime(params.fadeOutMs)}`
      : '';
  const readoutText = `A ${formatEnvelopeTime(params.attackMs)}  H ${formatEnvelopeTime(params.holdMs)}  D ${formatEnvelopeTime(params.decayMs)}  S ${Math.round(params.sustain * 100)}%  R ${formatEnvelopeTime(params.releaseMs)}  |  Trim ${formatTrimPct(params.trimStart)}–${formatTrimPct(params.trimEnd)}${fadeReadout}${params.reverse ? ' | REV' : ''}${!params.envEnabled ? ' | ENV OFF' : ''}`;

  const handleZoomStep = (factor: number) => {
    setViewport((prev) => {
      const span = getViewportSpan(prev.zoom);
      const v0 = prev.viewStart;
      const v1 = v0 + span;
      const startInView = params.trimStart >= v0 - 0.001 && params.trimStart <= v1 + 0.001;
      const endInView = params.trimEnd >= v0 - 0.001 && params.trimEnd <= v1 + 0.001;
      let anchor = v0 + span * 0.5;
      if (startInView && !endInView) anchor = params.trimStart;
      else if (endInView && !startInView) anchor = params.trimEnd;
      return zoomAroundNormalized(prev, anchor, factor);
    });
  };

  const updateOverviewFromClientX = (clientX: number) => {
    const bar = overviewRef.current;
    if (!bar) return;
    const rect = bar.getBoundingClientRect();
    const norm = clamp((clientX - rect.left) / Math.max(1, rect.width), 0, 1);
    const span = getViewportSpan(viewportRef.current.zoom);
    setViewport(clampViewport(norm - span * 0.5, viewportRef.current.zoom));
  };

  return (
    <div className="w-full h-full flex flex-col gap-1 min-h-0">
      {/* Waveform + FL Studio Envelope Curve + Tension Handles + Trim Markers */}
      <div
        ref={plotWrapRef}
        className="flex-1 min-h-0 w-full relative rounded-[3px] overflow-hidden border border-[#2b2b2b]"
      >
        {/* Top AHDSR Readout & Waveform Zoom Controls */}
        <div
          style={{ height: HEADER_H }}
          className="absolute top-0 left-0 right-0 z-10 px-2 flex items-center justify-between gap-2 bg-[#0c1016]/75 border-b border-white/10 select-none"
        >
          <span className="text-[10px] text-[#e7edf5] truncate font-sans">
            {readoutText}
          </span>

          <div className="flex items-center gap-1 shrink-0">
            <button
              type="button"
              data-testid={`${deckId}-zoom-focus-start`}
              onClick={() =>
                setViewport((prev) => focusViewportAt(prev, params.trimStart, 4))
              }
              title="Center zoom on Start (Trim F)"
              aria-label={`Focus ${deckId} start trim point`}
              className="h-[16px] px-1.5 rounded-[2px] border border-[#323044] bg-[#1a1924] hover:bg-[#262536] text-[9.5px] font-bold text-[#ff9f43] cursor-pointer"
            >
              |&lt;
            </button>
            <button
              type="button"
              data-testid={`${deckId}-zoom-focus-end`}
              onClick={() =>
                setViewport((prev) => focusViewportAt(prev, params.trimEnd, 4))
              }
              title="Center zoom on End (Trim B)"
              aria-label={`Focus ${deckId} end trim point`}
              className="h-[16px] px-1.5 rounded-[2px] border border-[#323044] bg-[#1a1924] hover:bg-[#262536] text-[9.5px] font-bold text-[#ff9f43] cursor-pointer"
            >
              &gt;|
            </button>
            <button
              type="button"
              data-testid={`${deckId}-zoom-fit-trim`}
              onClick={() =>
                setViewport(zoomToFitTrim(params.trimStart, params.trimEnd))
              }
              title="Zoom to fit active Start–End trim range"
              aria-label={`Zoom ${deckId} to fit trim range`}
              className="h-[16px] px-1.5 rounded-[2px] border border-[#323044] bg-[#1a1924] hover:bg-[#262536] text-[9.5px] font-semibold text-[#d2dce8] cursor-pointer"
            >
              Fit
            </button>
            <div className="flex items-center rounded-[2px] border border-[#323044] bg-[#16151f] overflow-hidden">
              <button
                type="button"
                data-testid={`${deckId}-zoom-out-btn`}
                disabled={viewport.zoom <= 1.01}
                onClick={() => handleZoomStep(1 / 1.6)}
                title="Zoom out waveform"
                aria-label={`Zoom out ${deckId} waveform`}
                className="h-[16px] w-[18px] flex items-center justify-center text-[11px] font-bold text-[#e7edf5] hover:bg-[#262536] disabled:opacity-35 disabled:cursor-default cursor-pointer"
              >
                −
              </button>
              <button
                type="button"
                data-testid={`${deckId}-zoom-reset-btn`}
                onClick={() => setViewport({ viewStart: 0, zoom: 1 })}
                title="Reset waveform zoom to 1.0×"
                aria-label={`Reset ${deckId} waveform zoom`}
                className={`h-[16px] px-1.5 text-[9.5px] font-mono border-x border-[#323044] cursor-pointer transition-colors ${
                  isZoomed
                    ? 'bg-[#262536] text-[#57d6ff] font-bold'
                    : 'text-[#aebacc] hover:bg-[#222130]'
                }`}
              >
                {formatZoomFactor(viewport.zoom)}
              </button>
              <button
                type="button"
                data-testid={`${deckId}-zoom-in-btn`}
                disabled={viewport.zoom >= MAX_WAVEFORM_ZOOM - 0.01}
                onClick={() => handleZoomStep(1.6)}
                title="Zoom in waveform"
                aria-label={`Zoom in ${deckId} waveform`}
                className="h-[16px] w-[18px] flex items-center justify-center text-[11px] font-bold text-[#e7edf5] hover:bg-[#262536] disabled:opacity-35 disabled:cursor-default cursor-pointer"
              >
                +
              </button>
            </div>
          </div>
        </div>

        <canvas
          ref={canvasRef}
          data-testid="simpler-canvas"
          onPointerDown={handlePointerDown}
          onPointerMove={handlePointerMove}
          onPointerUp={handlePointerUp}
          onPointerCancel={handlePointerUp}
          onPointerLeave={() => {
            if (activeHandleRef.current === 'None') setHoveredHandle('None');
          }}
          onDoubleClick={handleDoubleClick}
          onContextMenu={handleContextMenu}
          title="Wheel to zoom · Alt+drag or Shift+wheel to pan · Drag orange trim bars · Right-click ADSR circles for curve mode"
          className={`block w-full h-full touch-none ${cursorClass}`}
        />

        {/* Horizontal Waveform Overview Pan Bar (shown when zoomed > 1.0x) */}
        {isZoomed && (
          <div
            ref={overviewRef}
            data-testid="waveform-overview-bar"
            onPointerDown={(e) => {
              if (e.button !== 0) return;
              setPanDragging(true);
              updateOverviewFromClientX(e.clientX);
              e.currentTarget.setPointerCapture(e.pointerId);
            }}
            onPointerMove={(e) => {
              if (panDragging) updateOverviewFromClientX(e.clientX);
            }}
            onPointerUp={(e) => {
              if (panDragging) {
                setPanDragging(false);
                if (e.currentTarget.hasPointerCapture(e.pointerId)) {
                  e.currentTarget.releasePointerCapture(e.pointerId);
                }
              }
            }}
            onPointerCancel={() => setPanDragging(false)}
            style={{ bottom: RULER_H, height: OVERVIEW_H }}
            title="Drag or click to pan across full sample waveform"
            className="absolute left-0 right-0 z-10 bg-[#0d1117]/90 border-t border-white/10 cursor-ew-resize select-none"
          >
            {/* Full-sample trim active range indicator */}
            <div
              style={{
                left: `${params.trimStart * 100}%`,
                width: `${Math.max(0.5, (params.trimEnd - params.trimStart) * 100)}%`,
              }}
              className="absolute top-[4px] bottom-[4px] bg-[#ff7b00]/30 border-x border-[#ff7b00]"
            />
            {/* Active viewport thumb */}
            <div
              data-testid="waveform-overview-thumb"
              style={{
                left: `${viewport.viewStart * 100}%`,
                width: `${Math.max(3, vpSpan * 100)}%`,
              }}
              className="absolute top-[1px] bottom-[1px] rounded-[2px] border border-[#dce6f2]/80 bg-white/20"
            />
          </div>
        )}

        {/* FL Studio Right-Click ADSR Circle Curve-Types Dropdown */}
        {contextMenu && (
          <div
            data-testid="fl-curve-menu"
            onPointerDown={(e) => e.stopPropagation()}
            style={{ left: contextMenu.x, top: contextMenu.y }}
            className="fixed z-50 w-[182px] rounded-[3px] border border-[#6e5628] bg-[#2c2312]/98 shadow-2xl py-1 text-[11px] text-[#f2e4c9] select-none"
          >
            {!contextMenu.flipUp && (
              <div
                style={{ left: contextMenu.caretX - 5 }}
                className="absolute -top-[5px] w-2.5 h-2.5 rotate-45 bg-[#2c2312] border-l border-t border-[#6e5628]"
              />
            )}
            <div className="px-2.5 py-1 border-b border-[#4c3b1c]">
              <div className="text-[10px] font-bold text-[#ffcb77] truncate">
                {contextMenu.label}
              </div>
              <div className="mt-1 flex items-center gap-1">
                {(['attack', 'decay', 'release'] as const).map((seg) => {
                  const activeSeg = contextMenu.segment === seg;
                  return (
                    <button
                      key={seg}
                      type="button"
                      onClick={() =>
                        setContextMenu((prev) =>
                          prev ? { ...prev, segment: seg } : null
                        )
                      }
                      className={`flex-1 py-0.5 rounded-[2px] text-[9.5px] font-bold uppercase cursor-pointer transition-colors ${
                        activeSeg
                          ? 'bg-[#ffae42] text-[#1d1609]'
                          : 'bg-[#3d2f16] text-[#cbb690] hover:bg-[#4e3c1c]'
                      }`}
                    >
                      {seg === 'attack' ? 'ATT' : seg === 'decay' ? 'DEC' : 'REL'}
                    </button>
                  );
                })}
              </div>
            </div>
            <div className="py-0.5">
              {CURVE_NAMES.map((modeName, idx) => {
                const selected = params[activeCurveKey[contextMenu.segment]] === idx;
                return (
                  <button
                    key={modeName}
                    type="button"
                    data-testid={`fl-curve-option-${idx}`}
                    onClick={() => {
                      onChangeParams({
                        [activeCurveKey[contextMenu.segment]]: idx,
                      });
                      setContextMenu(null);
                    }}
                    className={`w-full px-2.5 py-1 flex items-center gap-2 text-left transition-colors cursor-pointer ${
                      selected
                        ? 'bg-[#4f3d1d] text-[#ffffff] font-semibold'
                        : 'hover:bg-[#3d2f16] text-[#eadbc0]'
                    }`}
                  >
                    <span
                      className="w-2.5 h-2.5 rounded-full border flex items-center justify-center shrink-0"
                      style={{
                        borderColor: selected ? '#ffae42' : '#8d7549',
                      }}
                    >
                      {selected && (
                        <span className="w-1.5 h-1.5 rounded-full bg-[#ffae42]" />
                      )}
                    </span>
                    <span>{modeName}</span>
                  </button>
                );
              })}
            </div>
            <div className="border-t border-[#4c3b1c] mt-0.5 pt-0.5">
              <button
                type="button"
                onClick={() => {
                  onChangeParams({
                    [activeTensionKey[contextMenu.segment]]: 0,
                  });
                  setContextMenu(null);
                }}
                className="w-full px-2.5 py-1 text-left text-[#d8c8a8] hover:bg-[#3d2f16] cursor-pointer"
              >
                Reset Tension (0%)
              </button>
              <button
                type="button"
                onClick={() => {
                  onChangeParams({ envEnabled: !params.envEnabled });
                  setContextMenu(null);
                }}
                className="w-full px-2.5 py-1 text-left text-[#d8c8a8] hover:bg-[#3d2f16] cursor-pointer"
              >
                {params.envEnabled ? 'Disable Envelope' : 'Enable Envelope'}
              </button>
            </div>
          </div>
        )}
      </div>

      {/* FL Studio Envelope Toolbar Strip */}
      <div className="shrink-0 bg-[#1a1924] border border-[#2d2b3c] rounded-[3px] px-2 py-1 flex items-center justify-between gap-1.5 flex-wrap">
        <div className="flex items-center gap-1.5 flex-wrap">
          <button
            type="button"
            data-testid={`${deckId}-env-enable`}
            aria-label={`Toggle ${deckId} envelope`}
            aria-pressed={params.envEnabled}
            onClick={() => onChangeParams({ envEnabled: !params.envEnabled })}
            title="Enable or bypass the FL envelope"
            className="h-5 px-2 rounded-[2px] border flex items-center gap-1.5 text-[10px] font-bold cursor-pointer transition-colors whitespace-nowrap shrink-0"
            style={{
              backgroundColor: params.envEnabled ? '#172e3c' : '#14131b',
              borderColor: params.envEnabled ? flCurveColour : '#343246',
              color: params.envEnabled ? '#ffffff' : '#9aa0be',
            }}
          >
            <span
              className="w-1.5 h-1.5 rounded-full transition-colors"
              style={{
                backgroundColor: params.envEnabled ? flCurveColour : '#53576e',
                boxShadow: params.envEnabled ? `0 0 6px ${flCurveColour}` : 'none',
              }}
            />
            <span>{params.envEnabled ? 'ENV ON' : 'ENV'}</span>
          </button>

          {(
            [
              {
                label: 'ATT',
                curveKey: 'attackCurve',
                tensionKey: 'attackTension',
                testId: `${deckId}-curve-select-attack`,
              },
              {
                label: 'DEC',
                curveKey: 'decayCurve',
                tensionKey: 'decayTension',
                testId: `${deckId}-curve-select-decay`,
              },
              {
                label: 'REL',
                curveKey: 'releaseCurve',
                tensionKey: 'releaseTension',
                testId: `${deckId}-curve-select-release`,
              },
            ] as const
          ).map(({ label, curveKey, tensionKey, testId }) => {
            const tenPct = Math.round(params[tensionKey] * 100);
            return (
              <div
                key={curveKey}
                className="flex items-center gap-1 bg-[#14131b] border border-[#2d2b3c] rounded-[2px] px-1.5 py-0.5"
              >
                <span className="text-[9px] font-bold text-[#8c91ad]">
                  {label}
                </span>
                <select
                  data-testid={testId}
                  aria-label={`${deckId} ${label} curve mode`}
                  value={params[curveKey]}
                  onChange={(e) =>
                    onChangeParams({ [curveKey]: parseInt(e.target.value, 10) })
                  }
                  className="bg-[#1b1a26] text-[#e5e8f5] border border-[#312f42] rounded-[2px] px-1 py-0 text-[9.5px] font-medium focus:outline-none cursor-pointer"
                >
                  {CURVE_NAMES.map((name, idx) => (
                    <option key={name} value={idx}>
                      {name}
                    </option>
                  ))}
                </select>
                <button
                  type="button"
                  title={`Click to reset ${label} tension (currently ${tenPct >= 0 ? '+' : ''}${tenPct}%)`}
                  onClick={() => onChangeParams({ [tensionKey]: 0 })}
                  className="text-[9px] font-mono tabular-nums px-1 rounded-[2px] text-[#cfd8e3] hover:bg-[#262536] cursor-pointer min-w-[30px] text-right"
                  style={{
                    color:
                      Math.abs(tenPct) > 0
                        ? flCurveColour
                        : 'rgba(182, 188, 214, 0.55)',
                  }}
                >
                  {tenPct > 0 ? `+${tenPct}%` : `${tenPct}%`}
                </button>
              </div>
            );
          })}
        </div>
      </div>

      {/* Snapback Real-Time Sample Parameter Strip */}
      <div className="shrink-0 bg-[#17161f] border border-[#2d2b3c] rounded-[3px] px-1.5 py-1 flex items-center justify-between gap-1 overflow-x-auto">
        <div className="flex items-center gap-1 pr-1.5 border-r border-[#2b2938] shrink-0">
          <button
            type="button"
            aria-label={`Reverse ${deckId} sample`}
            aria-pressed={params.reverse}
            onClick={() => onChangeParams({ reverse: !params.reverse })}
            title="Reverse sample playback"
            className="h-7 px-1.5 rounded-[2px] border text-[9.5px] font-bold cursor-pointer transition-colors whitespace-nowrap"
            style={{
              backgroundColor: params.reverse ? '#ff7b00' : '#232230',
              borderColor: params.reverse ? '#ff9f43' : '#353347',
              color: params.reverse ? '#111111' : '#c9cce2',
            }}
          >
            REV
          </button>

          <RotaryKnob
            label="Vol"
            valueText={`${Math.round(params.gain * 100)}%`}
            normValue={params.gain / 2}
            defaultNorm={0.5}
            accentColour={colour}
            size={24}
            onChangeNorm={(n) => onChangeParams({ gain: n * 2 })}
          />

          <RotaryKnob
            label="Pitch"
            valueText={`${formatPitch(params.pitchSemitones)}st`}
            normValue={(params.pitchSemitones + 24) / 48}
            defaultNorm={0.5}
            bipolar
            accentColour={colour}
            size={24}
            onChangeNorm={(n) =>
              onChangeParams({
                pitchSemitones: Math.round((n * 48 - 24) * 10) / 10,
              })
            }
          />

          <RotaryKnob
            label="Pogo"
            valueText={`${params.pogo > 0 ? '+' : ''}${Math.round(params.pogo * 100)}%`}
            normValue={(params.pogo + 1) / 2}
            defaultNorm={0.5}
            bipolar
            accentColour="#ff7b00"
            size={24}
            onChangeNorm={(n) =>
              onChangeParams({
                pogo: Math.round((n * 2 - 1) * 100) / 100,
              })
            }
          />
        </div>

        <div className="flex items-center gap-1 pr-1.5 border-r border-[#2b2938] shrink-0">
          <RotaryKnob
            label="Trim F"
            valueText={`${Math.round(params.trimStart * 100)}%`}
            normValue={params.trimStart / 0.99}
            defaultNorm={0}
            accentColour="#ff7b00"
            size={24}
            onChangeNorm={(n) => {
              const next = clamp(n * 0.99, 0, params.trimEnd - 0.005);
              onChangeParams({ trimStart: Math.round(next * 1000) / 1000 });
            }}
          />

          <RotaryKnob
            label="Trim B"
            valueText={`${Math.round(params.trimEnd * 100)}%`}
            normValue={(params.trimEnd - 0.005) / 0.995}
            defaultNorm={1}
            accentColour="#ff7b00"
            size={24}
            onChangeNorm={(n) => {
              const next = clamp(0.005 + n * 0.995, params.trimStart + 0.005, 1);
              onChangeParams({ trimEnd: Math.round(next * 1000) / 1000 });
            }}
          />

          <RotaryKnob
            label="Fade In"
            valueText={formatEnvelopeTime(params.fadeInMs)}
            normValue={valueToSkewed(params.fadeInMs, 0, 2000, 0.4)}
            defaultNorm={0}
            accentColour={colour}
            size={24}
            onChangeNorm={(n) =>
              onChangeParams({
                fadeInMs: Math.round(skewedToValue(n, 0, 2000, 0.4) * 10) / 10,
              })
            }
          />

          <RotaryKnob
            label="Fade Out"
            valueText={formatEnvelopeTime(params.fadeOutMs)}
            normValue={valueToSkewed(params.fadeOutMs, 0, 5000, 0.4)}
            defaultNorm={0}
            accentColour={colour}
            size={24}
            onChangeNorm={(n) =>
              onChangeParams({
                fadeOutMs: Math.round(skewedToValue(n, 0, 5000, 0.4) * 10) / 10,
              })
            }
          />
        </div>

        <div className="flex items-center gap-1 shrink-0">
          <RotaryKnob
            label="Shift"
            valueText={`${params.delayShiftMs > 0 ? '+' : ''}${params.delayShiftMs.toFixed(0)}ms`}
            normValue={(params.delayShiftMs + 100) / 200}
            defaultNorm={isPre ? 0.25 : 0.5}
            bipolar
            accentColour={colour}
            size={24}
            onChangeNorm={(n) =>
              onChangeParams({
                delayShiftMs: Math.round((n * 200 - 100) * 10) / 10,
              })
            }
          />
          <RotaryKnob
            label="Pan"
            valueText={formatPan(params.pan)}
            normValue={(params.pan + 1) / 2}
            defaultNorm={0.5}
            bipolar
            accentColour={colour}
            size={24}
            onChangeNorm={(n) =>
              onChangeParams({ pan: Math.round((n * 2 - 1) * 100) / 100 })
            }
          />
          <RotaryKnob
            label="HP"
            valueText={formatHzShort(params.highPassHz)}
            normValue={hpfNorm}
            defaultNorm={0}
            accentColour={colour}
            size={24}
            onChangeNorm={(n) =>
              onChangeParams({
                highPassHz: Math.round(skewedToValue(n, 20, 1000, 0.35)),
              })
            }
          />
          <RotaryKnob
            label="LP"
            valueText={formatHzShort(params.lowPassHz)}
            normValue={lpfNorm}
            defaultNorm={1}
            accentColour={colour}
            size={24}
            onChangeNorm={(n) =>
              onChangeParams({
                lowPassHz: Math.round(skewedToValue(n, 1000, 20000, 0.35)),
              })
            }
          />
        </div>
      </div>
    </div>
  );
};
