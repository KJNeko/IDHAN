import { beforeEach, describe, expect, it, vi } from 'vitest';

// Mock the REST client so we can count network calls and assert coalescing.
const recordsMetadata = vi.fn(async ({ record_ids }: { record_ids: number[] }) => ({
  records: record_ids.map((id) => ({ record_id: id })),
  missing: [] as number[],
}));

vi.mock('../api/client', () => ({
  api: { recordsMetadata: (body: { record_ids: number[] }) => recordsMetadata(body) },
}));

const {forgetMetadata, getMetadata, peekMetadata} = await import('./metadataCache');

beforeEach(() => {
  recordsMetadata.mockClear();
});

describe('metadata coalescing cache', () => {
    it('does not let an invalidated in-flight response replace refreshed metadata', async () => {
        const stale = {record_id: 401, tags: [{domain: 'gelbooru', text: 'old'}]};
        const fresh = {record_id: 401, tags: [{domain: 'pixiv', text: 'new'}]};
        let finishOld!: (response: { records: { record_id: number }[]; missing: number[] }) => void;
        recordsMetadata.mockImplementationOnce(() => new Promise((resolve) => {
            finishOld = resolve;
        }));
        const oldRequest = getMetadata([401]);
        await Promise.resolve();

        forgetMetadata([401]);
        recordsMetadata.mockResolvedValueOnce({records: [fresh], missing: []});
        expect((await getMetadata([401])).records).toEqual([fresh]);

        finishOld({records: [stale], missing: []});
        await oldRequest;
        expect(peekMetadata(401)).toEqual(fresh);
        expect((await getMetadata([401])).records).toEqual([fresh]);
        expect(recordsMetadata).toHaveBeenCalledTimes(2);
    });

    it('allows overlapping requests to finish without losing metadata for either caller', async () => {
        let finishFirst!: (response: { records: { record_id: number }[]; missing: number[] }) => void;
        let finishSecond!: (response: { records: { record_id: number }[]; missing: number[] }) => void;
        recordsMetadata.mockImplementationOnce(() => new Promise((resolve) => {
            finishFirst = resolve;
        }));
        recordsMetadata.mockImplementationOnce(() => new Promise((resolve) => {
            finishSecond = resolve;
        }));
        const first = getMetadata([402]);
        await Promise.resolve();
        const second = getMetadata([402]);
        await Promise.resolve();

        finishFirst({records: [{record_id: 402}], missing: []});
        expect((await first).records).toEqual([{record_id: 402}]);
        finishSecond({records: [{record_id: 402}], missing: []});
        expect((await second).records).toEqual([{record_id: 402}]);
    });

  it('merges concurrent requests into one batched call', async () => {
    const [a, b] = await Promise.all([getMetadata([101, 102]), getMetadata([102, 103])]);

    expect(recordsMetadata).toHaveBeenCalledTimes(1);
    const requestedIds = (recordsMetadata.mock.calls[0]?.[0].record_ids ?? []).slice().sort((x, y) => x - y);
    expect(requestedIds).toEqual([101, 102, 103]);

    expect(a.records.map((r) => r.record_id).sort()).toEqual([101, 102]);
    expect(b.records.map((r) => r.record_id).sort()).toEqual([102, 103]);
  });

  it('serves cached records without another network call', async () => {
    await getMetadata([201, 202]);
    expect(recordsMetadata).toHaveBeenCalledTimes(1);

    recordsMetadata.mockClear();
    const again = await getMetadata([201, 202]);
    expect(recordsMetadata).not.toHaveBeenCalled();
    expect(again.records).toHaveLength(2);
    expect(peekMetadata(201)).toBeDefined();
  });

  it('reports server-confirmed missing ids without refetching them', async () => {
    recordsMetadata.mockImplementationOnce(async ({ record_ids }) => ({
      records: record_ids.filter((id) => id !== 999).map((id) => ({ record_id: id })),
      missing: record_ids.includes(999) ? [999] : [],
    }));

    const first = await getMetadata([301, 999]);
    expect(first.missing).toContain(999);

    recordsMetadata.mockClear();
    const second = await getMetadata([999]);
    expect(recordsMetadata).not.toHaveBeenCalled();
    expect(second.missing).toEqual([999]);
  });
});
