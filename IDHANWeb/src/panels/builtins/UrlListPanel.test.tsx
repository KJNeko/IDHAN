import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import type {PanelProps} from '../../host/types';
import {urlListPanel} from './UrlListPanel';

Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('shows the server rejection and retains the URL until a retry succeeds', async () => {
    const url = 'https://example.com/image';
    const fetch = vi.fn().mockResolvedValueOnce(Response.json([]))
        .mockResolvedValueOnce(new Response('URL could not be added', {status: 400}))
        .mockResolvedValueOnce(Response.json({}))
        .mockResolvedValueOnce(Response.json([url]));
    const toast = vi.fn();
    const host = {
        selection: {
            get: () => [3], subscribe: () => () => {
            }
        }, http: {fetch}, ui: {toast}
    } as unknown as PanelProps['host'];
    const view = document.createElement('div');
    const root = createRoot(view);
    const Panel = urlListPanel.component;
    try {
        await act(async () => {
            root.render(<Panel host={host}/>);
        });
        const input = view.querySelector('input')!;
        await act(() => {
            Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, url);
            input.dispatchEvent(new Event('input', {bubbles: true}));
        });
        expect(view.querySelector('button')?.disabled).toBe(false);
        await act(async () => {
            view.querySelector('form')!.dispatchEvent(new Event('submit', {bubbles: true, cancelable: true}));
        });
        expect(view.querySelector('[role="alert"]')?.textContent).toBe('URL could not be added');
        expect(input.value).toBe(url);
        expect(toast).toHaveBeenCalledWith('URL could not be added', {kind: 'error'});

        await act(async () => {
            view.querySelector('form')!.dispatchEvent(new Event('submit', {bubbles: true, cancelable: true}));
        });
        expect(input.value).toBe('');
        expect(view.querySelector('[role="alert"]')).toBeNull();
        expect(view.querySelector('a')?.href).toBe(url);
    } finally {
        await act(() => root.unmount());
    }
});
