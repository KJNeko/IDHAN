import {afterEach, describe, expect, it, vi} from 'vitest';
import type {RecordMetadata} from '../../api/types';
import {describeRecord, RECORD_ACTIONS} from './recordActions';
import type {RecordActionContext} from './recordActions';

const record = (over: Partial<RecordMetadata> = {}): RecordMetadata => ({
    record_id: 12,
    hashes: {sha256: 'abcd'},
    ...over,
});

/** Only the record list decides availability and labels, so the host is never touched. */
const ctx = (ids: number[], target = ids[0]!): RecordActionContext =>
    ({host: undefined, ids, target}) as unknown as RecordActionContext;

const find = (key: string) => RECORD_ACTIONS.find((action) => action.key === key)!;

describe('describeRecord', () => {
    it('carries the id and hash of a record with no metadata yet', () => {
        expect(describeRecord(record())).toBe('record: #12\nsha256: abcd');
    });

    it('omits fields the record does not have', () => {
        const text = describeRecord(record({mime: 'image/png', width: 4, height: 2}));
        expect(text).toContain('mime: image/png');
        expect(text).toContain('dimensions: 4x2');
        expect(text).not.toContain('duration');
    });

    it('reports size in both units', () => {
        expect(describeRecord(record({size: 2048}))).toContain('size: 2.00 KB (2048 bytes)');
    });
});

describe('RECORD_ACTIONS', () => {
    it('offers open and relationships only for a single record', () => {
        expect(find('open').available?.(ctx([1]))).toBe(true);
        expect(find('open').available?.(ctx([1, 2]))).toBe(false);
        expect(find('send-to-relationships').available?.(ctx([1, 2]))).toBe(false);
    });

    it('applies to any number of records elsewhere', () => {
        for (const key of ['regenerate-metadata', 'copy-ids', 'copy-hashes', 'copy-link', 'copy-details'])
            expect(find(key).available).toBeUndefined();
    });

    it('names the one record a reparse would touch, whatever else is selected', () => {
        expect(find('regenerate-metadata').label(ctx([1, 2, 3], 2))).toBe('Regenerate metadata for #2');
    });

    it('pluralises the copies, which do act on the whole selection', () => {
        expect(find('copy-ids').label(ctx([1]))).toBe('Copy id');
        expect(find('copy-ids').label(ctx([1, 2]))).toBe('Copy ids');
    });

    it('keys every action uniquely, so the menu can list them', () => {
        expect(new Set(RECORD_ACTIONS.map((action) => action.key)).size).toBe(RECORD_ACTIONS.length);
    });
});

describe('file actions', () => {
    afterEach(() => vi.unstubAllGlobals());

    it('opens the authenticated original URL in a separate tab', () => {
        const open = vi.fn();
        vi.stubGlobal('window', {open});
        const context = {
            ...ctx([12]),
            host: {records: {fileUrl: () => '/records/12/file?idhan_key=test'}},
        } as unknown as RecordActionContext;
        find('open').run(context);
        expect(open).toHaveBeenCalledWith('/records/12/file?idhan_key=test', '_blank', 'noopener,noreferrer');
    });

    it('copies valid JSON in selection order for multiple records', async () => {
        const writeText = vi.fn().mockResolvedValue(undefined);
        vi.stubGlobal('navigator', {clipboard: {writeText}});
        const context = {
            ...ctx([13, 12]),
            host: {
                records: {getMetadata: async () => ({records: [record(), record({record_id: 13})]})},
                ui: {toast: vi.fn()},
            },
        } as unknown as RecordActionContext;
        await find('copy-json').run(context);
        expect(JSON.parse(writeText.mock.calls[0]![0])).toEqual([record({record_id: 13}), record()]);
    });
});
