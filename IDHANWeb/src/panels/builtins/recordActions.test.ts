import {describe, expect, it} from 'vitest';
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
