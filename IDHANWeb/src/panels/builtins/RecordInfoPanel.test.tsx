import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import type {PanelProps, RecordId} from '../../host/types';
import {recordInfoPanel} from './RecordInfoPanel';

Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('shows the server failure with retry, cancels obsolete requests, and rejects mismatched records', async () => {
    const fetch = vi.fn()
        .mockResolvedValueOnce(new Response('File cluster unavailable', {status: 503}))
        .mockResolvedValueOnce(Response.json({record_id: 42, mime: 'image/png', size: 100}))
        .mockResolvedValueOnce(Response.json({record_id: 42}));
    let onSelection: (ids: readonly RecordId[]) => void = () => {
    };
    const unsubscribe = vi.fn();
    const host = {
        http: {fetch}, selection: {
            get: () => [42], subscribe: (listener: typeof onSelection) => {
                onSelection = listener;
                return unsubscribe;
            }
        }
    } as unknown as PanelProps['host'];
    const view = document.createElement('div');
    const root = createRoot(view);
    const Panel = recordInfoPanel.component;
    try {
        await act(() => root.render(<Panel host={host}/>));
        expect(view.querySelector('[role="alert"]')!.textContent).toBe('File cluster unavailable');
        expect(view.querySelector('.record-info-overview')).toBeNull();
        const firstSignal = fetch.mock.calls[0]![1].signal as AbortSignal;
        await act(() => view.querySelector<HTMLButtonElement>('.record-info-error button')!.click());
        expect(firstSignal.aborted).toBe(true);
        expect(view.querySelector('[role="alert"]')).toBeNull();
        expect(view.querySelector('.record-info-overview')!.textContent).toContain('#42');
        await act(() => onSelection([42, 43]));
        expect(view.querySelector('.record-info-selection')!.textContent).toBe('2 selected · #43');
        expect(view.querySelector('[role="alert"]')!.textContent).toBe('The server returned invalid record details.');
        expect(view.querySelector('.record-info-overview')).toBeNull();
    } finally {
        await act(() => root.unmount());
    }
    expect(unsubscribe).toHaveBeenCalledOnce();
});
