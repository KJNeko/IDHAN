import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import {ParserHosts} from './ParserHosts';

const hosts = vi.hoisted(() => ({list: vi.fn(), set: vi.fn(), remove: vi.fn()}));
vi.mock('../../api/client', () => ({api: {downloaderHosts: hosts}}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('labels provenance and removes only the selected host', async () => {
    hosts.list.mockResolvedValueOnce([
        {host: 'auto.example', parser: 'hyshare', auto_discovered: true},
        {host: 'manual.example', parser: 'hyshare', auto_discovered: false},
        {host: 'other.example', parser: 'other', auto_discovered: true},
    ]).mockResolvedValue([{host: 'manual.example', parser: 'hyshare', auto_discovered: false}]);
    hosts.remove.mockResolvedValue({deleted: true});
    const view = document.createElement('div');
    const root = createRoot(view);
    try {
        await act(() => root.render(<ParserHosts parser="hyshare" disabled={false}/>));
        await act(() => {
            const details = view.querySelector('details')!;
            details.open = true;
            details.dispatchEvent(new Event('toggle'));
        });
        expect(view.textContent).toContain('auto.example — Auto-discovered');
        expect(view.textContent).toContain('manual.example — User added');
        expect(view.textContent).not.toContain('other.example');
        await act(() => view.querySelector<HTMLButtonElement>('[aria-label="Remove auto.example"]')!.click());
        expect(hosts.remove).toHaveBeenCalledWith('auto.example');
        expect(view.querySelector('[aria-label="Remove auto.example"]')).toBeNull();
        expect(view.querySelector('[aria-label="Remove manual.example"]')).not.toBeNull();
    } finally {
        await act(() => root.unmount());
    }
});

it('adds a user-supplied host to the selected parser', async () => {
    hosts.list.mockReset().mockResolvedValue([]);
    hosts.set.mockResolvedValue([{host: 'manual.example', parser: 'hyshare', auto_discovered: false}]);
    const view = document.createElement('div');
    const root = createRoot(view);
    try {
        await act(() => root.render(<ParserHosts parser="hyshare" disabled={false}/>));
        const input = view.querySelector('input')!;
        await act(() => {
            Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, 'manual.example');
            input.dispatchEvent(new Event('input', {bubbles: true}));
        });
        const add = [...view.querySelectorAll('button')].find(button => button.textContent === 'Add host')!;
        expect(add.disabled).toBe(false);
        await act(() => add.click());
        expect(hosts.set).toHaveBeenCalledWith('manual.example', 'hyshare');
        expect(input.value).toBe('');
    } finally {
        await act(() => root.unmount());
    }
});
