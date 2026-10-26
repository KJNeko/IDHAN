import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import type {PanelProps} from '../../host/types';
import type {DebugModule} from '../../api/types';
import {watchDownloaderDebug, watchRateLimits} from '../../api/client';
import {debugPanel} from './DebugPanel';

vi.mock('../../api/client', async importOriginal => ({
    ...await importOriginal<typeof import('../../api/client')>(),
    watchDownloaderDebug: vi.fn(), watchRateLimits: vi.fn(),
}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('subscribes to diagnostics and rate limits without polling when its clocks advance', async () => {
    vi.useFakeTimers();
    const closeDebug = vi.fn();
    const closeLimits = vi.fn();
    vi.mocked(watchDownloaderDebug).mockReturnValue(closeDebug);
    vi.mocked(watchRateLimits).mockReturnValue(closeLimits);
    const fetch = vi.fn();
    const host = {http: {fetch}, ui: {toast: vi.fn()}} as unknown as PanelProps['host'];
    const root = createRoot(document.createElement('div'));
    const Panel = debugPanel.component;
    try {
        await act(() => root.render(<Panel host={host}/>));
        await act(() => vi.advanceTimersByTime(5000));
        expect(watchDownloaderDebug).toHaveBeenCalledOnce();
        expect(watchRateLimits).toHaveBeenCalledOnce();
        expect(fetch).not.toHaveBeenCalled();
    } finally {
        await act(() => root.unmount());
        vi.useRealTimers();
    }
    expect(closeDebug).toHaveBeenCalledOnce();
    expect(closeLimits).toHaveBeenCalledOnce();
});

it('shows module backlogs above lanes and updates them from the diagnostic stream', async () => {
    vi.mocked(watchDownloaderDebug).mockClear();
    vi.mocked(watchDownloaderDebug).mockReturnValue(vi.fn());
    vi.mocked(watchRateLimits).mockReturnValue(vi.fn());
    const container = document.createElement('div');
    const root = createRoot(container);
    const Panel = debugPanel.component;
    const host = {http: {fetch: vi.fn()}, ui: {toast: vi.fn()}} as unknown as PanelProps['host'];
    const module: DebugModule = {
        library_index: 0, module_index: 0, library: 'media.so', name: 'Metadata',
        queued: 4, running: 1, workers: 1, reporting: true, thread_safe: false,
    };
    try {
        await act(() => root.render(<Panel host={host}/>));
        const {onState} = vi.mocked(watchDownloaderDebug).mock.calls[0][0];
        await act(() => onState({
            sessions: [], lanes: [], modules: [
                {...module, module_index: 1, name: 'Thumbnail', queued: 0, running: 0}, module,
            ]
        }, true));
        const headings = [...container.querySelectorAll('h4')].map(element => element.textContent);
        expect(headings.indexOf('Modules')).toBeLessThan(headings.indexOf('Lanes'));
        const rows = [...container.querySelectorAll('table tbody tr')];
        expect(rows[0].textContent).toContain('Metadata');
        expect([...rows[0].querySelectorAll('td')].map(cell => cell.textContent))
            .toEqual(['Metadata', 'media.so', 'Running', '4', '1', '1']);

        await act(() => onState({
            sessions: [], lanes: [], modules: [
                {...module, queued: 0, running: 0, workers: 0},
            ]
        }, false));
        expect(container.querySelector('table tbody tr')?.textContent).toContain('Stopped');

        await act(() => onState({
            sessions: [], lanes: [], modules: [
                {...module, reporting: false},
            ]
        }, false));
        expect([...container.querySelectorAll('table tbody td')].map(cell => cell.textContent))
            .toEqual(['Metadata', 'media.so', 'Awaiting report', '—', '—', '1']);
    } finally {
        await act(() => root.unmount());
    }
});
