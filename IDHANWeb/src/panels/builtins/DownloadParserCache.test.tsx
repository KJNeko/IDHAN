import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import {api} from '../../api/client';
import {DownloadParserCache} from './DownloadParserCache';

vi.mock('../../api/client', () => ({api: {downloader: {cache: vi.fn()}}}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('polls hit totals and ranked keys, retains the last snapshot on failure, and stops on unmount', async () => {
	vi.useFakeTimers();
	const request = vi.mocked(api.downloader.cache);
	const hot = {parser: 'gelbooru', key: 'tag-type:cat', hits: 12, lifetime_seconds: 86400, expires_in_seconds: 86000};
	request.mockResolvedValueOnce({
		hits: 12, misses: 4, hit_rate: 0.75, entry_count: 2, entries: [
			hot, {...hot, key: 'tag-type:dog', hits: 0},
		]
	}).mockResolvedValueOnce({
		hits: 13, misses: 4, hit_rate: 13 / 17, entry_count: 1, entries: [
			{...hot, hits: 13},
		]
	}).mockRejectedValueOnce(new Error('Disconnected'));
	const view = document.createElement('div');
	const root = createRoot(view);
	try {
		await act(() => root.render(<DownloadParserCache/>));
		expect(view.textContent).toContain('12 hits · 4 misses · 75.0% hit rate · 2 cached keys');
		const rows = () => [...view.querySelectorAll('tbody tr')].map(row => [...row.children].map(cell => cell.textContent));
		expect(rows()[0]).toEqual(['gelbooru', 'tag-type:cat', '12', '86,400s', '86,000s']);
		expect(rows()[1]![1]).toBe('tag-type:dog');
		await act(() => vi.advanceTimersByTimeAsync(2000));
		expect(view.textContent).toContain('13 hits · 4 misses · 76.5% hit rate · 1 cached key');
		expect(rows()).toHaveLength(1);
		await act(() => vi.advanceTimersByTimeAsync(2000));
		expect(view.querySelector('[role="alert"]')!.textContent).toContain('Disconnected — showing last received values.');
		expect(rows()[0]![2]).toBe('13');
		await act(() => root.unmount());
		expect(request.mock.lastCall![0]!.aborted).toBe(true);
		await act(() => vi.advanceTimersByTimeAsync(2000));
		expect(request).toHaveBeenCalledTimes(3);
	} finally {
		vi.useRealTimers();
	}
});
