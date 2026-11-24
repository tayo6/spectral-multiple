import test from 'node:test';
import assert from 'node:assert/strict';
import {
  clampViewport,
  focusViewportAt,
  formatZoomFactor,
  normToPlotX,
  panViewport,
  plotXToNorm,
  zoomAroundNormalized,
  zoomToFitTrim,
} from './waveformZoom';

test('clampViewport enforces zoom bounds and keeps viewStart within [0, 1 - span]', () => {
  assert.deepEqual(clampViewport(-0.2, 0.5), { viewStart: 0, zoom: 1 });
  assert.deepEqual(clampViewport(0.95, 4), { viewStart: 0.75, zoom: 4 });
  assert.deepEqual(clampViewport(0.99, 64), { viewStart: 1 - 1 / 32, zoom: 32 });
});

test('zoomAroundNormalized keeps anchor stationary across zoom in and out', () => {
  const base = { viewStart: 0, zoom: 1 };
  // Anchored at Front Trim (0.0) stays pinned at left edge
  const atStart = zoomAroundNormalized(base, 0.0, 4);
  assert.equal(atStart.zoom, 4);
  assert.equal(atStart.viewStart, 0);

  // Anchored at Back Trim (1.0) stays pinned at right edge
  const atEnd = zoomAroundNormalized(base, 1.0, 4);
  assert.equal(atEnd.zoom, 4);
  assert.ok(Math.abs(atEnd.viewStart - 0.75) < 1e-6);

  // Anchored at arbitrary point keeps pixel ratio constant
  const z = zoomAroundNormalized({ viewStart: 0.1, zoom: 2 }, 0.35, 2);
  const beforeRatio = (0.35 - 0.1) / 0.5;
  const afterRatio = (0.35 - z.viewStart) / (1 / z.zoom);
  assert.ok(Math.abs(beforeRatio - afterRatio) < 1e-6);
});

test('normToPlotX and plotXToNorm round-trip accurately when zoomed', () => {
  const vp = { viewStart: 0.25, zoom: 4 }; // visible window: [0.25, 0.50]
  const plotX = 10;
  const plotW = 800;

  assert.equal(normToPlotX(0.25, plotX, plotW, vp), 10);
  assert.equal(normToPlotX(0.5, plotX, plotW, vp), 810);
  assert.equal(normToPlotX(0.375, plotX, plotW, vp), 410);

  assert.ok(Math.abs(plotXToNorm(410, plotX, plotW, vp) - 0.375) < 1e-6);
  assert.ok(Math.abs(plotXToNorm(10, plotX, plotW, vp) - 0.25) < 1e-6);
});

test('zoomToFitTrim, focusViewportAt, and panViewport keep coordinates bounded', () => {
  const fitted = zoomToFitTrim(0.2, 0.4, 0.1); // span 0.2, pad 0.02 -> [0.18, 0.42] (span 0.24)
  assert.ok(fitted.zoom > 4 && fitted.zoom < 4.5);
  assert.ok(Math.abs(fitted.viewStart - 0.18) < 1e-6);

  // Near-full trim stays at 1x
  assert.deepEqual(zoomToFitTrim(0.01, 0.99), { viewStart: 0, zoom: 1 });

  // Focus jump zooms in from 1x and centers around target
  const focused = focusViewportAt({ viewStart: 0, zoom: 1 }, 0.8, 4);
  assert.equal(focused.zoom, 4);
  assert.ok(Math.abs(focused.viewStart - 0.675) < 1e-6);

  // Pan clamps cleanly
  const panned = panViewport({ viewStart: 0.5, zoom: 4 }, 0.4);
  assert.equal(panned.viewStart, 0.75);
  assert.equal(formatZoomFactor(1), '1.0×');
  assert.equal(formatZoomFactor(4), '4.0×');
  assert.equal(formatZoomFactor(16), '16×');
});
