import {describe, expect, it} from 'vitest';
import type {
    DownloadSessionUrlFlat,
    DownloadSessionUrlJob,
    DownloadSessionUrlNode,
    DownloadSessionUrlState
} from '../../api/types';
import {
    displayState,
    findDownloadNode,
    flatDisplayState,
    parseNewlineUrlList,
    sessionRecords,
    stateLabel,
    statusLabel,
    subtreeRecords,
    toggleStatus,
} from './DownloadsPanel';

function node(
    id: number,
    recordId: number | null,
    children: DownloadSessionUrlNode[] = [],
    state: DownloadSessionUrlState = 'completed',
): DownloadSessionUrlNode {
    return {
        id,
        parent_id: null,
        url: `https://example.test/${id}`,
        state,
        created_at: 0,
        finished_at: 0,
        error: null,
        note: null,
        record_id: recordId,
        children,
    };
}

describe('download tree projection', () => {
    const selected = node(2, 20, [node(3, null, [node(4, 40)]), node(5, 50), node(6, 20)]);
    const tree = [node(1, 10), selected, node(7, 40)];

    it('finds a selected node and projects unique records in tree order', () => {
        const found = findDownloadNode(tree, 2);

        expect(found).toBe(selected);
        expect(subtreeRecords(found!)).toEqual([20, 40, 50]);
    });

    it('does not find a node from an unrelated session update', () => {
        expect(findDownloadNode(tree, 99)).toBeUndefined();
    });

    it('projects unique records from every root in a session', () => {
        expect(sessionRecords(tree)).toEqual([10, 20, 40, 50]);
    });
});

describe('parseNewlineUrlList', () => {
    it('keeps a single URL', () => {
        expect(parseNewlineUrlList('https://example.test/one')).toEqual(['https://example.test/one']);
    });

    it('splits mixed LF and CRLF clipboard content', () => {
        expect(parseNewlineUrlList('https://example.test/one\r\nhttps://example.test/two\nhttps://example.test/three')).toEqual([
            'https://example.test/one',
            'https://example.test/two',
            'https://example.test/three',
        ]);
    });

    it('trims lines, discards blanks, and preserves order and duplicates', () => {
        expect(parseNewlineUrlList('  https://example.test/one  \n \t\r\nhttps://example.test/two\nhttps://example.test/one\n')).toEqual([
            'https://example.test/one',
            'https://example.test/two',
            'https://example.test/one',
        ]);
    });
});

describe('display state', () => {
    it('leaves a completed URL alone when its whole subtree is terminal', () => {
        const settled = node(1, 10, [node(2, 20), node(3, 30, [node(4, 40, [], 'skipped')], 'failed')]);

        expect(displayState(settled)).toBe('completed');
    });

    it('reads as pending children while a direct child is still working', () => {
        const working = node(1, 10, [node(2, null, [], 'processing')]);

        expect(displayState(working)).toBe('pending-children');
    });

    it('reads as pending children while any depth below is still working', () => {
        const deep = node(1, 10, [node(2, 20, [node(3, 30, [node(4, null, [], 'pending')])])]);

        expect(displayState(deep)).toBe('pending-children');
    });

    it('keeps a failed URL failed so the failure is not hidden by its children', () => {
        const failed = node(1, null, [node(2, null, [], 'processing')], 'failed');

        expect(displayState(failed)).toBe('failed');
    });

    it('leaves a URL that has not completed at its own state', () => {
        const running = node(1, null, [node(2, null, [], 'processing')], 'processing');

        expect(displayState(running)).toBe('processing');
    });

    it('applies the same rule to a flattened entry', () => {
        const job = (id: number, state: DownloadSessionUrlState): DownloadSessionUrlJob => ({
            id,
            parent_id: 1,
            url: `https://example.test/${id}`,
            state,
            created_at: 0,
            finished_at: null,
            error: null,
            note: null,
            record_id: null,
        });
        const entry = (...states: DownloadSessionUrlState[]): DownloadSessionUrlFlat => ({
            ...job(1, 'completed'),
            urls: states.map((state, index) => job(index + 2, state)),
            record_ids: [],
        });

        expect(flatDisplayState(entry('completed', 'skipped'))).toBe('completed');
        expect(flatDisplayState(entry('completed', 'pending'))).toBe('pending-children');
    });

    it('labels the derived state as words, leaving stored states untouched', () => {
        expect(stateLabel('pending-children')).toBe('pending children');
        expect(stateLabel('completed')).toBe('completed');
    });
});

describe('error log status filter', () => {
    it('names a status by its code and a missing one as no response', () => {
        expect(statusLabel(404)).toBe('404');
        expect(statusLabel(null)).toBe('no response');
    });

    it('adds an unselected status and keeps the selection sorted', () => {
        expect(toggleStatus([500, 404], 403)).toEqual([403, 404, 500]);
    });

    it('removes a status that was already selected', () => {
        expect(toggleStatus([403, 404, 500], 404)).toEqual([403, 500]);
    });

    it('sorts the responseless requests after every code and toggles them like any other', () => {
        expect(toggleStatus([404], null)).toEqual([404, null]);
        expect(toggleStatus([404, null], null)).toEqual([404]);
    });
});
