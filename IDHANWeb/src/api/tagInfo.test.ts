import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {tags} from './client';

const fetchMock = vi.fn<typeof fetch>();
const response = {
    tag_id: 42,
    namespace: {id: 1, text: 'character'},
    subtag: {text: 'samus'},
    storage_count: 2,
    display_count: 3,
    domain_counts: [
        {tag_domain_id: 2, storage_count: 0, display_count: 3},
        {tag_domain_id: 3, storage_count: 2, display_count: 2},
    ],
};

beforeEach(() => {
    fetchMock.mockReset();
    fetchMock.mockImplementation(async () => new Response(JSON.stringify(response), {status: 200}));
    vi.stubGlobal('fetch', fetchMock);
});
afterEach(() => vi.unstubAllGlobals());

describe('tag information counts', () => {
    it('sends repeated domain selection and preserves both count modes and cancellation', async () => {
        const controller = new AbortController();
        const info = await tags.info(42, {domains: [2, 3], signal: controller.signal});
        const [input, options] = fetchMock.mock.calls.at(-1)!;
        const url = new URL(String(input), 'http://localhost');
        expect(url.pathname).toBe('/tags/42/info');
        expect(url.searchParams.getAll('tag_domains')).toEqual(['2', '3']);
        expect(options?.signal).toBe(controller.signal);
        expect(info).toEqual(response);
        expect(info).not.toHaveProperty('items_count');
        expect(info).not.toHaveProperty('item_count');
    });

    it('uses all domains when omitted or empty', async () => {
        await tags.info(42);
        expect(new URL(String(fetchMock.mock.calls.at(-1)![0]), 'http://localhost').searchParams.has('tag_domains')).toBe(false);
        await tags.info(42, {domains: []});
        expect(new URL(String(fetchMock.mock.calls.at(-1)![0]), 'http://localhost').searchParams.has('tag_domains')).toBe(false);
    });
});
