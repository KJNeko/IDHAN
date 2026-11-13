import {afterEach, expect, it, vi} from 'vitest';
import {watchJobs} from './client';

const cleanups: (() => void)[] = [];
afterEach(() => {
    for (const close of cleanups.splice(0)) close();
    vi.unstubAllGlobals();
});

it('shares one stream, replays current progress to late subscribers, and handles completion and removal', async () => {
    let controller!: ReadableStreamDefaultController<Uint8Array>;
    const body = new ReadableStream<Uint8Array>({
        start(value) {
            controller = value;
        }
    });
    const fetch = vi.fn().mockResolvedValue(new Response(body, {headers: {'Content-Type': 'text/event-stream'}}));
    vi.stubGlobal('fetch', fetch);
    const send = (data: unknown) => controller.enqueue(new TextEncoder().encode(
        `event: job\ndata: ${JSON.stringify(data)}\n\n`,
    ));
    const first = vi.fn();
    const closeFirst = watchJobs({onJobs: first});
    cleanups.push(closeFirst);
    send({type: 'snapshot', jobs: [{job_id: 1, status: 'running', completed: false}]});
    await vi.waitFor(() => expect(first).toHaveBeenCalled());
    send({type: 'status', job_id: 1, status: 'running', completed: false, progress: {phase: 'storing_metadata'}});
    await vi.waitFor(() => expect(first.mock.lastCall?.[0][0].progress.phase).toBe('storing_metadata'));
    const second = vi.fn();
    const closeSecond = watchJobs({onJobs: second});
    cleanups.push(closeSecond);
    await vi.waitFor(() => expect(second.mock.lastCall?.[0][0].progress.phase).toBe('storing_metadata'));
    expect(fetch).toHaveBeenCalledTimes(1);
    expect(fetch.mock.calls[0][0]).toBe('/events?filter=job');
    send({type: 'status', job_id: 1, status: 'completed', completed: true, response: {scanned_count: 3}});
    await vi.waitFor(() => expect(second.mock.lastCall?.[0][0].response.scanned_count).toBe(3));
	send({type: 'response', job_id: 1, response: {kind: 'subscription', imported_count: 7}});
	await vi.waitFor(() => expect(second.mock.lastCall?.[0][0].response.imported_count).toBe(7));
	expect(second.mock.lastCall?.[0][0].status).toBe('completed');
    closeFirst();
    send({type: 'status', job_id: 1, removed: true});
    await vi.waitFor(() => expect(second.mock.lastCall?.[0]).toEqual([]));
    closeSecond();
    expect(fetch.mock.calls[0][1].signal.aborted).toBe(true);
});

it('reconnects with a fresh snapshot that drops jobs from the previous server state', async () => {
    let controller!: ReadableStreamDefaultController<Uint8Array>;
    const fetch = vi.fn().mockImplementation(() => new Response(new ReadableStream<Uint8Array>({
        start(value) {
            controller = value;
        },
    })));
    vi.stubGlobal('fetch', fetch);
    const onJobs = vi.fn();
    cleanups.push(watchJobs({onJobs}));
    const send = (jobs: unknown[]) => controller.enqueue(new TextEncoder().encode(
        `event: job\ndata: ${JSON.stringify({type: 'snapshot', jobs})}\n\n`,
    ));
    send([{job_id: 9, status: 'running', completed: false}]);
    await vi.waitFor(() => expect(onJobs.mock.lastCall?.[0][0].job_id).toBe(9));
    controller.close();
    await vi.waitFor(() => expect(fetch).toHaveBeenCalledTimes(2), {timeout: 2000});
    send([]);
    await vi.waitFor(() => expect(onJobs.mock.lastCall?.[0]).toEqual([]));
});
