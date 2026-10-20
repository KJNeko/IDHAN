import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {api} from './client';

const fetchMock = vi.fn<typeof fetch>();

beforeEach(() => {
    fetchMock.mockReset();
    fetchMock.mockImplementation(async () => new Response('[]', {status: 200}));
    vi.stubGlobal('fetch', fetchMock);
});

afterEach(() => vi.unstubAllGlobals());

function lastQuery(): URLSearchParams {
    const [path] = fetchMock.mock.calls.at(-1)!;
    return new URL(String(path), 'http://localhost').searchParams;
}

describe('autocomplete request options', () => {
    it('sends repeated domains and the selected count mode and used-only setting', async () => {
        await api.autocompleteTags('scope', {domains: [1, 2], display: 'storage', includeUnused: false});
        const query = lastQuery();
        expect(query.getAll('tag_domains')).toEqual(['1', '2']);
        expect(query.get('tag_display_type')).toBe('storage');
        expect(query.get('include_unused')).toBe('false');
        expect(query.get('tag')).toBe('scope');
    });

    it('adapts single-domain callers to the canonical parameter', async () => {
        await api.autocompleteTags('scope', {domain: 3});
        const query = lastQuery();
        expect(query.getAll('tag_domains')).toEqual(['3']);
        expect(query.has('tag_domain')).toBe(false);
        expect(query.get('tag_display_type')).toBe('display');
        expect(query.get('include_unused')).toBe('true');
    });

    it('uses all domains when omitted or explicitly empty and gives the set precedence', async () => {
        await api.autocompleteTags('scope');
        expect(lastQuery().has('tag_domains')).toBe(false);
        await api.autocompleteTags('scope', {domains: [], domain: 3});
        expect(lastQuery().has('tag_domains')).toBe(false);
        await api.autocompleteTags('scope', {domains: [2], domain: 3});
        expect(lastQuery().getAll('tag_domains')).toEqual(['2']);
    });
});
