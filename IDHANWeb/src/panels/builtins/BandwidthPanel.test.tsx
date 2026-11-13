import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import type {HostApi} from '../../host/types';
import type {BandwidthNode} from '../../api/types';
import {BandwidthPanel, bandwidthPath, bandwidthRows} from './BandwidthPanel';

Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});
const nodes: BandwidthNode[] = [
	{
		host: 'gelbooru.com', bytes_received: 1024, children: [
			{host: 'img4.gelbooru.com', bytes_received: 2048, children: []},
		]
	},
	{
		host: 'example.com', bytes_received: 0, children: [
			{host: 'cdn.example.com', bytes_received: 1024, children: []},
		]
	},
];

function json(value: unknown, status = 200) {
	return new Response(JSON.stringify(value), {status, headers: {'Content-Type': 'application/json'}});
}

describe('bandwidth data', () => {
	it('includes the current second and leaves all-time queries unbounded', () => {
		expect(bandwidthPath('3600', 1_700_000_000_500)).toBe('/bandwidth?start=1699996401&end=1700000001');
		expect(bandwidthPath('all')).toBe('/bandwidth');
	});
	it('preserves exact-host totals and hides only descendants when collapsed', () => {
		expect(bandwidthRows(nodes).map(({node, depth}) => [node.host, node.bytes_received, depth])).toEqual([
			['gelbooru.com', 1024, 0], ['img4.gelbooru.com', 2048, 1],
			['example.com', 0, 0], ['cdn.example.com', 1024, 1],
		]);
		expect(bandwidthRows(nodes, new Set(['gelbooru.com'])).map(({node}) => node.host))
		.toEqual(['gelbooru.com', 'example.com', 'cdn.example.com']);
	});
});

describe('Bandwidth panel', () => {
	let view: HTMLDivElement;
	let root: Root;
	let fetch: ReturnType<typeof vi.fn>;
	let host: HostApi;
	beforeEach(() => {
		vi.useFakeTimers();
		view = document.createElement('div');
		document.body.append(view);
		root = createRoot(view);
		fetch = vi.fn().mockImplementation(async () => json(nodes));
		host = {http: {fetch}} as unknown as HostApi;
	});
	afterEach(async () => {
		await act(() => root.unmount());
		view.remove();
		vi.useRealTimers();
	});

	async function mount() {
		await act(() => root.render(<BandwidthPanel host={host}/>));
	}

	it('keeps parent bytes separate and totals unchanged when collapsing children', async () => {
		await mount();
		expect(view.querySelector('.bandwidth-total')?.textContent).toBe('4 KiB');
		expect(view.textContent).toContain('3 hosts with traffic');
		expect(view.querySelector('tbody tr td')?.textContent).toBe('1 KiB');
		await act(() => view.querySelector<HTMLButtonElement>('[aria-label="Collapse gelbooru.com"]')!.click());
		expect(view.querySelector('tbody')?.textContent).not.toContain('img4.gelbooru.com');
		expect(view.querySelector('.bandwidth-total')?.textContent).toBe('4 KiB');
		await act(() => view.querySelector<HTMLButtonElement>('[aria-label="Expand gelbooru.com"]')!.click());
		expect(view.querySelector('tbody')?.textContent).toContain('img4.gelbooru.com');
	});
	it('polls every ten seconds and cancels on unmount', async () => {
		await mount();
		expect(fetch).toHaveBeenCalledTimes(1);
		await act(() => vi.advanceTimersByTimeAsync(10_000));
		expect(fetch).toHaveBeenCalledTimes(2);
		const signal = fetch.mock.calls[1]![1].signal as AbortSignal;
		await act(() => root.unmount());
		expect(signal.aborted).toBe(true);
		await act(() => vi.advanceTimersByTimeAsync(20_000));
		expect(fetch).toHaveBeenCalledTimes(2);
		root = createRoot(view);
	});
	it('does not overlap slow polls', async () => {
		fetch.mockImplementation(() => new Promise(() => {
		}));
		await mount();
		await act(() => vi.advanceTimersByTimeAsync(30_000));
		expect(fetch).toHaveBeenCalledTimes(1);
	});
	it('ignores a stale response after the timeframe changes', async () => {
		let finishOld: (response: Response) => void = () => {
		};
		fetch.mockImplementationOnce(() => new Promise<Response>((resolve) => {
			finishOld = resolve;
		}));
		await mount();
		const signal = fetch.mock.calls[0]![1].signal as AbortSignal;
		const select = view.querySelector('select')!;
		await act(() => {
			select.value = 'all';
			select.dispatchEvent(new Event('change', {bubbles: true}));
		});
		expect(signal.aborted).toBe(true);
		expect(fetch.mock.calls[1]![0]).toBe('/bandwidth');
		await act(() => finishOld(json([{host: 'stale.test', bytes_received: 10, children: []}])));
		expect(view.textContent).not.toContain('stale.test');
		expect(view.querySelector('.bandwidth-total')?.textContent).toBe('4 KiB');
	});
	it('supports manual refresh while automatic refresh is paused', async () => {
		await mount();
		await act(() => view.querySelector<HTMLInputElement>('input[type="checkbox"]')!.click());
		const calls = fetch.mock.calls.length;
		await act(() => vi.advanceTimersByTimeAsync(20_000));
		expect(fetch).toHaveBeenCalledTimes(calls);
		await act(() => [...view.querySelectorAll('button')].find((button) => button.textContent === 'Refresh')!.click());
		expect(fetch).toHaveBeenCalledTimes(calls + 1);
	});
	it('shows empty results and reports server errors', async () => {
		fetch.mockImplementationOnce(async () => json([]));
		await mount();
		expect(view.textContent).toContain('No bandwidth recorded for this timeframe.');
		fetch.mockImplementationOnce(async () => json({error: 'Database unavailable'}, 503));
		await act(() => vi.advanceTimersByTimeAsync(10_000));
		expect(view.querySelector('[role="alert"]')?.textContent).toContain('Database unavailable');
	});
});
