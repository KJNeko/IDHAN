import {afterEach, beforeEach, expect, it, vi} from 'vitest';
import {ApiError, api, readApiError, setKey} from './client';

const fetchMock = vi.fn<typeof fetch>();

beforeEach(() => {
    fetchMock.mockReset();
    setKey(null);
    vi.stubGlobal('fetch', fetchMock);
});
afterEach(() => vi.unstubAllGlobals());

it.each(['new session', 'existing session'])('preserves the plain-text URL rejection for %s', async (target) => {
    const reason = 'Unable to schedule URL: No URL class accepts URL: https://example.com/unmatched';
    fetchMock.mockResolvedValue(new Response(reason, {status: 400, headers: {'Content-Type': 'text/plain'}}));
    const submission = target === 'new session'
        ? api.downloadSessions.submitUrlSession('https://example.com/unmatched')
        : api.downloadSessions.submitUrl(3, 'https://example.com/unmatched');
    await expect(submission).rejects.toMatchObject({name: 'ApiError', status: 400, message: reason, body: reason});
});

it('keeps JSON error messages and their structured body', async () => {
    const body = {error: 'URL must be a string', field: 'url'};
    fetchMock.mockResolvedValue(Response.json(body, {status: 400}));
    await expect(api.downloadSessions.submitUrlSession('bad')).rejects.toMatchObject({
        status: 400, message: body.error, body,
    });
});

it.each(['', ' \n\t ', 'null', '{}'])('falls back to request and status when the body has no reason: %j', async (body) => {
    fetchMock.mockResolvedValue(new Response(body, {status: 400}));
    await expect(api.downloadSessions.submitUrlSession('bad')).rejects.toMatchObject({
        status: 400, message: 'POST /download_sessions/urls → 400',
    });
});

it('shares response error decoding with panels using host fetch', async () => {
    const error = await readApiError(new Response('  URL is too long\n', {status: 400}), 'POST /records/3/urls/add');
    expect(error).toBeInstanceOf(ApiError);
    expect(error.message).toBe('URL is too long');
    expect(error.body).toBe('  URL is too long\n');
});

it('keeps successful JSON and empty responses unchanged', async () => {
    const body = {id: 3, url: 'https://example.com/image', state: 'pending'};
    fetchMock.mockResolvedValueOnce(Response.json(body, {status: 201}));
    await expect(api.downloadSessions.submitUrl(1, body.url)).resolves.toEqual(body);
    fetchMock.mockResolvedValueOnce(new Response(null, {status: 204}));
    await expect(api.downloadSessions.submitUrl(1, body.url)).resolves.toBeUndefined();
});
