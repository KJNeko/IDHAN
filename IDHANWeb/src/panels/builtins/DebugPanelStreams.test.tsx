import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import type {PanelProps} from '../../host/types';
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
