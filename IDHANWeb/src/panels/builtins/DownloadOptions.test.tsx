import {act, useState} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import type {DownloadSettings} from '../../api/types';
import {DownloadOptions} from './DownloadOptions';

const options = vi.hoisted(() => vi.fn());
vi.mock('../../api/client', () => ({api: {downloadSessions: {options}}}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('loads nested options and keeps overrides isolated by parser', async () => {
    options.mockResolvedValue({
        force_import: false, parsers: [
            {
                id: 'first',
                options: [{name: 'enabled', type: 'boolean', default: false, description: 'Enable first parser'}]
            },
            {
                id: 'second',
                options: [{name: 'enabled', type: 'boolean', default: false, description: 'Enable second parser'}]
            },
        ]
    });
    let settings: DownloadSettings = {};

    function View() {
        const [value, setValue] = useState<DownloadSettings>({});
        settings = value;
        return <DownloadOptions settings={value} onChange={setValue} disabled={false}/>;
    }

    const view = document.createElement('div');
    const root = createRoot(view);
    try {
        await act(() => root.render(<View/>));
        expect(options).not.toHaveBeenCalled();
        await act(() => {
            const details = view.querySelector('details')!;
            details.open = true;
            details.dispatchEvent(new Event('toggle'));
        });
        expect([...view.querySelectorAll('summary')].map(node => node.textContent))
		.toEqual(['Downloader options', 'first (1)', 'Hosts', 'second (1)', 'Hosts']);
        const inputs = view.querySelectorAll<HTMLInputElement>('input[type="checkbox"]');
        await act(() => inputs[0]!.click());
        await act(() => inputs[1]!.click());
        expect(settings).toEqual({force_import: true, parsers: {first: {enabled: true}}});
        expect(inputs[2]!.checked).toBe(false);
    } finally {
        await act(() => root.unmount());
    }
});

it('offers a retry after discovery fails', async () => {
    options.mockRejectedValueOnce(new Error('Offline')).mockResolvedValueOnce({force_import: true, parsers: []});
    const view = document.createElement('div');
    const root = createRoot(view);
    try {
        await act(() => root.render(<DownloadOptions settings={{}} onChange={vi.fn()} disabled={false}/>));
        await act(() => {
            const details = view.querySelector('details')!;
            details.open = true;
            details.dispatchEvent(new Event('toggle'));
        });
        expect(view.querySelector('[role="alert"]')?.textContent).toContain('Offline');
        await act(() => view.querySelector<HTMLButtonElement>('button')!.click());
        expect(view.querySelector('[role="alert"]')).toBeNull();
        expect(view.querySelector<HTMLInputElement>('input')!.checked).toBe(true);
    } finally {
        await act(() => root.unmount());
    }
});

it('uses exposed types and numeric constraints to validate the form before submission', async () => {
    options.mockResolvedValue({
        force_import: false, parsers: [{
            id: 'typed', options: [
                {name: 'count', type: 'number', default: 2, minimum: 1, integer: true, description: 'Maximum count'},
                {name: 'ratio', type: 'number', default: 0.5, minimum: 0, description: 'Ratio'},
                {name: 'title', type: 'string', default: 'Example', description: 'Title'},
                {name: 'tags', type: 'string[]', default: ['one', 'two'], description: 'Tags'},
            ]
        }]
    });
    const view = document.createElement('div');
    const root = createRoot(view);
    try {
        await act(() => root.render(<form><DownloadOptions settings={{}} onChange={vi.fn()} disabled={false}/></form>));
        await act(() => {
            const details = view.querySelector('details')!;
            details.open = true;
            details.dispatchEvent(new Event('toggle'));
        });
        const form = view.querySelector('form')!;
        const numbers = view.querySelectorAll<HTMLInputElement>('input[type="number"]');
        expect(form.checkValidity()).toBe(true);
        expect(numbers[0]!.min).toBe('1');
        expect(numbers[0]!.step).toBe('1');
        expect(numbers[1]!.step).toBe('any');
        expect(view.querySelector<HTMLInputElement>('input[type="text"]:not([placeholder])')!.value).toBe('Example');
        expect(view.querySelector('textarea')!.value).toBe('one\ntwo');
        for (const invalid of ['', '0', '1.5']) {
            numbers[0]!.value = invalid;
            expect(form.checkValidity()).toBe(false);
        }
        numbers[0]!.value = '3';
        expect(form.checkValidity()).toBe(true);
    } finally {
        await act(() => root.unmount());
    }
});
