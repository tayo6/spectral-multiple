import test from 'node:test';
import assert from 'node:assert/strict';
import {
  computeEnvelopeSchedule,
  computeFadeGains,
  computePogoSemitones,
  computeSampleReadPosition,
  computeTrimBounds,
} from './dspMath';
import { flSegmentLevel } from './curves';

test('computeTrimBounds clamps and orders front/back trim indices', () => {
  const full = computeTrimBounds(1000, 0, 1);
  assert.equal(full.trimStartIdx, 0);
  assert.equal(full.trimEndIdx, 1000);
  assert.equal(full.trimmedLength, 1000);

  const cropped = computeTrimBounds(1000, 0.25, 0.75);
  assert.equal(cropped.trimStartIdx, 250);
  assert.equal(cropped.trimEndIdx, 750);
  assert.equal(cropped.trimmedLength, 500);

  const inverted = computeTrimBounds(1000, 0.8, 0.2);
  assert.ok(inverted.trimEndIdx > inverted.trimStartIdx);
  assert.ok(inverted.trimmedLength >= 1);
});

test('computeEnvelopeSchedule preserves 1:1 durations and scales proportionally on shorter samples', () => {
  const open = computeEnvelopeSchedule(210, 0, 310, 480, 1000);
  assert.equal(open.attackEndMs, 210);
  assert.equal(open.holdEndMs, 210);
  assert.equal(open.decayEndMs, 520);
  assert.equal(open.releaseStartMs, 520);
  assert.equal(open.releaseMs, 480);

  const scaled = computeEnvelopeSchedule(210, 0, 310, 480, 500);
  assert.equal(scaled.attackEndMs, 105);
  assert.equal(scaled.holdEndMs, 105);
  assert.equal(scaled.decayEndMs, 260);
  assert.equal(scaled.releaseStartMs, 260);
  assert.equal(scaled.releaseMs, 240);

  const longWindow = computeEnvelopeSchedule(100, 0, 200, 300, 1000);
  assert.equal(longWindow.attackEndMs, 100);
  assert.equal(longWindow.decayEndMs, 300);
  assert.equal(longWindow.releaseStartMs, 700);
  assert.equal(longWindow.releaseMs, 300);
});

test('computePogoSemitones bends pitch at onset and decays to zero by end', () => {
  assert.equal(computePogoSemitones(1.0, 0, 1000), 24.0);
  assert.equal(computePogoSemitones(-0.5, 0, 1000), -12.0);
  assert.equal(computePogoSemitones(1.0, 1000, 1000), 0);
  const mid = computePogoSemitones(1.0, 500, 1000);
  assert.ok(mid > 0 && mid < 24);
});

test('computeSampleReadPosition supports forward and reverse traversal within trim range', () => {
  assert.equal(computeSampleReadPosition(200, 800, 0, false), 200);
  assert.equal(computeSampleReadPosition(200, 800, 50, false), 250);
  assert.equal(computeSampleReadPosition(200, 800, 0, true), 799);
  assert.equal(computeSampleReadPosition(200, 800, 50, true), 749);
});

test('computeFadeGains computes linear fade-in and fade-out ramps', () => {
  const sr = 48000;
  const start = computeFadeGains(0, 48000, 100, 200, sr);
  assert.equal(start.fadeInGain, 0);
  assert.equal(start.fadeOutGain, 1);

  const midFadeIn = computeFadeGains(2400, 45600, 100, 200, sr);
  assert.ok(Math.abs(midFadeIn.fadeInGain - 0.5) < 1e-6);
  assert.equal(midFadeIn.fadeOutGain, 1);

  const midFadeOut = computeFadeGains(43200, 4800, 100, 200, sr);
  assert.equal(midFadeOut.fadeInGain, 1);
  assert.ok(Math.abs(midFadeOut.fadeOutGain - 0.5) < 1e-6);

  const end = computeFadeGains(48000, 0, 100, 200, sr);
  assert.equal(end.fadeOutGain, 0);

  const overlap = computeFadeGains(2400, 2400, 100, 100, sr);
  assert.ok(Math.abs(overlap.fadeInGain - 0.5) < 1e-6);
  assert.ok(Math.abs(overlap.fadeOutGain - 0.5) < 1e-6);
  assert.ok(Math.abs(overlap.combinedGain - 0.25) < 1e-6);

  const zeroFades = computeFadeGains(0, 0, 0, 0, sr);
  assert.equal(zeroFades.fadeInGain, 1);
  assert.equal(zeroFades.fadeOutGain, 1);
  assert.equal(zeroFades.combinedGain, 1);
});

test('flSegmentLevel handles all 8 FL Studio curve modes and visual tension direction', () => {
  // Single curve (mode 2): positive tension bends midpoint UP, negative bends DOWN (both rising & falling)
  assert.ok(flSegmentLevel(0.5, 0, 1, 2, 0.6) > 0.5);
  assert.ok(flSegmentLevel(0.5, 0, 1, 2, -0.6) < 0.5);
  assert.ok(flSegmentLevel(0.5, 1, 0, 2, 0.6) > 0.5);
  assert.ok(flSegmentLevel(0.5, 1, 0, 2, -0.6) < 0.5);
  assert.ok(Math.abs(flSegmentLevel(0.5, 0, 1, 2, 0) - 0.5) < 1e-6);

  // Hold (mode 0) stays at y0 until t=1
  assert.ok(Math.abs(flSegmentLevel(0.99, 0.8, 0.2, 0, 0) - 0.8) < 1e-6);
  assert.ok(Math.abs(flSegmentLevel(1.0, 0.8, 0.2, 0, 0) - 0.2) < 1e-6);

  // Smooth (mode 1), Half sine (mode 3), Stairs (mode 4), Smooth stairs (mode 5), Pulse (mode 6), Wave (mode 7)
  for (const mode of [1, 2, 3, 4, 5, 6, 7]) {
    assert.ok(Math.abs(flSegmentLevel(0, 0.2, 0.9, mode, 0.2) - 0.2) < 1e-6);
    assert.ok(Math.abs(flSegmentLevel(1, 0.2, 0.9, mode, 0.2) - 0.9) < 1e-6);
  }
});
