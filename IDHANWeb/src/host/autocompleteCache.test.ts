import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { AutocompleteResult } from '../api/types';

// Mock the REST client so we can count network calls and assert the prefix-extension reuse.
const autocompleteTags = vi.fn<
  (prefix: string, opts?: { domain?: number; limit?: number }) => Promise<AutocompleteResult[]>
>();

vi.mock('../api/client', () => ({
  api: { autocompleteTags: (prefix: string, opts?: { domain?: number; limit?: number }) => autocompleteTags(prefix, opts) },
}));

const { autocomplete, clearAutocompleteCache } = await import('./autocompleteCache');

const row = (text: string, id: number): AutocompleteResult => ({ tag_id: id, text });

beforeEach(() => {
  autocompleteTags.mockReset();
  clearAutocompleteCache();
});

describe('autocomplete cache', () => {
  it('enforces a 2-char minimum without hitting the network', async () => {
    const result = await autocomplete('s');
    expect(result).toEqual([]);
    expect(autocompleteTags).not.toHaveBeenCalled();
  });

  it('serves an exact repeat from cache', async () => {
    autocompleteTags.mockResolvedValue([row('samus', 1), row('sample', 2)]);
    await autocomplete('sam', { limit: 100 });
    expect(autocompleteTags).toHaveBeenCalledTimes(1);

    await autocomplete('sam', { limit: 100 });
    expect(autocompleteTags).toHaveBeenCalledTimes(1);
  });

  it('reuses a shorter complete result set by substring filter, with no second request', async () => {
    // "sam" returns fewer than limit → complete, so "samu" is a client-side subset.
    autocompleteTags.mockResolvedValue([row('samus', 1), row('sample', 2), row('tsamu', 3)]);
    await autocomplete('sam', { limit: 100 });
    expect(autocompleteTags).toHaveBeenCalledTimes(1);

    const extended = await autocomplete('samu', { limit: 100 });
    expect(autocompleteTags).toHaveBeenCalledTimes(1); // no new call
    expect(extended.map((r) => r.text).sort()).toEqual(['samus', 'tsamu']); // substring 'samu'
  });

  it('does NOT reuse when the shorter query was truncated at the limit', async () => {
    // Exactly `limit` rows → not complete → the extension must re-query.
    autocompleteTags.mockResolvedValue([row('saa', 1), row('sab', 2)]);
    await autocomplete('sa', { limit: 2 });
    expect(autocompleteTags).toHaveBeenCalledTimes(1);

    autocompleteTags.mockResolvedValue([row('sam', 3)]);
    await autocomplete('sam', { limit: 2 });
    expect(autocompleteTags).toHaveBeenCalledTimes(2);
  });
});
