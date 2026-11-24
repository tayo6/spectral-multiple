import test from 'node:test';
import assert from 'node:assert/strict';
import {
  barDurationMs,
  computeGridTicks,
  computeLayerBarPlacement,
  computeSecondsTicks,
} from './timelineMath';

test('barDurationMs calculates 1-bar duration from BPM and clamps safely', () => {
  assert.equal(barDurationMs(120), 2000);
  assert.equal(barDurationMs(60), 4000);
  assert.equal(barDurationMs(10), 6000); // clamped to 40 BPM
});

test('computeGridTicks subdivides from beats at 1x to 1/64 notes when zoomed in', () => {
  const full = computeGridTicks(0, 1);
  const labeled = full.filter((t) => t.label.length > 0).map((t) => t.label);
  assert.deepEqual(labeled, ['1.1', '1.2', '1.3', '1.4', '2.1']);

  const zoomed = computeGridTicks(0.25, 16);
  assert.ok(zoomed.some((t) => t.label.includes('1.2.')));
  assert.ok(zoomed.every((t) => t.norm >= 0.23 && t.norm <= 0.33));
});

test('computeSecondsTicks adapts tick resolution in seconds and ms on zoom', () => {
  const full = computeSecondsTicks(0, 1, 120);
  const majors = full.filter((t) => t.major).map((t) => t.label);
  assert.equal(majors[0], '0.00s');
  assert.equal(majors[majors.length - 1], '2.00s');

  const zoomedOrigin = computeSecondsTicks(0, 20, 120);
  assert.ok(zoomedOrigin.some((t) => t.label.endsWith('ms')));

  const zoomedMid = computeSecondsTicks(0.45, 20, 120);
  assert.ok(zoomedMid.some((t) => /\.\d{3}s$/.test(t.label)));
});

test('computeLayerBarPlacement maps shift, trim, and pitch onto 1-bar normalized window', () => {
  const placed = computeLayerBarPlacement(1000, 0, 0.5, 0, -50, 120, 0.5);
  assert.equal(placed.trimmedMs, 500);
  assert.equal(placed.playbackDurationMs, 500);
  // -50ms out of 2000ms bar = -0.025 from anchor 0.5
  assert.ok(Math.abs(placed.startNorm - 0.475) < 1e-6);
  assert.ok(Math.abs(placed.endNorm - 0.725) < 1e-6);

  // +12st halves playback duration
  const pitchedUp = computeLayerBarPlacement(1000, 0, 1, 12, 0, 120, 0.5);
  assert.ok(Math.abs(pitchedUp.playbackDurationMs - 500) < 1e-4);
});
