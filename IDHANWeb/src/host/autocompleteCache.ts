/**
 * Shared tag-autocomplete cache behind host.tags.autocomplete.
 *
 * Results are cached by needle, domain set, display mode, used-only choice, and limit. The server matches `tag_text LIKE '%needle%'`, so any
 * tag containing `needle` also contains every substring of it: when a shorter query returned its
 * complete match set (fewer than `limit` rows), a longer query is that same set filtered client-side,
 * with no request at all.
 *
 * A 2-char minimum is enforced here. On a multi-million-tag database a 1-char needle makes the server
 * compute similarity() and GROUP BY over a huge candidate set before LIMIT.
 */

import { api } from '../api/client';
import type {AutocompleteOptions, AutocompleteResult} from '../api/types';

/** Below this the server-side candidate set is too large to be worth querying; the UI shows nothing. */
const MIN_NEEDLE = 2;
const DEFAULT_LIMIT = 100;
/** Bounds the cache. FIFO eviction is fine here. */
const CACHE_LIMIT = 500;

interface CacheEntry {
  results: AutocompleteResult[];
  /** The server returned fewer than `limit` rows, so this is the complete match set for its needle. */
  complete: boolean;
}

const cache = new Map<string, CacheEntry>();

interface CacheScope {
    domains: number[];
    display: 'display' | 'storage';
    includeUnused: boolean;
    limit: number;
}

const cacheKey = (needle: string, scope: CacheScope): string =>
    JSON.stringify([scope.domains, scope.display, scope.includeUnused, scope.limit, needle]);

function store(key: string, entry: CacheEntry): void {
  cache.set(key, entry);
  if (cache.size > CACHE_LIMIT) {
    const oldest = cache.keys().next().value;
    if (oldest !== undefined) cache.delete(oldest);
  }
}

/** A shorter, complete cached query whose result set is a superset of `needle`'s (substring semantics). */
function reusableSuperset(needle: string, scope: CacheScope): CacheEntry | undefined {
  for (let len = needle.length - 1; len >= MIN_NEEDLE; len--) {
      const entry = cache.get(cacheKey(needle.slice(0, len), scope));
    if (entry?.complete) return entry;
  }
  return undefined;
}

export async function autocomplete(
  prefix: string,
  opts?: AutocompleteOptions,
  signal?: AbortSignal,
): Promise<AutocompleteResult[]> {
  const needle = prefix.trim().toLowerCase();
  if (needle.length < MIN_NEEDLE) return [];

    const requestedDomains = opts?.domains ?? (opts?.domain !== undefined ? [opts.domain] : []);
    const scope: CacheScope = {
        domains: [...new Set(requestedDomains)].sort((a, b) => a - b),
        display: opts?.display ?? 'display',
        includeUnused: opts?.includeUnused ?? true,
        limit: opts?.limit ?? DEFAULT_LIMIT,
    };
    const key = cacheKey(needle, scope);

  const exact = cache.get(key);
  if (exact) return exact.results;

    const superset = reusableSuperset(needle, scope);
  if (superset) {
    const filtered = superset.results.filter((r) => r.text.toLowerCase().includes(needle));
    store(key, { results: filtered, complete: true });
    return filtered;
  }

    const results = await api.autocompleteTags(needle, scope, signal);
    store(key, {results, complete: results.length < scope.limit});
  return results;
}

/** Test/maintenance hook: drop all cached suggestions. */
export function clearAutocompleteCache(): void {
  cache.clear();
}
