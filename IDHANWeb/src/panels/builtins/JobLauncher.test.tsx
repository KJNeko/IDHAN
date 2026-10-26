import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import {api} from '../../api/client';
import type {PanelProps} from '../../host/types';
import {JobLauncher} from './JobLauncher';

vi.mock('../../api/client', () => ({api: {reorganizeUrls: vi.fn(), rescanMetadata: vi.fn()}}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('starts scoped metadata, cluster verification, and available embedding backfills', async () => {
    const fetch = vi.fn(async (path: string) => {
        if (path === '/clusters/list') return Response.json([{cluster_id: 7, name: 'Archive'}]);
        if (path === '/embeddings/models') return Response.json([
            {model_name: 'offline', available: false}, {model_name: 'image-model', available: true},
        ]);
        return Response.json({job_id: 22});
    });
    const host = {http: {fetch}} as unknown as PanelProps['host'];
    const onQueued = vi.fn();
    const view = document.createElement('div');
    const root = createRoot(view);

    async function choose(value: string) {
        await act(async () => {
            const select = view.querySelector('select')!;
            select.value = value;
            select.dispatchEvent(new Event('change', {bubbles: true}));
        });
    }

    async function submit() {
        await act(() => view.querySelector('form')!.dispatchEvent(new Event('submit', {
            bubbles: true,
            cancelable: true
        })));
    }

    try {
        await act(() => root.render(<JobLauncher host={host} jobs={[]} onQueued={onQueued}/>));
        expect(fetch).not.toHaveBeenCalled();
        await choose('metadata');
        await act(() => {
            const input = view.querySelector('input')!;
            Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, '12, 34 12');
            input.dispatchEvent(new Event('input', {bubbles: true}));
        });
        vi.mocked(api.rescanMetadata).mockResolvedValue({job_id: 20, status: 'dispatched'});
        await submit();
        expect(api.rescanMetadata).toHaveBeenCalledWith([12, 34]);
        expect(onQueued).toHaveBeenCalledWith(expect.objectContaining({
            job_id: 20,
            job_name: 'Rescanning record metadata'
        }));
        await choose('cluster');
        expect(view.textContent).toContain('Archive');
        await act(() => view.querySelector<HTMLInputElement>('input[type=checkbox]')!.click());
        await submit();
        expect(fetch).toHaveBeenCalledWith('/clusters/7/scan?verify_hash=true', {method: 'POST'});
        await choose('embeddings');
        const offline = view.querySelector<HTMLOptionElement>('option[value=offline]')!;
        expect(offline.disabled).toBe(true);
        await submit();
        expect(fetch).toHaveBeenCalledWith('/embeddings/generate', expect.objectContaining({
            method: 'POST', body: JSON.stringify({model_name: 'image-model'}),
        }));
    } finally {
        await act(() => root.unmount());
    }
});

it('blocks duplicate jobs and reports dispatch failures', async () => {
    const host = {http: {fetch: vi.fn()}} as unknown as PanelProps['host'];
    const onQueued = vi.fn();
    const view = document.createElement('div');
    const root = createRoot(view);
    try {
        await act(() => root.render(<JobLauncher host={host} jobs={[{job_name: 'Reorganizing URLs', status: 'running'}]}
                                                 onQueued={onQueued}/>));
        expect(view.querySelector<HTMLButtonElement>('button[type=submit]')!.disabled).toBe(true);
        await act(() => root.render(<JobLauncher host={host} jobs={[]} onQueued={onQueued}/>));
        vi.mocked(api.reorganizeUrls).mockRejectedValueOnce(new Error('Permission denied'));
        await act(() => view.querySelector('form')!.dispatchEvent(new Event('submit', {
            bubbles: true,
            cancelable: true
        })));
        expect(view.querySelector('[role=alert]')?.textContent).toBe('Permission denied');
        expect(onQueued).not.toHaveBeenCalled();
        expect(view.querySelector<HTMLButtonElement>('button[type=submit]')!.disabled).toBe(false);
    } finally {
        await act(() => root.unmount());
    }
});
