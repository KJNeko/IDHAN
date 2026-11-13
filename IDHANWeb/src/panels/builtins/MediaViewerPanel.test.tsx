import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import type {PanelProps} from '../../host/types';
import {mediaViewerPanel} from './MediaViewerPanel';

vi.mock('./recordActions', () => ({
    RECORD_ACTIVATE_TOPIC: 'record.activate',
    useRecordMenu: () => ({openRecordMenu: vi.fn(), recordMenu: null}),
}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('shows metadata and media failures with retry instead of a successful or loading preview', async () => {
    const getMetadata = vi.fn()
        .mockRejectedValueOnce(new Error('Server unavailable'))
        .mockResolvedValue({records: [{record_id: 42, simple_type: 'image'}], missing: []});
    const host = {
        results: {
            get: () => ({ids: new Int32Array([42])}), subscribe: () => () => {
            }
        },
        selection: {
            get: () => [42], subscribe: () => () => {
            }
        },
        bus: {
            on: () => () => {
            }
        },
        records: {getMetadata, fileUrl: () => '/file', thumbnailUrl: () => '/thumb'},
    } as unknown as PanelProps['host'];
    const view = document.createElement('div');
    const root = createRoot(view);
    const Panel = mediaViewerPanel.component;
    try {
        await act(() => root.render(<Panel host={host}/>));
        expect(view.querySelector('[role="alert"]')!.textContent).toContain('Server unavailable');
        expect(view.querySelector('.viewer-loading')).toBeNull();
        await act(() => view.querySelector<HTMLButtonElement>('.viewer-fallback button')!.click());
        expect(view.querySelector('img')).not.toBeNull();
        expect(view.querySelector('[role="alert"]')).toBeNull();
        await act(() => view.querySelector('img')!.dispatchEvent(new Event('error')));
        expect(view.querySelector('[role="alert"]')!.textContent).toBe('Could not load media.');
        expect(view.querySelector('.viewer-loading')).toBeNull();
        expect(view.querySelector<HTMLAnchorElement>('.viewer-download')!.getAttribute('href')).toBe('/file');
    } finally {
        await act(() => root.unmount());
    }
});
