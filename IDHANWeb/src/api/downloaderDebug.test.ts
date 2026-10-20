import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {api} from './client';

const fetchMock = vi.fn<typeof fetch>();

beforeEach(() => {
    fetchMock.mockReset();
    fetchMock.mockImplementation(async () => new Response('{"sessions":[],"lanes":[]}', {status: 200}));
    vi.stubGlobal('fetch', fetchMock);
});

afterEach(() => vi.unstubAllGlobals());

describe('downloader diagnostic event cursors', () => {
    it('sends per-session sequences using event_cursors and forwards cancellation', async () => {
        const controller = new AbortController();
        await api.downloader.debug(new Map([[12, 100], [15, 40]]), controller.signal);
        const [input, options] = fetchMock.mock.calls.at(-1)!;
        const url = new URL(String(input), 'http://localhost');
        expect(url.pathname).toBe('/downloader/debug');
        expect(url.searchParams.getAll('event_cursors')).toEqual(['12:100,15:40']);
        expect(url.searchParams.has('since')).toBe(false);
        expect(options?.signal).toBe(controller.signal);
    });

    it('omits the cursor query when no sequences are available', async () => {
        await api.downloader.debug();
        const [input] = fetchMock.mock.calls.at(-1)!;
        expect(String(input)).toBe('/downloader/debug');
    });

    it('preserves an explicit sequence zero', async () => {
        await api.downloader.debug(new Map([[12, 0]]));
        const [input] = fetchMock.mock.calls.at(-1)!;
        const url = new URL(String(input), 'http://localhost');
        expect(url.searchParams.get('event_cursors')).toBe('12:0');
    });
});
