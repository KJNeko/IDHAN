import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import type {PanelProps} from '../../host/types';
import {advancedImportPanel} from './AdvancedImportPanel';

Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

function source(path: string): File {
    const file = new File(['image bytes'], path.split('/').at(-1)!, {type: 'image/jpeg'});
    Object.defineProperty(file, 'webkitRelativePath', {value: path});
    return file;
}

async function fixture(status = 1, domains = [{tag_domain_id: 4, domain_name: 'Local'}]) {
    const fetch = vi.fn(async (path: string) => {
        if (path === '/clusters/list') return Response.json([{readonly: false}]);
        if (path.startsWith('/file/import')) return Response.json({record_id: 7, status});
        throw new Error(`Unexpected request ${path}`);
    });
    const addToRecords = vi.fn<PanelProps['host']['tags']['addToRecords']>(async () => {
    });
    const selection = vi.fn();
    const host = {
        http: {fetch}, tags: {listDomains: async () => domains, addToRecords},
        selection: {set: selection},
    } as unknown as PanelProps['host'];
    const view = document.createElement('div');
    const root = createRoot(view);
    const Panel = advancedImportPanel.component;
    await act(() => root.render(<Panel host={host}/>));
    return {
        view, fetch, addToRecords, selection,
        drop: async (paths: string[]) => {
            const event = new Event('drop', {bubbles: true, cancelable: true});
            Object.defineProperty(event, 'dataTransfer', {value: {files: paths.map(source)}});
            await act(() => view.querySelector('.import-drop')!.dispatchEvent(event));
        },
        start: async () => {
            const button = [...view.querySelectorAll<HTMLButtonElement>('button')].find(button => ['Import files', 'Retry failed'].includes(button.textContent!))!;
            await act(() => button.click());
        },
        unmount: () => act(() => root.unmount()),
    };
}

it('previews the folder hierarchy and derived tags before uploading only the basename', async () => {
    const f = await fixture();
    try {
        expect(f.view.querySelector<HTMLInputElement>('[aria-label="Preview folder"]')!.webkitdirectory).toBe(true);
        await f.drop(['artist-name/character-name/whatever.jpg']);
        expect(f.fetch.mock.calls.filter(([path]) => path.startsWith('/file/import'))).toHaveLength(0);
        expect(f.view.querySelectorAll('.advanced-import-folder')).toHaveLength(2);
        expect(f.view.querySelector('.advanced-import-tags')!.textContent).toBe('artist:artist-name · character:character-name');
        expect(f.view.querySelector('.advanced-import-file-head')!.textContent).toContain('whatever.jpg');
        await f.start();
        const requests = f.fetch.mock.calls.filter(([path]) => path.startsWith('/file/import'));
        expect(requests).toHaveLength(1);
        expect(requests[0]![0]).toBe('/file/import?filename=whatever.jpg');
        expect(f.addToRecords).toHaveBeenCalledWith([7], ['artist:artist-name', 'character:character-name'], 4, expect.any(AbortSignal));
        expect(f.selection).toHaveBeenCalledWith([7]);
    } finally {
        await f.unmount();
    }
});

it('retains uploaded records when tagging fails and retries tagging without reuploading', async () => {
    const f = await fixture();
    f.addToRecords.mockRejectedValueOnce(new Error('Tag permission denied'));
    try {
        await f.drop(['someone/character/image.jpg']);
        await f.start();
        expect(f.view.querySelector('[role="alert"]')!.textContent).toContain('stored as #7');
        expect(f.view.querySelector('[role="alert"]')!.textContent).toContain('Tag permission denied');
        await f.start();
        expect(f.fetch.mock.calls.filter(([path]) => path.startsWith('/file/import'))).toHaveLength(1);
        expect(f.addToRecords).toHaveBeenCalledTimes(2);
        expect(f.view.querySelector('[role="alert"]')).toBeNull();
    } finally {
        await f.unmount();
    }
});

it.each([{status: 2, calls: 1, label: 'Already present'}, {status: 3, calls: 0, label: 'Previously deleted'}])(
    'handles stored import outcome $status without incorrectly tagging deleted records', async ({
                                                                                                    status,
                                                                                                    calls,
                                                                                                    label
                                                                                                }) => {
        const f = await fixture(status);
        try {
            await f.drop(['artist/character/image.jpg']);
            await f.start();
            expect(f.addToRecords).toHaveBeenCalledTimes(calls);
            expect(f.view.querySelector('.advanced-import-file-head')!.textContent).toContain(label);
        } finally {
            await f.unmount();
        }
    },
);

it('lets users exclude a container folder and blocks inconsistent folder mappings', async () => {
    const f = await fixture();
    try {
        await f.drop(['collection/artist/character/image.jpg']);
        const root = [...f.view.querySelectorAll<HTMLLabelElement>('label')].find(label => label.textContent === 'Include selected folder')!;
        await act(() => root.querySelector<HTMLInputElement>('input')!.click());
        expect(f.view.querySelector('.advanced-import-tags')!.textContent).toBe('artist:artist · character:character');
        await f.drop(['collection/artist/character/image.jpg', 'collection/artist/shallow.jpg']);
        expect(f.view.querySelector('[role="alert"]')!.textContent).toContain('Missing folder level 2');
        const start = [...f.view.querySelectorAll<HTMLButtonElement>('button')].find(button => button.textContent === 'Import files')!;
        expect(start.disabled).toBe(true);
    } finally {
        await f.unmount();
    }
});


function enterCommonTags(view: HTMLElement, text: string) {
    const input = view.querySelector<HTMLTextAreaElement>('[aria-label="Tags for all files"]')!;
    Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value')!.set!.call(input, text);
    input.dispatchEvent(new Event('input', {bubbles: true}));
}

it.each([1, 2])('appends shared and folder tags in the chosen existing domain for import outcome %i', async status => {
    const f = await fixture(status, [
        {tag_domain_id: 4, domain_name: 'Local'}, {tag_domain_id: 9, domain_name: 'Archive'},
    ]);
    try {
        await act(() => {
            enterCommonTags(f.view, 'rating:safe\nseries:hololive\nartist:artist-name\nrating:safe\n');
            const domain = f.view.querySelector<HTMLSelectElement>('[aria-label="Tag domain"]')!;
            expect([...domain.options].map(option => option.textContent)).toEqual(['Local', 'Archive']);
            domain.value = '9';
            domain.dispatchEvent(new Event('change', {bubbles: true}));
        });
        await f.drop(['artist-name/character-name/one.jpg', 'artist-name/character-name/two.jpg']);
        for (const preview of f.view.querySelectorAll('.advanced-import-tags')) {
            expect(preview.textContent).toBe('rating:safe · series:hololive · artist:artist-name · character:character-name');
        }
        expect(f.addToRecords).not.toHaveBeenCalled();
        await f.start();
        expect(f.addToRecords).toHaveBeenCalledTimes(2);
        expect(f.addToRecords).toHaveBeenNthCalledWith(1, [7],
            ['rating:safe', 'series:hololive', 'artist:artist-name', 'character:character-name'], 9, expect.any(AbortSignal));
        expect(f.addToRecords).toHaveBeenNthCalledWith(2, [7],
            ['rating:safe', 'series:hololive', 'artist:artist-name', 'character:character-name'], 9, expect.any(AbortSignal));
        expect(f.view.querySelector('[role="alert"]')).toBeNull();
    } finally {
        await f.unmount();
    }
});

it('applies shared tags to plain files without requiring folder mappings', async () => {
    const f = await fixture(2);
    try {
        await act(() => enterCommonTags(f.view, 'character:gawr gura\ncollection:one, two'));
        await f.drop(['image.jpg']);
        expect(f.view.querySelector('.advanced-import-mapping')).toBeNull();
        await f.start();
        expect(f.addToRecords).toHaveBeenCalledWith([7], ['character:gawr gura', 'collection:one, two'], 4, expect.any(AbortSignal));
        expect(f.view.querySelector('.advanced-import-file-head')!.textContent).toContain('Already present');
    } finally {
        await f.unmount();
    }
});

it('blocks shared tagging when no existing domain is available', async () => {
    const f = await fixture(1, []);
    try {
        await f.drop(['image.jpg']);
        await act(() => enterCommonTags(f.view, 'rating:safe'));
		expect(f.view.querySelector('[role="alert"]')!.textContent).toContain('No tag domain.');
		expect([...f.view.querySelectorAll('button')].some(button => button.textContent === 'Add tag domain')).toBe(true);
        const start = [...f.view.querySelectorAll<HTMLButtonElement>('button')].find(button => button.textContent === 'Import files')!;
        expect(start.disabled).toBe(true);
        await f.start();
        expect(f.fetch.mock.calls.filter(([path]) => path.startsWith('/file/import'))).toHaveLength(0);
        expect(f.addToRecords).not.toHaveBeenCalled();
    } finally {
        await f.unmount();
    }
});

it('keeps shared tags in the reviewed plan when retrying a tagging failure', async () => {
    const f = await fixture(2);
    f.addToRecords.mockRejectedValueOnce(new Error('Tag permission denied'));
    try {
        await f.drop(['image.jpg']);
        await act(() => enterCommonTags(f.view, 'rating:safe'));
        await f.start();
        expect(f.view.querySelector<HTMLTextAreaElement>('[aria-label="Tags for all files"]')!.disabled).toBe(true);
        expect(f.view.querySelector<HTMLSelectElement>('[aria-label="Tag domain"]')!.disabled).toBe(true);
        await f.start();
        expect(f.fetch.mock.calls.filter(([path]) => path.startsWith('/file/import'))).toHaveLength(1);
        expect(f.addToRecords).toHaveBeenCalledTimes(2);
        expect(f.addToRecords).toHaveBeenLastCalledWith([7], ['rating:safe'], 4, expect.any(AbortSignal));
        expect(f.view.querySelector('[role="alert"]')).toBeNull();
    } finally {
        await f.unmount();
    }
});
