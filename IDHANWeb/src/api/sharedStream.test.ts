import {afterEach, describe, expect, it, vi} from 'vitest';
import type {DownloadBandwidth, DownloadSessionInfo, DownloadSessionUrlNode} from './types';
import {watchDownloadSessions} from './client';

function frame(payload: unknown): string {
    return `event: downloader\ndata: ${JSON.stringify(payload)}\n\n`;
}

const SESSION_SNAPSHOT = {
    type: 'session_snapshot', session_id: 7, name: 'gelbooru', created_at: 1, last_used_at: 2, state: 'active',
};

type Stream = { calls: string[]; send: (payload: unknown) => void };

/** Serves every /events request from a stream the test writes to; anything else answers 404. */
function install(initial: unknown[] = [SESSION_SNAPSHOT, {type: 'snapshot_complete'}]): Stream {
    const calls: string[] = [];
    let controller: ReadableStreamDefaultController<Uint8Array> | undefined;
    vi.stubGlobal('fetch', (input: string) => {
        const url = String(input);
        calls.push(url);
        if (!url.startsWith('/events')) return Promise.resolve(new Response('', {status: 404}));
        return Promise.resolve(new Response(new ReadableStream<Uint8Array>({
            start(value) {
                controller = value;
                for (const payload of initial) value.enqueue(new TextEncoder().encode(frame(payload)));
            },
        }), {status: 200, headers: {'Content-Type': 'text/event-stream'}}));
    });
    return {calls, send: (payload) => controller!.enqueue(new TextEncoder().encode(frame(payload)))};
}

const settle = () => new Promise((resolve) => setTimeout(resolve, 20));

it('delivers bandwidth independently of URL updates and preserves the global rate for filtered listeners', async () => {
	const stream = install();
	const sessions = vi.fn();
	const bandwidth: DownloadBandwidth[] = [];
	const close = watchDownloadSessions({
		onSession: sessions,
		onBandwidth: value => bandwidth.push(value)
	}, {sessionIds: [7]});
	let closeLate: (() => void) | undefined;
	try {
		await settle();
		const sessionCalls = sessions.mock.calls.length;
		stream.send({
			type: 'bandwidth', download_bytes_per_second: 5000, sessions: [
				{session_id: 7, download_bytes_per_second: 2000},
				{session_id: 8, download_bytes_per_second: 3000},
			]
		});
		await settle();
		expect(bandwidth).toEqual([{
			download_bytes_per_second: 5000,
			sessions: [{session_id: 7, download_bytes_per_second: 2000}]
		}]);
		expect(sessions).toHaveBeenCalledTimes(sessionCalls);
		stream.send({
			type: 'bandwidth',
			download_bytes_per_second: 0,
			sessions: [{session_id: 7, download_bytes_per_second: 0}]
		});
		await settle();
		expect(bandwidth.at(-1)?.download_bytes_per_second).toBe(0);
		const late = vi.fn();
		closeLate = watchDownloadSessions({onSession: () => undefined, onBandwidth: late}, {sessionIds: [7]});
		await settle();
		expect(late).toHaveBeenCalledWith(bandwidth.at(-1));
		stream.send({type: 'bandwidth', download_bytes_per_second: -1, sessions: []});
		await settle();
		expect(bandwidth).toHaveLength(2);
	} finally {
		closeLate?.();
		close();
	}
});

afterEach(() => {
    vi.unstubAllGlobals();
});

describe('download session stream sharing', () => {
    it('opens one stream for two subscribers and catches the late one up from the store', async () => {
        const {calls} = install();
        const first: number[] = [];
        const second: number[] = [];

        const closeFirst = watchDownloadSessions({onSession: (s) => void first.push(s.id)});
        await settle();
        const closeSecond = watchDownloadSessions({onSession: (s) => void second.push(s.id)});
        await settle();

        expect(calls.filter((url) => url.startsWith('/events'))).toEqual(['/events?filter=downloader']);
        expect(first).toEqual([7]);
        expect(second).toEqual([7]);

        closeFirst();
        closeSecond();
    });

    it('keeps the stream open while any subscriber remains', async () => {
        const {calls} = install();
        const closeFirst = watchDownloadSessions({onSession: () => undefined});
        const closeSecond = watchDownloadSessions({onSession: () => undefined});
        await settle();

        closeFirst();
        await settle();

        expect(calls.filter((url) => url.startsWith('/events'))).toHaveLength(1);
        closeSecond();
    });
});

it('builds URL trees and counts from snapshot chunks and live events', async () => {
    const stream = install([
        SESSION_SNAPSHOT,
        {
            type: 'urls_snapshot', session_id: 7, urls: [
                {url_id: 1, url: 'https://example.test/post', created_at: 1, state: 'processing'},
                {
                    url_id: 2,
                    parent_id: 1,
                    url: 'https://example.test/a.png',
                    created_at: 1,
                    state: 'downloading',
                    bytes: 10
                },
            ],
        },
        {type: 'snapshot_complete'},
    ]);
    const states: { session: DownloadSessionInfo; urls: DownloadSessionUrlNode[] }[] = [];
    const close = watchDownloadSessions({onSession: (session, urls) => states.push({session, urls})});
    try {
        await settle();
        expect(states.at(-1)?.urls).toMatchObject([
            {id: 1, state: 'processing', children: [{id: 2, state: 'downloading', bytes: 10}]},
        ]);

        stream.send({type: 'url_progress', session_id: 7, url_id: 2, bytes: 2048, total: 4096});
        stream.send({type: 'url_updated', session_id: 7, url_id: 2, state: 'importing'});
        stream.send({
            type: 'url_updated',
            session_id: 7,
            url_id: 2,
            state: 'imported',
            record_id: 99,
            size: 4096,
            finished_at: 3
        });
        stream.send({
            type: 'url_created',
            session_id: 7,
            url_id: 3,
            parent_id: 1,
            url: 'https://example.test/a.png',
            created_at: 3,
            state: 'skipped_already_seen',
            finished_at: 3
        });
        stream.send({
            type: 'url_created',
            session_id: 7,
            url_id: 4,
            parent_id: 1,
            url: 'https://example.test/bad',
            created_at: 3
        });
        stream.send({
            type: 'url_updated',
            session_id: 7,
            url_id: 4,
            state: 'failed',
            stage: 'download',
            error: '404',
            finished_at: 4
        });
        await settle();

        const latest = states.at(-1)!;
        const children = latest.urls[0]!.children;
        expect(children.map(child => [child.id, child.state])).toEqual([
            [2, 'imported'], [3, 'skipped_already_seen'], [4, 'failed'],
        ]);
        expect(children[0]).toMatchObject({record_id: 99, size: 4096, bytes: undefined});
        expect(children[2]).toMatchObject({stage: 'download', error: '404'});
        expect(latest.session.counts).toMatchObject({processing: 1, imported: 1, skipped_already_seen: 1, failed: 1});

        stream.send({type: 'url_removed', session_id: 7, url_id: 1});
        await settle();
        expect(states.at(-1)!.urls).toEqual([]);
    } finally {
        close();
    }
});

it('requests only the subscribed sessions and ignores others', async () => {
    const stream = install([]);
    const seen: number[] = [];
    const close = watchDownloadSessions({onSession: (session) => seen.push(session.id)}, {sessionIds: [7]});
    try {
        await settle();
        expect(stream.calls[0]).toBe('/events?filter=downloader&session_ids=7');
        stream.send(SESSION_SNAPSHOT);
        stream.send({...SESSION_SNAPSHOT, session_id: 8});
		stream.send({type: 'snapshot_complete'});
        await settle();
        expect(seen).toEqual([7]);
    } finally {
        close();
    }
});

it('publishes a large baseline once after all snapshot chunks, including to late subscribers', async () => {
	const stream = install([SESSION_SNAPSHOT]);
	const first = vi.fn();
	const late = vi.fn();
	const close = watchDownloadSessions({onSession: first});
	let closeLate: (() => void) | undefined;
	try {
		await settle();
		for (let chunk = 0; chunk < 3; ++chunk) {
			stream.send({
				type: 'urls_snapshot', session_id: 7,
				urls: Array.from({length: 1000}, (_, index) => ({
					url_id: chunk * 1000 + index + 1,
					url: `https://example.test/${chunk}/${index}`,
					state: 'pending', created_at: 1,
				})),
			});
			await settle();
		}
		closeLate = watchDownloadSessions({onSession: late});
		await settle();
		expect(first).not.toHaveBeenCalled();
		expect(late).not.toHaveBeenCalled();

		// Changes arriving during baseline ingestion must be included in its first publication.
		stream.send({type: 'url_updated', session_id: 7, url_id: 1, state: 'imported', record_id: 99});
		stream.send({type: 'snapshot_complete'});
		await settle();
		for (const listener of [first, late]) {
			expect(listener).toHaveBeenCalledTimes(1);
			const [info, urls] = listener.mock.lastCall!;
			expect(info.counts).toMatchObject({pending: 2999, imported: 1});
			expect(urls).toHaveLength(3000);
			expect(urls.at(-1)).toMatchObject({id: 1, record_id: 99});
		}

		stream.send({type: 'url_progress', session_id: 7, url_id: 2, bytes: 1024});
		await settle();
		expect(first).toHaveBeenCalledTimes(2);
		expect(late).toHaveBeenCalledTimes(2);
	} finally {
		closeLate?.();
		close();
	}
});

it('counts request errors and drops cleared ones', async () => {
    const stream = install();
    const counts: number[] = [];
    const close = watchDownloadSessions({onSession: (session) => counts.push(session.error_count)});
    try {
        await settle();
        stream.send({
            type: 'request_error',
            session_id: 7,
            error_id: 5,
            url: 'https://example.test',
            lane: 'example.test',
            http_status: 503
        });
        stream.send({
            type: 'request_error',
            session_id: 7,
            error_id: 6,
            url: 'https://example.test',
            lane: 'example.test'
        });
        await settle();
        expect(counts.at(-1)).toBe(2);
        stream.send({type: 'request_errors_cleared', session_id: 7, error_ids: [5]});
        await settle();
        expect(counts.at(-1)).toBe(1);
    } finally {
        close();
    }
});

it('removes deleted sessions', async () => {
    const stream = install();
    const removed: number[] = [];
    const close = watchDownloadSessions({onSession: () => undefined, onRemoved: (id) => removed.push(id)});
    try {
        await settle();
        stream.send({type: 'session_deleted', session_id: 7});
        await settle();
        expect(removed).toEqual([7]);
    } finally {
        close();
    }
});

it('keeps subscription-owned sessions out of the normal download store', async () => {
	const stream = install([SESSION_SNAPSHOT,
		{...SESSION_SNAPSHOT, session_id: 8, is_subscription: true}, {type: 'snapshot_complete'}]);
	const ids: number[] = [];
	const close = watchDownloadSessions({
		onSession: session => {
			ids.push(session.id);
		}
	});
	try {
		await settle();
		stream.send({...SESSION_SNAPSHOT, type: 'session_created', session_id: 9, is_subscription: true});
		stream.send({type: 'session_state', session_id: 9, state: 'active'});
		await settle();
		expect(ids).toEqual([7]);
	} finally {
		close();
	}
});
