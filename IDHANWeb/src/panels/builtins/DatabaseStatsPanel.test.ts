import { describe, expect, it } from 'vitest';
import { aggregateMimeBytes, buildDonutRing, buildStorageBreakdown } from './DatabaseStatsPanel';
import type { StorageNode } from '../../api/types';

describe('buildStorageBreakdown', () => {
  it('returns empty for a null or childless tree', () => {
    expect(buildStorageBreakdown(null)).toEqual({ major: [], minor: [], grand: 0 });
    expect(buildStorageBreakdown({ name: 'root', value: 0 })).toEqual({ major: [], minor: [], grand: 0 });
  });

  it('sums heap + indexes into a footprint and sorts tables descending', () => {
    const root: StorageNode = {
      name: 'root',
      value: 0,
      children: [
        { name: 'small', value: 10, children: [{ name: 'small_idx', value: 5 }] },
        { name: 'big', value: 100, children: [{ name: 'big_idx', value: 50 }] },
      ],
    };
    const { major, grand } = buildStorageBreakdown(root, 0);
    expect(grand).toBe(165); // 15 + 150
    expect(major.map((t) => t.name)).toEqual(['big', 'small']);
    expect(major[0].footprint).toBe(150);
    expect(major[1].footprint).toBe(15);
  });

  it('folds tables below the threshold into minor', () => {
    const root: StorageNode = {
      name: 'root',
      value: 0,
      children: [
        { name: 'huge', value: 990 },
        { name: 'tiny_a', value: 6 },
        { name: 'tiny_b', value: 4 },
      ],
    };
    // grand = 1000; threshold 1% = 10. tiny_a (6) and tiny_b (4) fall below.
    const { major, minor } = buildStorageBreakdown(root, 0.01);
    expect(major.map((t) => t.name)).toEqual(['huge']);
    expect(minor.map((t) => t.name)).toEqual(['tiny_a', 'tiny_b']);
  });

  it('drops zero-footprint tables and zero-size indexes', () => {
    const root: StorageNode = {
      name: 'root',
      value: 0,
      children: [
        { name: 'empty', value: 0, children: [{ name: 'empty_idx', value: 0 }] },
        { name: 'kept', value: 100, children: [{ name: 'zero_idx', value: 0 }, { name: 'real_idx', value: 20 }] },
      ],
    };
    const { major } = buildStorageBreakdown(root, 0);
    expect(major.map((t) => t.name)).toEqual(['kept']);
    expect(major[0].indexes.map((i) => i.name)).toEqual(['real_idx']);
  });
});

describe('buildDonutRing', () => {
  it('returns an empty ring for no items', () => {
    expect(buildDonutRing([])).toEqual({ segments: [], otherValue: 0, otherCount: 0, otherItems: [], total: 0 });
  });

  it('drops zero/negative-value items', () => {
    const ring = buildDonutRing([
      { name: 'a', value: 10 },
      { name: 'zero', value: 0 },
      { name: 'neg', value: -5 },
    ]);
    expect(ring.segments.map((s) => s.label)).toEqual(['a']);
    expect(ring.otherValue).toBe(0);
  });

  it('keeps the top N by value and folds the rest into "Other", regardless of individual share', () => {
    const items = [
      { name: 'huge', value: 1000 },
      { name: 'big', value: 500 },
      { name: 'mid', value: 300 },
      { name: 'small', value: 100 },
      { name: 'smaller', value: 50 },
      { name: 'tiny_but_not_smallest', value: 40 },
      { name: 'tiny', value: 5 },
    ];
    const ring = buildDonutRing(items, 5);
    expect(ring.segments.map((s) => s.label)).toEqual(['huge', 'big', 'mid', 'small', 'smaller']);
    expect(ring.otherCount).toBe(2);
    expect(ring.otherValue).toBe(45);
    expect(ring.total).toBe(1995);
    // The folded items themselves survive, sorted descending, so a legend can reveal what "Other" is.
    expect(ring.otherItems.map((i) => [i.label, i.value])).toEqual([
      ['tiny_but_not_smallest', 40],
      ['tiny', 5],
    ]);
    // All folded items share one neutral color (they didn't earn an individual palette slot).
    expect(new Set(ring.otherItems.map((i) => i.color)).size).toBe(1);
  });

  it('assigns palette colors by rank, deterministically, regardless of input order', () => {
    const forward = buildDonutRing([
      { name: 'a', value: 5 },
      { name: 'b', value: 4 },
      { name: 'c', value: 3 },
    ]);
    const shuffled = buildDonutRing([
      { name: 'c', value: 3 },
      { name: 'a', value: 5 },
      { name: 'b', value: 4 },
    ]);
    // Same ranking (a > b > c) regardless of input order, so the same color goes to the same rank.
    expect(forward.segments.map((s) => [s.label, s.color])).toEqual(shuffled.segments.map((s) => [s.label, s.color]));
    expect(new Set(forward.segments.map((s) => s.color)).size).toBe(3); // each rank gets a distinct slot
  });

  it('omits the "Other" bucket entirely when nothing is folded', () => {
    const ring = buildDonutRing([{ name: 'only', value: 1 }], 5);
    expect(ring.otherValue).toBe(0);
    expect(ring.otherCount).toBe(0);
  });

  it('carries an optional sub through to the segment, leaving it undefined when omitted', () => {
    const ring = buildDonutRing([
      { name: 'with-sub', value: 10, sub: 'read-only · limit 50.0 GB' },
      { name: 'without-sub', value: 5 },
    ]);
    expect(ring.segments.find((s) => s.label === 'with-sub')?.sub).toBe('read-only · limit 50.0 GB');
    expect(ring.segments.find((s) => s.label === 'without-sub')?.sub).toBeUndefined();
  });
});

describe('aggregateMimeBytes', () => {
  it('sums the same mime across clusters', () => {
    const totals = aggregateMimeBytes([
      { by_mime: [{ mime: 'image/png', count: 2, bytes: 100 }] },
      { by_mime: [{ mime: 'image/png', count: 1, bytes: 50 }] },
    ]);
    expect(totals).toEqual([{ name: 'image/png', value: 150 }]);
  });

  it('folds the null-mime bucket into a single "unknown" label', () => {
    const totals = aggregateMimeBytes([
      { by_mime: [{ mime: null, count: 1, bytes: 10 }] },
      { by_mime: [{ mime: null, count: 1, bytes: 20 }] },
    ]);
    expect(totals).toEqual([{ name: 'unknown (not obtained)', value: 30 }]);
  });

  it('returns an empty array for no clusters or empty breakdowns', () => {
    expect(aggregateMimeBytes([])).toEqual([]);
    expect(aggregateMimeBytes([{ by_mime: [] }])).toEqual([]);
  });
});
