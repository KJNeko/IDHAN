import {describe, expect, it} from 'vitest';
import {buildSortJobs, sortRowsBySlowest, type SortProfileRow} from './DebugPanel';

describe('buildSortJobs', () => {
    it('produces one job per sort option x direction, desc before asc', () => {
        const jobs = buildSortJobs([
            {value: 'mime', label: 'Filetype'},
            {value: 'hash', label: 'Hash'},
        ]);
        expect(jobs).toEqual([
            {value: 'mime', label: 'Filetype', order: 'desc'},
            {value: 'mime', label: 'Filetype', order: 'asc'},
            {value: 'hash', label: 'Hash', order: 'desc'},
            {value: 'hash', label: 'Hash', order: 'asc'},
        ]);
    });

    it('defaults to every registered sort option', () => {
        const jobs = buildSortJobs();
        expect(jobs.length).toBeGreaterThan(0);
        expect(jobs.length % 2).toBe(0); // desc + asc per option
    });
});

describe('sortRowsBySlowest', () => {
    const row = (value: string, queryMs: number): SortProfileRow => ({
        value: value as SortProfileRow['value'],
        label: value,
        order: 'desc',
        queryMs,
        roundTripMs: queryMs + 5,
        bytes: 100,
        count: 10,
        truncated: false,
    });

    it('orders rows by queryMs descending', () => {
        const rows = [row('fast', 5), row('slow', 500), row('mid', 50)];
        expect(sortRowsBySlowest(rows).map((r) => r.value)).toEqual(['slow', 'mid', 'fast']);
    });

    it('does not mutate the input array', () => {
        const rows = [row('a', 1), row('b', 2)];
        const original = [...rows];
        sortRowsBySlowest(rows);
        expect(rows).toEqual(original);
    });

    it('returns an empty array for no rows', () => {
        expect(sortRowsBySlowest([])).toEqual([]);
    });
});
