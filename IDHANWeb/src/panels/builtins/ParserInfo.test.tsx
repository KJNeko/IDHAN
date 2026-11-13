import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import {ParserInfo} from './ParserInfo';

const parsers = vi.hoisted(() => vi.fn());
vi.mock('../../api/client', () => ({api: {downloadSessions: {parsers}}}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('loads parser versions and offers the installed bundle for download', async () => {
	parsers.mockResolvedValue({
		parsers: [{
			id: 'example', filename: 'example.js', version: 2,
			sdkVersion: '1.0.0', options: [], handlers: [], discoveries: [], urlClasses: []
		}]
	});
	const view = document.createElement('div');
	const root = createRoot(view);
	try {
		await act(() => root.render(<ParserInfo/>));
		await act(() => {
			const details = view.querySelector('details')!;
			details.open = true;
			details.dispatchEvent(new Event('toggle'));
		});
		expect(view.textContent).toContain('example · version 2 · SDK 1.0.0');
		expect(view.textContent).toContain('Download parser');
		expect(view.textContent).toContain('Replace existing filename');
	} finally {
		await act(() => root.unmount());
	}
});
