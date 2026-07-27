import { beforeEach, describe, expect, it, vi } from 'vitest';

// Mock the REST client so we can count network calls and assert coalescing.
const recordsMetadata = vi.fn(async ({ record_ids }: { record_ids: number[] }) => ({
  records: record_ids.map((id) => ({ record_id: id })),
  missing: [] as number[],
}));

vi.mock('../api/client', () => ({
  api: { recordsMetadata: (body: { record_ids: number[] }) => recordsMetadata(body) },
}));

const { getMetadata, peekMetadata } = await import('./metadataCache');

beforeEach(() => {
  recordsMetadata.mockClear();
});

describe('metadata coalescing cache', () => {
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
