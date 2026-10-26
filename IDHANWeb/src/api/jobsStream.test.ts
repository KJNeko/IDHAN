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
    const send = (event: string, data: unknown, type = 'job.status') => controller.enqueue(new TextEncoder().encode(
        `event: ${event}\ntype: ${type}\ndata: ${JSON.stringify(data)}\n\n`,
    ));
    const first = vi.fn();
    const closeFirst = watchJobs({onJobs: first});
    cleanups.push(closeFirst);
    send('snapshot', {jobs: [{job_id: 1, status: 'running', completed: false}]});
    send('snapshot', {}, 'stream.snapshot.complete');
    await vi.waitFor(() => expect(first).toHaveBeenCalled());
    send('update', {job_id: 1, status: 'running', completed: false, progress: {phase: 'storing_metadata'}});
    await vi.waitFor(() => expect(first.mock.lastCall?.[0][0].progress.phase).toBe('storing_metadata'));
    const second = vi.fn();
    const closeSecond = watchJobs({onJobs: second});
    cleanups.push(closeSecond);
    await vi.waitFor(() => expect(second.mock.lastCall?.[0][0].progress.phase).toBe('storing_metadata'));
    expect(fetch).toHaveBeenCalledTimes(1);
    expect(fetch.mock.calls[0][0]).toBe('/events?types=job.status');
    send('update', {job_id: 1, status: 'completed', completed: true, response: {scanned_count: 3}});
    await vi.waitFor(() => expect(second.mock.lastCall?.[0][0].response.scanned_count).toBe(3));
    closeFirst();
    send('update', {job_id: 1, removed: true});
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
        `event: snapshot\ntype: job.status\ndata: ${JSON.stringify({jobs})}\n\n`,
    ));
    const complete = () => controller.enqueue(new TextEncoder().encode(
        'event: snapshot\ntype: stream.snapshot.complete\ndata: {}\n\n',
    ));
    send([{job_id: 9, status: 'running', completed: false}]);
    complete();
    await vi.waitFor(() => expect(onJobs.mock.lastCall?.[0][0].job_id).toBe(9));
    controller.close();
    await vi.waitFor(() => expect(fetch).toHaveBeenCalledTimes(2), {timeout: 2000});
    send([]);
    complete();
    await vi.waitFor(() => expect(onJobs.mock.lastCall?.[0]).toEqual([]));
});
