/**
 * Resolves tag ids to "namespace:subtag" text, cached and request-coalesced across panels.
 *
 * The verbose active-tags endpoint returns ids, not text, and GET /tags/{id}/info is one id per call,
 * so a record with dozens of tags would otherwise fan out to dozens of requests every time it is
 * focused. This caches each resolution for the session and de-dupes concurrent lookups of the same id.
 */

import { tags as tagApi } from '../api/client';
import type { TagInfo } from '../api/types';

const cache = new Map<number, string>();
const inflight = new Map<number, Promise<string>>();

function format(info: TagInfo): string {
  const ns = info.namespace.text;
  return ns.length > 0 ? `${ns}:${info.subtag.text}` : info.subtag.text;
}

/** Synchronously read an already-resolved tag, or undefined. */
export function peekTag(tagId: number): string | undefined {
  return cache.get(tagId);
}

export async function resolveTag(tagId: number): Promise<string> {
  const cached = cache.get(tagId);
  if (cached !== undefined) return cached;

  const existing = inflight.get(tagId);
  if (existing) return existing;

  const promise = tagApi
    .info(tagId)
    .then((info) => {
      const text = format(info);
      cache.set(tagId, text);
      inflight.delete(tagId);
      return text;
    })
    .catch((error: unknown) => {
      inflight.delete(tagId);
      throw error;
    });
  inflight.set(tagId, promise);
  return promise;
}

/** Resolve many ids at once; failed lookups are simply omitted from the returned map. */
export async function resolveTags(tagIds: readonly number[]): Promise<Map<number, string>> {
  const unique = [...new Set(tagIds)];
  await Promise.all(unique.map((id) => resolveTag(id).catch(() => undefined)));
  const out = new Map<number, string>();
  for (const id of unique) {
    const text = cache.get(id);
    if (text !== undefined) out.set(id, text);
  }
  return out;
}
