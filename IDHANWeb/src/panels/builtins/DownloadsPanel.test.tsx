import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import type {DownloadSessionInfo, DownloadSessionUrlNode, RecordMetadata} from '../../api/types';
import {forgetMetadata, getMetadata} from '../../host/metadataCache';
import type {PanelProps} from '../../host/types';
import {downloadsPanel, ErrorLog} from './DownloadsPanel';

const mocks = vi.hoisted(() => ({
    watch: vi.fn((_handlers: unknown) => () => {
    }),
    recordsMetadata: vi.fn(),
    errors: vi.fn(),
    sessionRecords: vi.fn(),
}));
vi.mock('../../api/client', () => ({
    api: {
		downloader: {
		    rateLimits: vi.fn(async () => []),
		    cache: vi.fn(async () => ({hits: 0, misses: 0, hit_rate: 0, entry_count: 0, entries: []}))
	    },
        recordsMetadata: mocks.recordsMetadata,
        downloadSessions: {errors: mocks.errors, records: mocks.sessionRecords}
    },
    watchDownloadSessions: mocks.watch,
}));
vi.mock('./recordActions', () => ({
    useRecordMenu: () => ({openRecordMenu: vi.fn(), recordMenu: null}),
}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

function downloadNode(id: number, recordId: number | null): DownloadSessionUrlNode {
    return {
        id, parent_id: null, url: `https://example.test/${id}`, record_id: recordId,
        state: recordId === null ? 'processing' : 'imported',
        created_at: 0, finished_at: recordId === null ? null : id,
        stage: null, error: null, size: null, children: [],
    };
}

it('expands matching URL IDs independently in each download session', async () => {
	const host = {
		http: {fetch: vi.fn(async () => new Response(JSON.stringify([{readonly: false}]), {status: 200}))},
		records: {thumbnailUrl: () => '/thumbnail'},
	} as unknown as PanelProps['host'];
	const view = document.createElement('div');
	const root = createRoot(view);
	const Panel = downloadsPanel.component;
	try {
		await act(() => root.render(<Panel host={host}/>));
		const updates = mocks.watch.mock.lastCall![0] as {
			onSession: (session: DownloadSessionInfo, urls: DownloadSessionUrlNode[]) => void;
		};
		await act(() => {
			for (const id of [1, 2]) {
				const parent = downloadNode(1, null);
				parent.children = [{...downloadNode(2, null), parent_id: 1}];
				updates.onSession({
					id,
					name: `Session ${id}`,
					created_at: 0,
					last_used_at: id,
					error_count: 0
				}, [parent]);
			}
		});
		const sessions = view.querySelectorAll('.download-tree-session');
		const rowCount = (index: number) => sessions[index]!.querySelectorAll('.download-tree-row').length;
		const toggle = (index: number) => sessions[index]!.querySelector<HTMLButtonElement>('.download-tree-twisty')!.click();
		expect([rowCount(0), rowCount(1)]).toEqual([1, 1]);
		await act(() => toggle(0));
		expect([rowCount(0), rowCount(1)]).toEqual([2, 1]);
		await act(() => toggle(1));
		expect([rowCount(0), rowCount(1)]).toEqual([2, 2]);
		await act(() => toggle(0));
		expect([rowCount(0), rowCount(1)]).toEqual([1, 2]);
	} finally {
		await act(() => root.unmount());
	}
});

it('refreshes tags on expanded downloads when a followed parser imports the same record', async () => {
    const recordId = 42;
    forgetMetadata([recordId]);
    let metadata: RecordMetadata = {
        record_id: recordId, hashes: {sha256: 'a'.repeat(64)},
        tags: [{domain: 'gelbooru', text: 'gelbooru-only'}],
    };
    mocks.recordsMetadata.mockImplementation(async () => ({records: [metadata], missing: []}));
    const host = {
        http: {fetch: vi.fn(async () => new Response(JSON.stringify([{readonly: false}]), {status: 200}))},
        records: {getMetadata, thumbnailUrl: () => '/thumbnail'},
        results: {set: vi.fn()}, selection: {set: vi.fn()},
    } as unknown as PanelProps['host'];
    const session: DownloadSessionInfo = {
        id: 1,
        name: 'Source follow',
        created_at: 0,
        last_used_at: 0,
        error_count: 0,
        downloading: 8,
        waiting: 24,
        waiting_parsers: 3,
        waiting_requests: 32,
        waiting_http_rate: 10,
        waiting_http_shard: 12,
        waiting_storage: 2,
        download_bytes_per_second: 12_000_000,
        downloaded_bytes: 120_000_000,
        average_import_bytes: 2_000_000
    };
    const view = document.createElement('div');
    const root = createRoot(view);
    const DownloadsPanel = downloadsPanel.component;

    try {
        await act(() => root.render(<DownloadsPanel host={host}/>));
		const updates = mocks.watch.mock.lastCall![0] as {
            onSession: (session: DownloadSessionInfo, urls: DownloadSessionUrlNode[]) => void;
			onBandwidth: (bandwidth: {
				download_bytes_per_second: number;
				sessions: { session_id: number; download_bytes_per_second: number }[]
			}) => void;
        };
		await act(() => {
			updates.onSession(session, [downloadNode(1, recordId), downloadNode(2, null)]);
			updates.onBandwidth({
			    download_bytes_per_second: 15_000_000,
			    sessions: [{session_id: 1, download_bytes_per_second: 12_000_000}]
		    });
		});
        expect(view.querySelector('[aria-label="Session network stats"]')?.textContent)
		.toBe('Downloading 8 · Waiting: 3 Parser, 32 Requests · Download: 12.0 MB/s · Downloaded: 120.0 MB · Avg/import: 2.0 MB');
        await act(() => view.querySelector<HTMLButtonElement>('.download-tree-twisty:not(:disabled)')!.click());
		expect(view.querySelector('[aria-label="Global download speed"]')?.textContent).toBe('Download: 15.0 MB/s');
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


it('bounds a thousand errors to 50 rows and resets pagination when the status filter changes', async () => {
    const log = {
        statuses: [{status: 404, count: 1001}],
        errors: Array.from({length: 1001}, (_, id) => ({
            id, url_id: id, url: `https://example.test/${id}`, status: 404, lane: 'example',
            message: 'Unsupported URL\nFull diagnostic details', occurred_at: 1,
        })),
    };
    const view = document.createElement('div');
    const root = createRoot(view);
    const props = {
        count: 1001, log, selected: [], open: true,
        onToggle: vi.fn(), onToggleStatus: vi.fn(), onClearStatuses: vi.fn()
    };
    try {
        await act(() => root.render(<ErrorLog {...props}/>));
        expect(view.querySelectorAll('.download-error-item')).toHaveLength(50);
        expect(view.querySelector('details')!.open).toBe(false);
        const next = [...view.querySelectorAll('button')].find(button => button.textContent === 'Next')!;
        await act(() => next.click());
        expect(view.querySelector('.download-error-url')!.getAttribute('title')).toBe('https://example.test/50');
        expect(view.querySelectorAll('.download-error-item')).toHaveLength(50);
        await act(() => root.render(<ErrorLog {...props} selected={[404]}/>));
        expect(view.querySelector('.download-error-url')!.getAttribute('title')).toBe('https://example.test/0');
        await act(() => root.render(<ErrorLog {...props} open={false}/>));
        expect(view.querySelectorAll('.download-error-item')).toHaveLength(0);
    } finally {
        await act(() => root.unmount());
    }
});


it('isolates session log failures and preserves action errors when the stream reconnects', async () => {
    mocks.errors.mockImplementation(async (id: number) => {
        if (id === 2) throw new Error('Log unavailable');
        return {
            statuses: [{status: 404, count: 1}], errors: [{
                id: 1, url_id: 1,
                url: 'https://example.test/failed', status: 404, lane: 'example', message: 'Not found', occurred_at: 1
            }]
        };
    });
    mocks.sessionRecords.mockRejectedValue(new Error('Records unavailable'));
    const host = {
        http: {fetch: vi.fn(async () => new Response(JSON.stringify([{readonly: false}]), {status: 200}))},
    } as unknown as PanelProps['host'];
    const view = document.createElement('div');
    const root = createRoot(view);
    const Panel = downloadsPanel.component;
    try {
        await act(() => root.render(<Panel host={host}/>));
        const updates = mocks.watch.mock.lastCall![0] as {
            onSession: (session: DownloadSessionInfo, urls: DownloadSessionUrlNode[]) => void;
            onOpen: () => void;
        };
        await act(() => {
            for (const id of [1, 2]) updates.onSession({
                id, name: `Session ${id}`,
                created_at: 0, last_used_at: id, error_count: 1
            }, []);
        });
        for (const button of view.querySelectorAll<HTMLButtonElement>('.download-error-toggle')) {
            await act(() => button.click());
        }
        expect(view.querySelectorAll('.download-error-item')).toHaveLength(1);
        expect(view.textContent).toContain('Could not load errors: Log unavailable');
        await act(() => view.querySelector<HTMLButtonElement>('.download-tree-session-select')!.click());
        expect(view.textContent).toContain('Records unavailable');
        await act(() => updates.onOpen());
        expect(view.textContent).toContain('Records unavailable');
    } finally {
        await act(() => root.unmount());
    }
});


it.each([false, true])('scopes URL selection and live updates to their session (flat: %s)', async (flat) => {
	const setResults = vi.fn();
	const host = {
		http: {fetch: vi.fn(async () => Response.json([{readonly: false}]))},
		records: {thumbnailUrl: () => '/thumbnail'},
		results: {set: setResults}, selection: {set: vi.fn()},
	} as unknown as PanelProps['host'];
	const sessions: DownloadSessionInfo[] = [1, 2].map(id => ({
		id, name: `Session ${id}`, created_at: 0, last_used_at: id, error_count: 0,
	}));
	const view = document.createElement('div');
	const root = createRoot(view);
	const Panel = downloadsPanel.component;
	try {
		await act(() => root.render(<Panel host={host}/>));
		const updates = mocks.watch.mock.lastCall![0] as {
			onSession: (session: DownloadSessionInfo, urls: DownloadSessionUrlNode[]) => void;
			onRemoved: (sessionId: number) => void;
		};
		await act(() => {
			for (const session of sessions) updates.onSession(session, [downloadNode(1, session.id * 10)]);
		});
		if (flat) {
			await act(() => view.querySelector<HTMLInputElement>('.download-tree-toolbar input')!.click());
		}

		const sessionView = (id: number) => [...view.querySelectorAll('.download-tree-session')]
		.find(section => section.querySelector('.download-tree-session-select')!.textContent === `Session ${id}`)!;
		const clickUrl = (id: number) => sessionView(id).querySelector<HTMLButtonElement>('.download-tree-url')!.click();
		const selected = () => view.querySelectorAll('.download-tree-row.is-selected, .download-tree-url.is-selected');
		await act(() => clickUrl(1));
		expect(selected()).toHaveLength(1);
		expect(sessionView(1).contains(selected()[0]!)).toBe(true);
		expect([...setResults.mock.lastCall![0].ids]).toEqual([10]);

		setResults.mockClear();
		await act(() => updates.onSession(sessions[1]!, [downloadNode(1, 21)]));
		expect(setResults).not.toHaveBeenCalled();
		await act(() => updates.onSession(sessions[0]!, [downloadNode(1, 11)]));
		expect([...setResults.mock.lastCall![0].ids]).toEqual([11]);

		await act(() => clickUrl(2));
		expect(selected()).toHaveLength(1);
		expect(sessionView(2).contains(selected()[0]!)).toBe(true);
		expect([...setResults.mock.lastCall![0].ids]).toEqual([21]);

		await act(() => updates.onRemoved(2));
		setResults.mockClear();
		await act(() => updates.onSession(sessions[1]!, [downloadNode(1, 22)]));
		expect(selected()).toHaveLength(0);
		expect(setResults).not.toHaveBeenCalled();
	} finally {
		await act(() => root.unmount());
	}
});


it.each([false, true])('reports a failed child URL without request errors (flat: %s)', async (flat) => {
	mocks.errors.mockResolvedValue({statuses: [], errors: []});
	const host = {
		http: {fetch: vi.fn(async () => Response.json([{readonly: false}]))},
		records: {thumbnailUrl: () => '/thumbnail'},
	} as unknown as PanelProps['host'];
	const session: DownloadSessionInfo = {
		id: 1, name: 'Parser failure', created_at: 0, last_used_at: 0, error_count: 0,
	};
	const parent = downloadNode(1, null);
	parent.url = 'https://mega.nz/folder/example#share-key';
	const failure: DownloadSessionUrlNode = {
		...downloadNode(2, null), parent_id: 1, state: 'failed', stage: 'parser',
		url: `${parent.url}/file/child`,
		error: 'Parser returned invalid data\nExpected a media URL',
	};
	parent.children = [failure];
	const view = document.createElement('div');
	const root = createRoot(view);
	const Panel = downloadsPanel.component;
	try {
		await act(() => root.render(<Panel host={host}/>));
		const updates = mocks.watch.mock.lastCall![0] as {
			onSession: (session: DownloadSessionInfo, urls: DownloadSessionUrlNode[]) => void;
		};
		await act(() => updates.onSession(session, [parent]));
		if (flat) {
			await act(() => view.querySelector<HTMLInputElement>('.download-tree-toolbar input')!.click());
		} else {
			await act(() => view.querySelector<HTMLButtonElement>('.download-tree-twisty')!.click());
		}

		const labels = Array.from(view.querySelectorAll('.download-tree-url-text'), label => label.textContent);
		expect(labels).toContain('mega.nz/folder/example#share-key');
		expect(labels).toContain('mega.nz/folder/example#share-key/file/child');
		const toggle = view.querySelector<HTMLButtonElement>('.download-error-toggle')!;
		expect(toggle.disabled).toBe(false);
		expect(toggle.textContent).toContain('Errors (1)');
		await act(async () => {
			toggle.click();
		});
		const failures = view.querySelector('[aria-label="Failed URLs"]')!;
		expect(failures.querySelector('.download-error-url')!.getAttribute('title')).toBe(failure.url);
		expect(failures.querySelector('.download-error-url')!.textContent).toBe('mega.nz/folder/example#share-key/file/child');
		expect(failures.querySelector('.download-error-message')!.textContent).toBe(failure.error);
		expect(failures.querySelector('.download-error-status')!.textContent).toBe('failed (Parser Error)');
		expect(view.querySelector('.download-error-log')!.textContent).toContain('Request errors (0)');

		await act(async () => {
			updates.onSession(session, [{
				...parent,
				children: [{...failure, state: 'completed', error: null, stage: null}]
			}]);
		});
		expect(view.querySelector('[aria-label="Failed URLs"]')).toBeNull();
		expect(toggle.textContent).toContain('Errors (0)');
	} finally {
		await act(() => root.unmount());
	}
});

it('bounds failed URL details to 50 rows and provides a fallback for missing reasons', async () => {
	const failures: DownloadSessionUrlNode[] = Array.from({length: 51}, (_, id) => ({
		...downloadNode(id, null), state: 'failed',
	}));
	const view = document.createElement('div');
	const root = createRoot(view);
	try {
		await act(() => root.render(<ErrorLog count={0} failures={failures} log={{statuses: [], errors: []}}
											  selected={[]} open={true} onToggle={vi.fn()} onToggleStatus={vi.fn()}
		                                      onClearStatuses={vi.fn()}/>));
		expect(view.querySelectorAll('.download-error-item')).toHaveLength(50);
		expect(view.querySelector('.download-error-message')!.textContent).toBe('The download failed without a reason');
		const next = [...view.querySelectorAll<HTMLButtonElement>('[aria-label="Failed URL pages"] button')]
		.find(button => button.textContent === 'Next')!;
		await act(() => next.click());
		expect(view.querySelectorAll('.download-error-item')).toHaveLength(1);
		expect(view.querySelector('.download-error-url')!.getAttribute('title')).toBe(failures[50]!.url);
	} finally {
		await act(() => root.unmount());
	}
});
