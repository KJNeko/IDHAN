import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import {api} from '../../api/client';
import type {RateLimitLane} from '../../api/types';
import {DownloadRateLimits} from './DownloadRateLimits';

vi.mock('../../api/client', () => ({api: {downloader: {rateLimits: vi.fn(), enableLane: vi.fn()}}}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

const limited: RateLimitLane = {
	lane: 'gelbooru.com', group: '', requests: 7, seconds: 1, remaining_ms: 29001,
	active: true, backed_off: true, rate_limited: true, rate_limit_hits: 1,
	parser_error_hits: 0,
	concurrency_limit: 2,
	queued: 12, in_flight: 2, dispatch_wait: 'backoff', download_bytes_per_second: 0,
	advertised_limits: 'retry-after=30',
};

it('refreshes reduced bursts, 429 cooldowns and pending requests, retaining history after recovery', async () => {
	vi.useFakeTimers();
	const request = vi.mocked(api.downloader.rateLimits);
	request.mockResolvedValueOnce([limited]).mockResolvedValueOnce([{
		...limited, parser_limited: true, rate_limited: false, parser_error_hits: 1, queued: 8,
	}]).mockResolvedValueOnce([{
		...limited, requests: 6, rate_limit_hits: 2, rate_limited: false, backed_off: false,
		remaining_ms: 750, dispatch_wait: 'rate_limit', queued: 4, spread: true,
	}]).mockRejectedValueOnce(new Error('Disconnected'));
	const view = document.createElement('div');
	const root = createRoot(view);
	try {
		await act(() => root.render(<DownloadRateLimits/>));
		const cells = () => [...view.querySelectorAll('tbody tr td')].map(cell => cell.textContent);
		expect(view.textContent).toContain('gelbooru.com');
		expect(cells()).toEqual(['7 / 1s (burst)', 'HTTP 429 cooldown', '30s', '12', '2 / 2', '1', '0', 'retry-after=30', '']);
		await act(() => vi.advanceTimersByTimeAsync(1000));
		expect(cells()).toEqual(['7 / 1s (burst)', 'Parser error cooldown', '30s', '8', '2 / 2', '1', '1', 'retry-after=30', '']);
		await act(() => vi.advanceTimersByTimeAsync(1000));
		expect(cells()).toEqual(['6 / 1s (spread)', 'Waiting for rate allowance', '1s', '4', '2 / 2', '2', '0', 'retry-after=30', '']);
		await act(() => vi.advanceTimersByTimeAsync(1000));
		expect(view.querySelector('[role="alert"]')!.textContent).toContain('Disconnected — showing last received values.');
		expect(cells()[0]).toBe('6 / 1s (spread)');
		await act(() => root.unmount());
		expect(request.mock.lastCall![0]!.aborted).toBe(true);
		await act(() => vi.advanceTimersByTimeAsync(1000));
		expect(request).toHaveBeenCalledTimes(4);
	} finally {
		vi.useRealTimers();
	}
});

it('requires an explicit enable action to reopen a disabled lane', async () => {
	const request = vi.mocked(api.downloader.rateLimits);
	request.mockReset();
	request.mockResolvedValueOnce([{...limited, disabled: true, remaining_ms: 0}])
	.mockResolvedValueOnce([{...limited, disabled: false, backed_off: false, rate_limited: false}]);
	vi.mocked(api.downloader.enableLane).mockResolvedValue({reset: limited.lane});
	const view = document.createElement('div');
	const root = createRoot(view);
	try {
		await act(() => root.render(<DownloadRateLimits/>));
		expect(view.textContent).toContain('Disabled after repeated HTTP 429');
		const button = view.querySelector<HTMLButtonElement>('[aria-label="Enable lane gelbooru.com"]')!;
		expect(button).not.toBeNull();
		await act(() => button.click());
		expect(api.downloader.enableLane).toHaveBeenCalledExactlyOnceWith('gelbooru.com');
		expect(view.textContent).not.toContain('Disabled after repeated HTTP 429');
		expect(view.querySelector('[aria-label="Enable lane gelbooru.com"]')).toBeNull();
	} finally {
		await act(() => root.unmount());
	}
});
