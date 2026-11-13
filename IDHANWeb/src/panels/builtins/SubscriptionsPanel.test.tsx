import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, expect, it, vi} from 'vitest';
import {api} from '../../api/client';
import {watchSubscriptions} from '../../api/subscriptionsStream';
import type {Subscription, SubscriptionGroup, DownloaderParser} from '../../api/types';
import {subscriptionsPanel} from './SubscriptionsPanel';

vi.mock('../../api/subscriptionsStream', () => ({watchSubscriptions: vi.fn()}));

async function emitSubscriptions() {
	const handlers = vi.mocked(watchSubscriptions).mock.lastCall![0];
	const [groups, items] = await Promise.all([
		api.subscriptionGroups.list(), api.subscriptions.list(),
	]);
	handlers.onChange({groups, items});
}

vi.mock('../../api/client', () => ({
	watchJobs: vi.fn(() => () => {
	}), api: {
		subscriptionGroups: {list: vi.fn(), create: vi.fn(), update: vi.fn(), run: vi.fn(), remove: vi.fn()},
		subscriptions: {
			list: vi.fn(),
			batch: vi.fn(),
			records: vi.fn(),
			create: vi.fn(),
			update: vi.fn(),
			run: vi.fn(),
			remove: vi.fn()
		},
		downloadSessions: {parsers: vi.fn()},
	}
}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

const group: SubscriptionGroup = {
	id: 7, name: 'Artists', generator: 'gelbooru:search', interval_seconds: 86400, enabled: true,
	subscription_count: 1, record_count: 12, total_size_bytes: 3145728, running_count: 0, dead_count: 0, error_count: 0,
};
const parsers: DownloaderParser[] = [
	{
		id: 'gelbooru', filename: 'gelbooru.js', version: 1, options: [], discoveries: [], urlClasses: [],
		handlers: [{id: 'search', method: 'search', parameters: {tags: {name: 'Tags', type: 'string'}}},
			{id: 'post', method: 'post', parameters: {id: {name: 'Post ID', type: 'string'}}}]
	},
	{
		id: 'other', filename: 'other.js', version: 1, options: [], discoveries: [], urlClasses: [],
		handlers: [{id: 'creator', method: 'creator', parameters: {name: {name: 'Creator', type: 'string'}}}]
	},
];
const item: Subscription = {
	id: 9,
	group_id: 7,
	tags: 'old_artist',
	parameters: {tags: 'old_artist'},
	enabled: true,
	record_count: 12,
	total_size_bytes: 1048576,
	latest_post_at: null,
	post_dates_complete: false,
	dead: false,
	next_run_at: 0,
	initial_sync_complete: true,
	active_session_id: null,
	last_session_id: null,
	last_finished_at: null,
	last_error: null,
};
let view: HTMLDivElement;
let root: Root;
beforeEach(() => {
	vi.clearAllMocks();
	vi.mocked(watchSubscriptions).mockImplementation(() => {
		void emitSubscriptions();
		return () => undefined;
	});
	vi.mocked(api.subscriptionGroups.list).mockResolvedValue([group]);
	vi.mocked(api.downloadSessions.parsers).mockResolvedValue({parsers});
	vi.mocked(api.subscriptions.list).mockResolvedValue([item]);
	vi.mocked(api.subscriptions.create).mockResolvedValue(item);
	vi.mocked(api.subscriptionGroups.update).mockResolvedValue(group);
	view = document.createElement('div');
	document.body.append(view);
	root = createRoot(view);
});
afterEach(async () => {
	await act(() => root.unmount());
	view.remove();
});

async function render() {
	const Panel = subscriptionsPanel.component;
	await act(() => root.render(<Panel/>));
}

async function input(field: HTMLInputElement, value: string) {
	await act(() => {
		Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(field, value);
		field.dispatchEvent(new Event('input', {bubbles: true}));
	});
}

it('adds a parameter-only search through the group input and keeps its name on the group', async () => {
	await render();
	expect(view.querySelector('h3')?.textContent).toBe('Artists');
	expect(view.textContent).toContain('1 item · 12 files');
	expect(view.querySelector('.subscription-items')?.textContent).toContain('old_artist');
	expect(view.querySelector('.subscription-settings')).toBeNull();
	const field = view.querySelector<HTMLInputElement>('input[aria-label="Tags"]')!;
	field.focus();
	expect(field.placeholder).toBe('Tags');
	await input(field, '  cat_ears   nude,blue  ');
	await act(() => field.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true, cancelable: true})));
	expect(api.subscriptions.create).toHaveBeenCalledWith(7, {tags: '  cat_ears   nude,blue  ', enabled: true});
	expect(field.value).toBe('');
	expect(document.activeElement).toBe(field);
	expect(api.subscriptionGroups.create).not.toHaveBeenCalled();
});

it('edits shared intervals', async () => {
	await render();
	await act(() => [...view.querySelectorAll('button')].find(button => button.textContent === 'Settings')!.click());
	const unit = view.querySelector<HTMLSelectElement>('select[aria-label="Interval unit"]')!;
	await act(() => {
		unit.value = 'months';
		unit.dispatchEvent(new Event('change', {bubbles: true}));
	});
	await input(view.querySelector<HTMLInputElement>('input[aria-label="Check interval"]')!, '3');
	await act(() => view.querySelector('.subscription-settings')!.dispatchEvent(new Event('submit', {
		bubbles: true,
		cancelable: true
	})));
	expect(api.subscriptionGroups.update).toHaveBeenCalledWith(7, expect.objectContaining({
		interval_seconds: 7776000, generator: 'gelbooru:search', name: 'Artists',
	}));
});

it('offers every declared generator from every parser and uses its field names', async () => {
	await render();
	const select = view.querySelector<HTMLSelectElement>('select[aria-label="Generator"]')!;
	expect([...select.options].map(option => option.value)).toEqual(['gelbooru:search', 'gelbooru:post', 'other:creator']);
	vi.mocked(api.subscriptionGroups.list).mockResolvedValue([{...group, generator: 'other:creator'}]);
	// A streamed group change selects the new generator's input.
	vi.mocked(api.subscriptionGroups.create).mockResolvedValue(group);
	const field = view.querySelector<HTMLInputElement>('input[aria-label="New group"]')!;
	await input(field, 'Another');
	await act(() => field.closest('form')!.dispatchEvent(new Event('submit', {bubbles: true, cancelable: true})));
	await act(() => emitSubscriptions());
	const creator = view.querySelector<HTMLInputElement>('input[aria-label="Creator"]')!;
	expect(creator.placeholder).toBe('Creator');
	await input(creator, 'some.creator');
	await act(() => creator.closest('form')!.dispatchEvent(new Event('submit', {bubbles: true, cancelable: true})));
	expect(api.subscriptions.create).toHaveBeenLastCalledWith(7, {parameters: {name: 'some.creator'}, enabled: true});
});

it('submits multi-field generators with Enter and preserves IME composition', async () => {
	const multi = structuredClone(parsers);
	multi[1].handlers[0].parameters.region = {name: 'Region', type: 'string'};
	vi.mocked(api.downloadSessions.parsers).mockResolvedValue({parsers: multi});
	vi.mocked(api.subscriptionGroups.list).mockResolvedValue([{...group, generator: 'other:creator'}]);
	await render();
	const creator = view.querySelector<HTMLInputElement>('input[aria-label="Creator"]')!;
	const region = view.querySelector<HTMLInputElement>('input[aria-label="Region"]')!;
	await input(creator, 'some.creator');
	await act(() => creator.dispatchEvent(new KeyboardEvent('keydown', {
		key: 'Enter',
		bubbles: true,
		cancelable: true
	})));
	expect(api.subscriptions.create).not.toHaveBeenCalled();
	await input(region, 'west');
	await act(() => region.dispatchEvent(new KeyboardEvent('keydown', {
		key: 'Enter',
		isComposing: true,
		bubbles: true,
		cancelable: true
	})));
	expect(api.subscriptions.create).not.toHaveBeenCalled();
	await act(() => region.dispatchEvent(new KeyboardEvent('keydown', {
		key: 'Enter',
		bubbles: true,
		cancelable: true
	})));
	expect(api.subscriptions.create).toHaveBeenCalledExactlyOnceWith(7, {
		parameters: {name: 'some.creator', region: 'west'}, enabled: true,
	});
});

it('uses declared string defaults while allowing an explicit empty value', async () => {
	const configured = structuredClone(parsers);
	configured[1].handlers[0].parameters.name.default = 'default creator';
	vi.mocked(api.downloadSessions.parsers).mockResolvedValue({parsers: configured});
	vi.mocked(api.subscriptionGroups.list).mockResolvedValue([{...group, generator: 'other:creator'}]);
	await render();
	const field = view.querySelector<HTMLInputElement>('input[aria-label="Creator"]')!;
	expect(field.value).toBe('default creator');
	await input(field, '');
	await act(() => field.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true, cancelable: true})));
	expect(api.subscriptions.create).toHaveBeenCalledExactlyOnceWith(7, {parameters: {name: ''}, enabled: true});
});

it('shows each next run and the earliest enabled idle query on its group', async () => {
	const early = Math.floor(Date.now() / 1000) + 86400;
	const later = early + 86400;
	vi.mocked(api.subscriptions.list).mockResolvedValue([
		{...item, tags: 'later', parameters: {tags: 'later'}, next_run_at: later},
		{...item, id: 10, tags: 'early', parameters: {tags: 'early'}, next_run_at: early},
		{...item, id: 11, tags: 'paused', parameters: {tags: 'paused'}, enabled: false, next_run_at: early - 3600},
		{...item, id: 12, tags: 'active', parameters: {tags: 'active'}, active_session_id: 22, next_run_at: 0},
	]);
	await render();
	expect(view.querySelector('span[aria-label="Next run for Artists"] time')?.getAttribute('datetime'))
	.toBe(new Date(early * 1000).toISOString());
	expect(view.querySelector('span[aria-label="Next run for later"] time')?.getAttribute('datetime'))
	.toBe(new Date(later * 1000).toISOString());
	expect(view.querySelector('span[aria-label="Next run for paused"]')?.textContent).toBe('12 files (0 added) Paused');
	expect(view.querySelector('span[aria-label="Next run for active"]')?.textContent).toBe('12 files (0 added) Running');
});

it('shows queued subscriptions with their scheduled time even when it is in the past', async () => {
	const past = 1700000000;
	vi.mocked(api.subscriptions.list).mockResolvedValue([{...item, active_root_id: 8, next_run_at: past}]);
	await render();
	const status = view.querySelector('span[aria-label="Next run for old_artist"]')!;
	expect(status.textContent).toBe(`12 files (0 added) Next: ${new Date(past * 1000).toLocaleString(undefined, {
		dateStyle: 'medium',
		timeStyle: 'short'
	})}`);
	expect(status.querySelector('time')?.getAttribute('datetime')).toBe(new Date(past * 1000).toISOString());
	expect(status.textContent).not.toContain('Queued');
	expect(status.textContent).not.toContain('Running');
});

it('shows past due times and suppresses scheduled times when their group is paused', async () => {
	await render();
	expect(view.querySelector('span[aria-label="Next run for old_artist"] time')?.getAttribute('datetime'))
	.toBe(new Date(item.next_run_at * 1000).toISOString());
	vi.mocked(api.subscriptionGroups.list).mockResolvedValue([{...group, enabled: false}]);
	await act(() => [...view.querySelectorAll('button')].find(button => button.textContent === 'Pause')!.click());
	await act(() => emitSubscriptions());
	expect(view.querySelector('span[aria-label="Next run for Artists"]')?.textContent).toContain('Next run: Paused');
	expect(view.querySelector('span[aria-label="Next run for old_artist"]')?.textContent).toBe('12 files (0 added) Paused');
});


it('pastes newline-delimited searches as a batch, preserving spaces and commas within each row', async () => {
	await render();
	const field = view.querySelector<HTMLInputElement>('input[aria-label="Tags"]')!;
	await act(() => {
		const paste = new Event('paste', {bubbles: true, cancelable: true});
		Object.defineProperty(paste, 'clipboardData', {value: {getData: () => '  artist_one bed  \r\nartist_two,cat ears\n\nartist_three\r'}});
		field.dispatchEvent(paste);
	});
	expect(api.subscriptions.batch).toHaveBeenCalledWith(7, [
		{parameters: {tags: '  artist_one bed  '}, enabled: true},
		{parameters: {tags: 'artist_two,cat ears'}, enabled: true},
		{parameters: {tags: 'artist_three'}, enabled: true},
	]);
	expect(api.subscriptions.create).not.toHaveBeenCalled();
});

it('shows the previous run count as a stat without a files button', async () => {
	vi.mocked(api.subscriptions.list).mockResolvedValue([{...item, session_id: 42, imported_count: 3}]);
	await render();
	expect(view.querySelector('.subscription-items')?.textContent).toContain('12 files (3 added) Next: ');
	expect(view.textContent).not.toContain('Files since last run');
	expect(api.subscriptions.records).not.toHaveBeenCalled();
});

it('keeps other generator defaults when pasting multiple values for one field', async () => {
	const configured = structuredClone(parsers);
	configured[1].handlers[0].parameters.region = {name: 'Region', type: 'string', default: 'west'};
	vi.mocked(api.downloadSessions.parsers).mockResolvedValue({parsers: configured});
	vi.mocked(api.subscriptionGroups.list).mockResolvedValue([{...group, generator: 'other:creator'}]);
	await render();
	const field = view.querySelector<HTMLInputElement>('input[aria-label="Creator"]')!;
	await act(() => {
		const paste = new Event('paste', {bubbles: true, cancelable: true});
		Object.defineProperty(paste, 'clipboardData', {value: {getData: () => 'first\nsecond'}});
		field.dispatchEvent(paste);
	});
	expect(api.subscriptions.batch).toHaveBeenCalledWith(7, [
		{parameters: {name: 'first', region: 'west'}, enabled: true},
		{parameters: {name: 'second', region: 'west'}, enabled: true},
	]);
});

it('keeps the previous run stat visible while another run is active', async () => {
	vi.mocked(api.subscriptions.list).mockResolvedValue([{
		...item,
		active_session_id: 42,
		active_root_id: 8,
		imported_count: 2
	}]);
	await render();
	expect(view.querySelector('.subscription-items')?.textContent).toContain('12 files (2 added) Running');
	expect(view.querySelector('.subscription-items')?.textContent).toContain('Running');
});

it('shows total sizes alongside compact query statuses and keeps group totals', async () => {
	await render();
	expect(view.querySelector('.subscription-group .subscription-group-header')?.textContent).toContain('12 files · 3 MiB');
	expect(view.querySelector('.subscription-items')?.textContent).toContain('12 files (0 added) Next: ');
	expect(view.querySelector('[aria-label="Total size for old_artist"]')?.textContent).toBe(' · 1 MiB');
	expect(view.querySelector('.subscription-group-header [title="3,145,728 bytes"]')).not.toBeNull();
	expect(view.querySelector('.subscription-items [title="1,048,576 bytes (1 MiB)"]')).not.toBeNull();
});


it('uses streamed updates without refetching lists or parsers after commands', async () => {
	await render();
	const calls = vi.mocked(api.subscriptionGroups.list).mock.calls.length;
	await act(() => [...view.querySelectorAll('button')].find(button => button.textContent === 'Pause')!.click());
	expect(api.subscriptionGroups.list).toHaveBeenCalledTimes(calls);
	expect(api.downloadSessions.parsers).toHaveBeenCalledTimes(1);
	await act(() => {
		vi.mocked(watchSubscriptions).mock.lastCall![0].onChange({
			groups: [{...group, enabled: false}], items: [item],
		});
	});
	expect(view.querySelector('span[aria-label="Next run for Artists"]')?.textContent).toContain('Paused');
});
