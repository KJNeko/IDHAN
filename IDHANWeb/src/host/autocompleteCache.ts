/**
 * Shared tag-autocomplete cache behind host.tags.autocomplete.
 *
 * Two things make typing feel instant, and both live here so every panel gets them free:
 *
 *  - **Result cache** keyed by `needle|domain|limit`, so re-querying a prefix issues no network.
 *  - **Prefix-extension reuse.** The server matches `tag_text LIKE '%needle%'` (substring, verified in
 *    autocompleteTag.cpp). Any tag containing `needle` also contains every substring of `needle`, so if
 *    a shorter query returned its *complete* match set (fewer than `limit` rows), the longer query's
 *    matches are exactly that set filtered by substring containment — zero requests. "sam" complete ⇒
 *    "samu" is a client-side filter of it.
 *
 * A 2-char minimum is enforced here too: on a multi-million-tag DB a 1-char needle makes the server
 * compute similarity()+GROUP BY over a huge candidate set before LIMIT (see the plan's autocomplete note).
 */

import { api } from '../api/client';
import type { AutocompleteResult } from '../api/types';

/** Below this the server-side candidate set is too large to be worth querying; the UI shows nothing. */
const MIN_NEEDLE = 2;
const DEFAULT_LIMIT = 100;
/** Bound the cache; the user can type through a lot of prefixes in one session. FIFO is fine here. */
const CACHE_LIMIT = 500;

interface CacheEntry {
  results: AutocompleteResult[];
  /** The server returned fewer than `limit` rows, so this is the complete match set for its needle. */
  complete: boolean;
}

const cache = new Map<string, CacheEntry>();

const cacheKey = (needle: string, domain: number | undefined, limit: number): string =>
  `${domain ?? ''}|${limit}|${needle}`;

function store(key: string, entry: CacheEntry): void {
  cache.set(key, entry);
  if (cache.size > CACHE_LIMIT) {
    const oldest = cache.keys().next().value;
    if (oldest !== undefined) cache.delete(oldest);
  }
}

/** A shorter, complete cached query whose result set is a superset of `needle`'s (substring semantics). */
function reusableSuperset(needle: string, domain: number | undefined, limit: number): CacheEntry | undefined {
  for (let len = needle.length - 1; len >= MIN_NEEDLE; len--) {
    const entry = cache.get(cacheKey(needle.slice(0, len), domain, limit));
    if (entry?.complete) return entry;
  }
  return undefined;
}

export async function autocomplete(
  prefix: string,
  opts?: { domain?: number; limit?: number },
  signal?: AbortSignal,
): Promise<AutocompleteResult[]> {
  const needle = prefix.trim().toLowerCase();
  if (needle.length < MIN_NEEDLE) return [];

  const limit = opts?.limit ?? DEFAULT_LIMIT;
  const domain = opts?.domain;
  const key = cacheKey(needle, domain, limit);

  const exact = cache.get(key);
  if (exact) return exact.results;

  const superset = reusableSuperset(needle, domain, limit);
  if (superset) {
    const filtered = superset.results.filter((r) => r.text.toLowerCase().includes(needle));
    store(key, { results: filtered, complete: true });
    return filtered;
  }

  const results = await api.autocompleteTags(needle, { domain, limit }, signal);
  store(key, { results, complete: results.length < limit });
  return results;
}

/** Test/maintenance hook: drop all cached suggestions. */
export function clearAutocompleteCache(): void {
  cache.clear();
}
