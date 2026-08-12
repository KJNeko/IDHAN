import {describe, expect, it} from 'vitest';
import {barFraction, buildCompareRows, deltaScale, sortRowsByDelta, type CompareRow} from './compareRows';
import type {CompareTerm} from './embeddingTerms';

const TERMS: CompareTerm[] = [
    {kind: 'text', text: 'blonde hair', enabled: true},
    {kind: 'text', text: 'night', enabled: true},
    {kind: 'record', recordId: 55, enabled: true},
];

const DISTANCES = [
    [0.412, 0.478],
    [0.501, 0.489],
    [0.455, 0.457],
];

function rowsOf(deltas: number[]): CompareRow[] {
    return deltas.map((delta, index) => ({
        label: `row ${index}`,
        distanceA: 0.5 + delta,
        distanceB: 0.5,
        delta,
    }));
}

describe('buildCompareRows', () => {
    it('pairs each term with its two distances', () => {
        const rows = buildCompareRows(TERMS, DISTANCES);
        expect(rows).toHaveLength(3);
        expect(rows[0]).toEqual({label: 'blonde hair', distanceA: 0.412, distanceB: 0.478, delta: 0.412 - 0.478});
        expect(rows[2].label).toBe('record 55');
    });

    // delta is A minus B, so a negative delta means A is the closer image.
    it('signs the delta toward A when A is closer', () => {
        const rows = buildCompareRows(TERMS, DISTANCES);
        expect(rows[0].delta).toBeLessThan(0);
        expect(rows[1].delta).toBeGreaterThan(0);
    });

    // The matrix comes from our own request, so a mismatch is a server bug. Rendering the rows that
    // do line up beats throwing away a whole response or, worse, mislabelling a number.
    it('builds only the rows both sides have', () => {
        expect(buildCompareRows(TERMS, DISTANCES.slice(0, 2))).toHaveLength(2);
        expect(buildCompareRows(TERMS.slice(0, 1), DISTANCES)).toHaveLength(1);
    });

    it('skips a term whose distances are not a pair', () => {
        expect(buildCompareRows(TERMS.slice(0, 1), [[0.4]])).toHaveLength(0);
    });
});

describe('sortRowsByDelta', () => {
    it('puts the strongest difference first regardless of direction', () => {
        const sorted = sortRowsByDelta(rowsOf([0.01, -0.2, 0.15]));
        expect(sorted.map((row) => row.delta)).toEqual([-0.2, 0.15, 0.01]);
    });

    it('leaves the input untouched', () => {
        const rows = rowsOf([0.01, -0.2]);
        sortRowsByDelta(rows);
        expect(rows.map((row) => row.delta)).toEqual([0.01, -0.2]);
    });
});

describe('deltaScale', () => {
    it('is the largest magnitude present', () => {
        expect(deltaScale(rowsOf([0.01, -0.2, 0.15]))).toBeCloseTo(0.2);
    });

    // Every distance in a CLIP-style model lands in a narrow band, so identical deltas are a real
    // case rather than a contrived one, and dividing by that scale must not produce NaN.
    it('never returns zero', () => {
        expect(deltaScale(rowsOf([0, 0, 0]))).toBeGreaterThan(0);
        expect(deltaScale([])).toBeGreaterThan(0);
    });
});

describe('barFraction', () => {
    it('is one at the scale and a half at half of it', () => {
        expect(barFraction(-0.2, 0.2)).toBeCloseTo(1);
        expect(barFraction(0.1, 0.2)).toBeCloseTo(0.5);
        expect(barFraction(0, 0.2)).toBeCloseTo(0);
    });

    it('clamps rather than overflowing its half of the bar', () => {
        expect(barFraction(0.5, 0.2)).toBe(1);
    });
});
