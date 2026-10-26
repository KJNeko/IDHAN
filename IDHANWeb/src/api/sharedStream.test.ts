import {afterEach, describe, expect, it, vi} from 'vitest';
import {watchDownloadSessions} from './client';

const SNAPSHOT = 'event: snapshot\ntimestamp: 1758542400\ntype: downloader.session\ndata: {"sessions":[7]}\n\n';

const session = {id: 7, name: 'gelbooru', created_at: 1, last_used_at: 2, error_count: 0};

/** A body that delivers one snapshot frame and then stays open, as the real stream does. */
function eventStream(): Response {
    const body = new ReadableStream<Uint8Array>({
        start(controller) {
            controller.enqueue(new TextEncoder().encode(SNAPSHOT));
        },
    });
    return new Response(body, {status: 200, headers: {'Content-Type': 'text/event-stream'}});
}

function install(): { calls: string[] } {
    const calls: string[] = [];
    vi.stubGlobal('fetch', (input: string) => {
        const url = String(input);
        calls.push(url);
        if (url.includes('/events?types=downloader.session')) return Promise.resolve(eventStream());
        if (url.endsWith('/urls')) return Promise.resolve(Response.json([]));
        return Promise.resolve(Response.json(session));
    });
    return {calls};
}

const settle = () => new Promise((resolve) => setTimeout(resolve, 20));

afterEach(() => {
    vi.unstubAllGlobals();
});

describe('download session stream sharing', () => {
    it('opens one stream for two subscribers and delivers the snapshot to both', async () => {
        const {calls} = install();
        const first: number[] = [];
        const second: number[] = [];

        const closeFirst = watchDownloadSessions({onSession: (s) => void first.push(s.id)});
        await settle();
        const closeSecond = watchDownloadSessions({onSession: (s) => void second.push(s.id)});
        await settle();

        const streams = calls.filter((url) => url.includes('/events?types=downloader.session'));
        expect(streams).toHaveLength(1);

        // The second subscriber joined after the snapshot had already been delivered, so it can only
        // be caught up from the cached one.
        expect(first).toEqual([7]);
        expect(second).toEqual([7]);

        closeFirst();
        closeSecond();
    });

    it('keeps the stream open while any subscriber remains', async () => {
        const {calls} = install();

        const closeFirst = watchDownloadSessions({
            onSession: () => {
            }
        });
        const closeSecond = watchDownloadSessions({
            onSession: () => {
            }
        });
        await settle();

        closeFirst();
        await settle();

        // Dropping one subscriber must not reopen or tear down the shared stream.
        expect(calls.filter((url) => url.includes('/events?types=downloader.session'))).toHaveLength(1);

        closeSecond();
    });
});
