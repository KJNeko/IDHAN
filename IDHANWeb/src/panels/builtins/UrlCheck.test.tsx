import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import {UrlCheck} from './UrlCheck';

const checkUrl = vi.hoisted(() => vi.fn());
vi.mock('../../api/client', () => ({api: {downloadSessions: {checkUrl}}}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('looks up a URL, opens associated records, and clears old results on edits', async () => {
	checkUrl.mockResolvedValue({normalized_url: 'https://example.com/file', known_direct: true, record_ids: [42, 43]});
	const onShowRecords = vi.fn();
	const view = document.createElement('div');
	const root = createRoot(view);
	try {
		await act(() => root.render(<UrlCheck onShowRecords={onShowRecords}/>));
		const input = view.querySelector('input')!;
		await act(() => {
			Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, 'https://example.com/file');
			input.dispatchEvent(new Event('input', {bubbles: true}));
		});
		await act(() => view.querySelector('form')!.dispatchEvent(new Event('submit', {
			bubbles: true,
			cancelable: true
		})));
		expect(checkUrl).toHaveBeenCalledWith('https://example.com/file');
		expect(view.textContent).toContain('Known direct URL: Yes');
		expect(view.textContent).toContain('#42, #43');
		await act(() => [...view.querySelectorAll('button')].find(button => button.textContent?.trim() === 'Show records')!.click());
		expect(onShowRecords).toHaveBeenCalledWith([42, 43], 'URL: https://example.com/file');
		await act(() => {
			Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, 'https://example.com/other');
			input.dispatchEvent(new Event('input', {bubbles: true}));
		});
		expect(view.textContent).not.toContain('#42');
		checkUrl.mockResolvedValue({normalized_url: 'https://example.com/other', known_direct: false, record_ids: []});
		await act(() => view.querySelector('form')!.dispatchEvent(new Event('submit', {
			bubbles: true,
			cancelable: true
		})));
		expect(view.textContent).toContain('Known direct URL: No');
		expect(view.textContent).toContain('No associated records.');
		checkUrl.mockRejectedValue(new Error('Lookup unavailable'));
		await act(() => view.querySelector('form')!.dispatchEvent(new Event('submit', {
			bubbles: true,
			cancelable: true
		})));
		expect(view.querySelector('[role="alert"]')?.textContent).toBe('Lookup unavailable');
		expect(view.textContent).not.toContain('No associated records.');
	} finally {
		await act(() => root.unmount());
	}
});
