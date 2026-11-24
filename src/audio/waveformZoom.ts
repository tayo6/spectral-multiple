export interface WaveformViewport {
  /** Normalized start of the visible window in [0, 1]. */
  viewStart: number;
  /** Horizontal zoom factor in [1, MAX_WAVEFORM_ZOOM]. */
  zoom: number;
}

export const MIN_WAVEFORM_ZOOM = 1;
export const MAX_WAVEFORM_ZOOM = 32;

function clampNumber(val: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, val));
}

export function getViewportSpan(zoom: number): number {
  const safeZoom = clampNumber(
    Number.isFinite(zoom) ? zoom : MIN_WAVEFORM_ZOOM,
    MIN_WAVEFORM_ZOOM,
    MAX_WAVEFORM_ZOOM
  );
  return 1 / safeZoom;
}

export function clampViewport(
  viewStart: number,
  zoom: number
): WaveformViewport {
  const safeZoom = clampNumber(
    Number.isFinite(zoom) ? zoom : MIN_WAVEFORM_ZOOM,
    MIN_WAVEFORM_ZOOM,
    MAX_WAVEFORM_ZOOM
  );
  const span = 1 / safeZoom;
  const maxStart = Math.max(0, 1 - span);
  const safeStart = clampNumber(
    Number.isFinite(viewStart) ? viewStart : 0,
    0,
    maxStart
  );
  return {
    viewStart: safeZoom <= 1.0001 ? 0 : safeStart,
    zoom: safeZoom <= 1.0001 ? 1 : safeZoom,
  };
}

/**
 * Zooms in/out by `factor` while keeping `anchorNorm` at the same relative
 * horizontal position inside the visible viewport.
 */
export function zoomAroundNormalized(
  viewport: WaveformViewport,
  anchorNorm: number,
  factor: number
): WaveformViewport {
  const current = clampViewport(viewport.viewStart, viewport.zoom);
  const curSpan = 1 / current.zoom;
  const safeAnchor = clampNumber(anchorNorm, 0, 1);
  const anchorRatio = clampNumber((safeAnchor - current.viewStart) / curSpan, 0, 1);

  const nextZoom = clampNumber(
    current.zoom * (Number.isFinite(factor) && factor > 0 ? factor : 1),
    MIN_WAVEFORM_ZOOM,
    MAX_WAVEFORM_ZOOM
  );
  const nextSpan = 1 / nextZoom;
  const nextStart = safeAnchor - anchorRatio * nextSpan;
  return clampViewport(nextStart, nextZoom);
}

export function panViewport(
  viewport: WaveformViewport,
  deltaNorm: number
): WaveformViewport {
  const current = clampViewport(viewport.viewStart, viewport.zoom);
  return clampViewport(current.viewStart + deltaNorm, current.zoom);
}

/**
 * Centers the current zoom window around `targetNorm` (useful for jumping to Front/Back trim).
 * If still at 1x zoom, gently zooms in to `defaultZoomIfUnzoomed` (e.g. 4x) so start/end details become visible.
 */
export function focusViewportAt(
  viewport: WaveformViewport,
  targetNorm: number,
  defaultZoomIfUnzoomed = 4
): WaveformViewport {
  const current = clampViewport(viewport.viewStart, viewport.zoom);
  const nextZoom =
    current.zoom <= 1.05
      ? clampNumber(defaultZoomIfUnzoomed, MIN_WAVEFORM_ZOOM, MAX_WAVEFORM_ZOOM)
      : current.zoom;
  const span = 1 / nextZoom;
  const safeTarget = clampNumber(targetNorm, 0, 1);
  return clampViewport(safeTarget - span * 0.5, nextZoom);
}

/**
 * Zooms cleanly to frame the trimmed sample region `[trimStart, trimEnd]` with padding.
 */
export function zoomToFitTrim(
  trimStart: number,
  trimEnd: number,
  paddingFraction = 0.08
): WaveformViewport {
  const t0 = clampNumber(trimStart, 0, 0.99);
  const t1 = clampNumber(trimEnd, t0 + 0.005, 1);
  const span = Math.max(0.01, t1 - t0);
  if (span >= 0.96) {
    return { viewStart: 0, zoom: 1 };
  }
  const pad = span * clampNumber(paddingFraction, 0, 0.4);
  const paddedStart = Math.max(0, t0 - pad);
  const paddedEnd = Math.min(1, t1 + pad);
  const targetSpan = Math.max(1 / MAX_WAVEFORM_ZOOM, paddedEnd - paddedStart);
  const zoom = clampNumber(1 / targetSpan, MIN_WAVEFORM_ZOOM, MAX_WAVEFORM_ZOOM);
  return clampViewport(paddedStart, zoom);
}

export function normToPlotX(
  norm: number,
  plotX: number,
  plotW: number,
  viewport: WaveformViewport
): number {
  const span = getViewportSpan(viewport.zoom);
  const rel = (norm - viewport.viewStart) / span;
  return plotX + rel * plotW;
}

export function plotXToNorm(
  x: number,
  plotX: number,
  plotW: number,
  viewport: WaveformViewport
): number {
  const span = getViewportSpan(viewport.zoom);
  const rel = (x - plotX) / Math.max(1, plotW);
  return clampNumber(viewport.viewStart + rel * span, 0, 1);
}

export function formatZoomFactor(zoom: number): string {
  if (zoom <= 1.02) return '1.0×';
  if (zoom >= 10) return `${Math.round(zoom)}×`;
  return `${zoom.toFixed(1)}×`;
}
