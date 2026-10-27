import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {api, watchDownloadSessions} from '../../api/client';
import type {PanelProps} from '../../host/types';
import {importPanel} from './ImportPanel';
import {downloadsPanel} from './DownloadsPanel';

vi.mock('../../api/client', async importOriginal => {
    const original = await importOriginal<typeof import('../../api/client')>();
    return {
        ...original,
        api: {
            ...original.api,
            downloadSessions: {
                ...original.api.downloadSessions,
                list: vi.fn(), urls: vi.fn(), records: vi.fn(),
                submitUrl: vi.fn(), submitUrlSession: vi.fn(), retryUrl: vi.fn(),
            },
        },
        watchDownloadSessions: vi.fn(),
    };
});
vi.mock('./recordActions', () => ({
    useRecordMenu: () => ({openRecordMenu: vi.fn(), recordMenu: null}),
}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

let unmount: (() => Promise<void>) | undefined;

beforeEach(() => {
    vi.clearAllMocks();
    vi.mocked(api.downloadSessions.list).mockResolvedValue([]);
    vi.mocked(api.downloadSessions.urls).mockResolvedValue([]);
    vi.mocked(watchDownloadSessions).mockReturnValue(() => {
    });
});

afterEach(async () => {
    await unmount?.();
});

async function mount(fetch: ReturnType<typeof vi.fn>, panel = importPanel.component) {
    const container = document.createElement('div');
    const root = createRoot(container);
    const host = {
        http: {fetch}, selection: {set: vi.fn()}, results: {set: vi.fn()},
        ui: {toast: vi.fn()}, records: {thumbnailUrl: () => '/thumbnail'},
    } as unknown as PanelProps['host'];
    unmount = () => act(() => root.unmount());
    const Panel = panel;
    await act(() => root.render(<Panel host={host}/>));
    return {container, host};
}

function storageFetch(clusters: () => { readonly: boolean }[], upload = () => new Response(JSON.stringify({
    status: 1,
    record_id: 7
}))) {
    return vi.fn(async (path: string) => {
        if (path === '/clusters/list') return new Response(JSON.stringify(clusters()));
        if (path.startsWith('/file/import')) return upload();
        if (path === '/records/7/info') return new Response('', {status: 404});
        throw new Error(`Unexpected request: ${path}`);
    });
}

async function dropFile(container: HTMLElement) {
    const drop = new Event('drop', {bubbles: true, cancelable: true});
    Object.defineProperty(drop, 'dataTransfer', {value: {files: [new File(['bytes'], 'image.png')]}});
    await act(() => {
        container.querySelector('.import-drop')!.dispatchEvent(drop);
    });
}

describe('import storage availability', () => {
    it.each([{clusters: []}, {clusters: [{readonly: true}]}])('blocks file drops without a writable cluster: %j', async ({clusters}) => {
        const fetch = storageFetch(() => clusters);
        const {container} = await mount(fetch);
        expect(container.querySelector('.import-drop')!.getAttribute('aria-disabled')).toBe('true');
        expect(container.querySelector<HTMLInputElement>('input[type=file]')!.disabled).toBe(true);
        expect(container.textContent).toContain(clusters.length ? 'read-only' : 'No storage clusters');
        await dropFile(container);
        expect(fetch.mock.calls.some(([path]) => path.startsWith('/file/import'))).toBe(false);
    });

    it('allows importing when a writable cluster exists alongside a read-only cluster', async () => {
        const fetch = storageFetch(() => [{readonly: true}, {readonly: false}]);
        const {container, host} = await mount(fetch);
        await dropFile(container);
        expect(fetch.mock.calls.some(([path]) => path.startsWith('/file/import'))).toBe(true);
        expect(host.selection.set).toHaveBeenCalledWith([7]);
        expect(container.textContent).toContain('1 imported');
    });

    it('rechecks storage before uploading and notices a cluster becoming read-only', async () => {
        let readonly = false;
        const fetch = storageFetch(() => [{readonly}]);
        const {container} = await mount(fetch);
        readonly = true;
        await dropFile(container);
        expect(fetch.mock.calls.filter(([path]) => path === '/clusters/list')).toHaveLength(2);
        expect(fetch.mock.calls.some(([path]) => path.startsWith('/file/import'))).toBe(false);
        expect(container.textContent).toContain('read-only');
    });

    it('enables importing after storage is made writable and refreshed', async () => {
        let readonly = true;
        const {container} = await mount(storageFetch(() => [{readonly}]));
        readonly = false;
        await act(() => {
            [...container.querySelectorAll('button')].find(button => button.textContent === 'Refresh storage')!.click();
        });
        expect(container.querySelector<HTMLInputElement>('input[type=file]')!.disabled).toBe(false);
    });

    it('shows storage-check errors and keeps imports disabled', async () => {
        const {container} = await mount(vi.fn(async () => new Response('Database unavailable', {status: 503})));
        expect(container.textContent).toContain('Database unavailable');
        expect(container.querySelector<HTMLInputElement>('input[type=file]')!.disabled).toBe(true);
    });

    it('blocks URL submissions in both import and download panels', async () => {
        vi.mocked(api.downloadSessions.list).mockResolvedValue([
            {id: 1, name: 'Downloads', created_at: 0, last_used_at: 0, error_count: 0},
        ]);
        const fetch = storageFetch(() => [{readonly: true}]);
        const {container} = await mount(fetch);
        expect(container.querySelector<HTMLTextAreaElement>('textarea')!.disabled).toBe(true);
        expect([...container.querySelectorAll('button')].find(button => button.textContent === 'Queue URL(s)')!.disabled).toBe(true);
        await unmount?.();
        const downloads = await mount(fetch, downloadsPanel.component);
        expect(downloads.container.querySelector<HTMLTextAreaElement>('textarea')!.disabled).toBe(true);
        expect([...downloads.container.querySelectorAll('button')].find(button => button.textContent === 'Download URL(s)')!.disabled).toBe(true);
        expect(api.downloadSessions.submitUrl).not.toHaveBeenCalled();
        expect(api.downloadSessions.submitUrlSession).not.toHaveBeenCalled();
    });

    it('rechecks storage before queuing a download URL', async () => {
        let readonly = false;
        const {container} = await mount(storageFetch(() => [{readonly}]), downloadsPanel.component);
        const textarea = container.querySelector('textarea')!;
        const setValue = Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value')!.set!;
        await act(() => {
            setValue.call(textarea, 'https://example.test/image.png');
            textarea.dispatchEvent(new Event('input', {bubbles: true}));
        });
        const submit = [...container.querySelectorAll('button')].find(button => button.textContent === 'Download URL(s)')!;
        expect(submit.disabled).toBe(false);
        readonly = true;
        await act(() => {
            submit.click();
        });
        expect(api.downloadSessions.submitUrlSession).not.toHaveBeenCalled();
        expect(container.textContent).toContain('read-only');
    });
});

describe('import failure reasons', () => {
    it.each([
        {status: 400, body: 'All clusters are read-only', reason: 'All clusters are read-only'},
        {
            status: 503,
            body: 'No writable cluster has a verified identity',
            reason: 'No writable cluster has a verified identity'
        },
        {status: 500, body: JSON.stringify({error: 'Disk full'}), reason: 'Disk full'},
        {status: 200, body: JSON.stringify({status: 4, reason: 'unknown mime'}), reason: 'unknown mime'},
    ])('shows the server reason for status $status: $reason', async ({status, body, reason}) => {
        const {container} = await mount(storageFetch(() => [{readonly: false}], () => new Response(body, {status})));
        await dropFile(container);
        expect(container.querySelector('.import-row.state-error .import-reason')!.textContent).toBe(reason);
        expect(container.textContent).toContain('1 failed');
    });
});
