/**
 * Shared metadata cache and request coalescer behind host.records.getMetadata.
 *
 * Concurrent requests from different panels within a tick are merged into one batched POST, and
 * results are held in an LRU so a re-scroll over the same records issues no network at all.
 */

import { api } from '../api/client';
import type { MetadataResponse } from '../api/types';
import type { RecordId } from './types';

type Meta = MetadataResponse['records'][number];

/** Ids per POST; the server caps at 1000, so chunk larger fan-outs. */
const MAX_BATCH = 1000;
/** Cached metadata objects to retain, kept well above the visible window. */
const CACHE_LIMIT = 20_000;
/** Remember server-confirmed missing ids to avoid re-requesting holes every scroll. */
const MISSING_LIMIT = 20_000;

// Map iteration order is insertion order, so re-inserting on hit gives a simple LRU.
const cache = new Map<RecordId, Meta>();
const missingIds = new Set<RecordId>();

function remember(id: RecordId, meta: Meta): void {
  cache.delete(id);
  cache.set(id, meta);
  if (cache.size > CACHE_LIMIT) {
    const oldest = cache.keys().next().value;
    if (oldest !== undefined) cache.delete(oldest);
  }
}

function rememberMissing(id: RecordId): void {
  missingIds.add(id);
  if (missingIds.size > MISSING_LIMIT) {
    const oldest = missingIds.values().next().value;
    if (oldest !== undefined) missingIds.delete(oldest);
  }
}

interface Batch {
  ids: Set<RecordId>;
  promise: Promise<void>;
  resolve: () => void;
}

let currentBatch: Batch | null = null;

function openBatch(): Batch {
  if (currentBatch) return currentBatch;
  let resolve!: () => void;
  const promise = new Promise<void>((r) => {
    resolve = r;
  });
  const batch: Batch = { ids: new Set(), promise, resolve };
  currentBatch = batch;
  // Microtask flush coalesces the synchronous burst of requests a scroll settle produces.
  queueMicrotask(() => void flush(batch));
  return batch;
}

async function flush(batch: Batch): Promise<void> {
  if (currentBatch === batch) currentBatch = null;
  const ids = [...batch.ids];
  for (let i = 0; i < ids.length; i += MAX_BATCH) {
    const chunk = ids.slice(i, i + MAX_BATCH);
    try {
      const res = await api.recordsMetadata({ record_ids: chunk });
      for (const record of res.records) remember(record.record_id, record);
      for (const id of res.missing) rememberMissing(id);
    } catch {
      // Transient failure: leave these uncached so a later call retries. Callers just see them missing.
    }
  }
  batch.resolve();
}

const isDefaultInclude = (include?: string[]): boolean =>
  include === undefined || (include.length === 1 && include[0] === 'basic');

/** Synchronously reads an already-cached record, or undefined. */
export function peekMetadata(id: RecordId): Meta | undefined {
  return cache.get(id);
}

export async function getMetadata(ids: readonly RecordId[], include?: string[]): Promise<MetadataResponse> {
  // A non-default include has a different shape, so it bypasses the shared cache and coalescer.
  if (!isDefaultInclude(include)) {
    return api.recordsMetadata({ record_ids: [...ids], include });
  }

  const toFetch = ids.filter((id) => !cache.has(id) && !missingIds.has(id));
  if (toFetch.length > 0) {
    const batch = openBatch();
    for (const id of toFetch) batch.ids.add(id);
    await batch.promise;
  }

  const records: Meta[] = [];
  const missing: RecordId[] = [];
  for (const id of ids) {
    const meta = cache.get(id);
    if (meta) records.push(meta);
    else missing.push(id);
  }
  return { records, missing };
}
