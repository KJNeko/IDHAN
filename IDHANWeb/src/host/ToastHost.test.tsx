import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import type {Toast} from './globalServices';

vi.hoisted(() => {
    Object.defineProperty(window, 'matchMedia', {
        configurable: true,
        value: () => ({matches: false, addEventListener: vi.fn()})
    });
});
import {globalServices} from './globalServices';
import {ToastHost} from './ToastHost';

Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('keeps default errors until dismissed, expires success, and releases timers on unmount', async () => {
    vi.useFakeTimers();
    const view = document.createElement('div');
    const root = createRoot(view);
    const emitted: Toast[] = [];
    const unsubscribe = globalServices.toasts.subscribe(toast => emitted.push(toast));
    try {
        await act(() => root.render(<ToastHost/>));
        await act(() => {
            globalServices.toasts.push('Save failed', {kind: 'error'});
            globalServices.toasts.push('Saved', {kind: 'success'});
        });
        expect(emitted.map(toast => toast.timeoutMs)).toEqual([0, 4000]);
        await act(() => vi.advanceTimersByTime(5000));
        expect(view.querySelectorAll('.toast')).toHaveLength(1);
        expect(view.querySelector('[role="alert"]')!.textContent).toContain('Save failed');
        await act(() => view.querySelector<HTMLButtonElement>('button')!.click());
        expect(view.querySelector('.toast')).toBeNull();
        await act(() => globalServices.toasts.push('Temporary', {timeoutMs: 1000}));
        await act(() => root.unmount());
        expect(vi.getTimerCount()).toBe(0);
    } finally {
        unsubscribe();
        vi.useRealTimers();
    }
});
