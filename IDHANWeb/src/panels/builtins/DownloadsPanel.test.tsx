import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import type {DownloadSessionInfo, DownloadSessionUrlNode, RecordMetadata} from '../../api/types';
import {forgetMetadata, getMetadata} from '../../host/metadataCache';
import type {PanelProps} from '../../host/types';
import {downloadsPanel} from './DownloadsPanel';

const mocks = vi.hoisted(() => ({
    watch: vi.fn((_handlers: unknown) => () => {
    }),
    recordsMetadata: vi.fn(),
}));
vi.mock('../../api/client', () => ({
    api: {recordsMetadata: mocks.recordsMetadata},
    watchDownloadSessions: mocks.watch,
}));
vi.mock('./recordActions', () => ({
    useRecordMenu: () => ({openRecordMenu: vi.fn(), recordMenu: null}),
}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

function downloadNode(id: number, recordId: number | null): DownloadSessionUrlNode {
    return {
        id, parent_id: null, url: `https://example.test/${id}`, record_id: recordId,
        state: recordId === null ? 'processing' : 'completed',
        created_at: 0, finished_at: recordId === null ? null : id,
        note: null, error: null, children: [],
    };
}

it('refreshes tags on expanded downloads when a followed parser imports the same record', async () => {
    const recordId = 42;
    forgetMetadata([recordId]);
    let metadata: RecordMetadata = {
        record_id: recordId, hashes: {sha256: 'a'.repeat(64)},
        tags: [{domain: 'gelbooru', text: 'gelbooru-only'}],
    };
    mocks.recordsMetadata.mockImplementation(async () => ({records: [metadata], missing: []}));
    const host = {
        records: {getMetadata, thumbnailUrl: () => '/thumbnail'},
        results: {set: vi.fn()}, selection: {set: vi.fn()},
    } as unknown as PanelProps['host'];
    const session: DownloadSessionInfo = {id: 1, name: 'Source follow', created_at: 0, last_used_at: 0, error_count: 0};
    const view = document.createElement('div');
    const root = createRoot(view);
    const DownloadsPanel = downloadsPanel.component;

    try {
        await act(() => root.render(<DownloadsPanel host={host}/>));
        const updates = mocks.watch.mock.calls[0]![0] as {
            onSession: (session: DownloadSessionInfo, urls: DownloadSessionUrlNode[]) => void;
        };
        await act(() => updates.onSession(session, [downloadNode(1, recordId), downloadNode(2, null)]));
        await act(() => view.querySelector<HTMLButtonElement>('.download-tree-twisty:not(:disabled)')!.click());
        expect(view.querySelector('.download-tree-import')!.textContent).toContain('gelbooru-only');
        expect(view.textContent).not.toContain('pixiv-only');

        metadata = {...metadata, tags: [...metadata.tags!, {domain: 'pixiv', text: 'pixiv-only'}]};
        await act(() => updates.onSession(session, [downloadNode(1, recordId), downloadNode(2, recordId)]));
        expect(view.querySelector('.download-tree-import')!.textContent).toContain('pixiv-only');

        await act(() => view.querySelectorAll<HTMLButtonElement>('.download-tree-twisty')[1]!.click());
        const details = view.querySelectorAll('.download-tree-import');
        expect(details).toHaveLength(2);
        for (const detail of details) {
            expect(detail.textContent).toContain('gelbooru-only');
            expect(detail.textContent).toContain('pixiv-only');
        }
        expect(mocks.recordsMetadata).toHaveBeenCalledTimes(2);
        expect(mocks.recordsMetadata.mock.lastCall![0]).toEqual({record_ids: [recordId]});
    } finally {
        await act(() => root.unmount());
        forgetMetadata([recordId]);
    }
});
