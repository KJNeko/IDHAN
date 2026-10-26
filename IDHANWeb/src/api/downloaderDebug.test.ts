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

it('streams diagnostics directly and catches up late subscribers without REST reads', async () => {
    const {watchDownloaderDebug} = await import('./client');
    let stream!: ReadableStreamDefaultController<Uint8Array>;
    fetchMock.mockImplementation(async () => new Response(new ReadableStream<Uint8Array>({
        start(controller) {
            stream = controller;
        },
    })));
    const send = (name: string, sessions: unknown[]) => stream.enqueue(new TextEncoder().encode(
        `event: ${name}\ntype: downloader.debug\ndata: ${JSON.stringify({sessions, lanes: []})}\n\n`,
    ));
    const first = vi.fn();
    const second = vi.fn();
    const closeFirst = watchDownloaderDebug({onState: first});
    let closeSecond: (() => void) | undefined;
    try {
        send('snapshot', [{id: 7, events: [{sequence: 1}], events_dropped: 0}]);
        await vi.waitFor(() => expect(first).toHaveBeenCalled());
        send('update', [{id: 7, events: [{sequence: 2}], events_dropped: 0}]);
        await vi.waitFor(() => expect(first.mock.lastCall?.[0].sessions[0].events[0].sequence).toBe(2));
        closeSecond = watchDownloaderDebug({onState: second});
        await vi.waitFor(() => expect(second).toHaveBeenCalled());
        expect(second.mock.lastCall?.[0].sessions[0].events.map((event: {
            sequence: number
        }) => event.sequence)).toEqual([1, 2]);
        expect(second.mock.lastCall?.[1]).toBe(true);
        expect(fetchMock).toHaveBeenCalledTimes(1);
        expect(String(fetchMock.mock.calls[0][0])).toBe('/events?types=downloader.debug');
        send('update', []);
        await vi.waitFor(() => expect(second.mock.lastCall?.[0].sessions).toEqual([]));
    } finally {
        closeFirst();
        closeSecond?.();
    }
});
