import {describe, expect, it} from 'vitest';
import {
    barFraction,
    buildCompareRows,
    deltaScale,
    indexRowsByLabel,
    orderTerms,
    type CompareRow,
    type CompareSortMode,
} from './compareRows';
import {compareTermLabel, type CompareTerm} from './embeddingTerms';

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

describe('indexRowsByLabel', () => {
    it('keys each row by its label', () => {
        const index = indexRowsByLabel(buildCompareRows(TERMS, DISTANCES));
        expect(index.get('blonde hair')?.distanceA).toBeCloseTo(0.412);
        expect(index.get('record 55')?.distanceB).toBeCloseTo(0.457);
        expect(index.get('never asked')).toBeUndefined();
    });
});

describe('orderTerms', () => {
    const index = indexRowsByLabel(buildCompareRows(TERMS, DISTANCES));
    const order = (mode: CompareSortMode, terms: readonly CompareTerm[] = TERMS) =>
        orderTerms(terms, index, mode).map(compareTermLabel);

    // deltas: blonde hair -0.066 (leans A), night +0.012 (leans B), record 55 -0.002 (near neutral)
    it('keeps the typed order under "entered"', () => {
        expect(order('entered')).toEqual(['blonde hair', 'night', 'record 55']);
    });

    it('ranks by the size of the difference under "delta"', () => {
        expect(order('delta')).toEqual(['blonde hair', 'night', 'record 55']);
    });

    it('ranks by which side a term leans toward', () => {
        expect(order('toward-a')).toEqual(['blonde hair', 'record 55', 'night']);
        expect(order('toward-b')).toEqual(['night', 'record 55', 'blonde hair']);
    });

    // Distinct from leaning: a term can be near both images while leaning slightly to one.
    it('ranks by raw closeness to each image', () => {
        expect(order('closest-a')).toEqual(['blonde hair', 'record 55', 'night']);
        expect(order('closest-b')).toEqual(['record 55', 'blonde hair', 'night']);
    });

    it('sorts alphabetically under "label"', () => {
        expect(order('label')).toEqual(['blonde hair', 'night', 'record 55']);
    });

    // A term added since the last run has no numbers yet. It must not sort as if its difference were
    // zero, which would bury it among the terms that genuinely do not discriminate.
    it('puts terms with no result last, in their typed order', () => {
        const withFresh: CompareTerm[] = [
            {kind: 'text', text: 'fresh one', enabled: true},
            ...TERMS,
            {kind: 'text', text: 'aaa fresh two', enabled: true},
        ];

        for (const mode of ['delta', 'toward-a', 'toward-b', 'closest-a', 'closest-b'] as CompareSortMode[]) {
            expect(order(mode, withFresh).slice(-2)).toEqual(['fresh one', 'aaa fresh two']);
        }
    });

    // Alphabetical says nothing about results, so an unrun term belongs in its alphabetical place
    // rather than exiled to the bottom.
    it('does not exile unrun terms when sorting alphabetically', () => {
        const withFresh: CompareTerm[] = [...TERMS, {kind: 'text', text: 'aaa fresh', enabled: true}];
        expect(order('label', withFresh)[0]).toBe('aaa fresh');
    });

    it('leaves the input untouched', () => {
        const terms = [...TERMS];
        orderTerms(terms, index, 'delta');
        expect(terms.map(compareTermLabel)).toEqual(TERMS.map(compareTermLabel));
    });

    it('is the typed order when nothing has been run', () => {
        expect(orderTerms(TERMS, new Map(), 'delta').map(compareTermLabel)).toEqual([
            'blonde hair',
            'night',
            'record 55',
        ]);
    });
});
