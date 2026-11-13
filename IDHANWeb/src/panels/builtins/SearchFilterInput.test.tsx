import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {SearchFilterInput} from './SearchFilterInput';

vi.mock('../../host/mimeCatalogue', () => ({
	peekMimes: () => ({entries: [], names: []}),
	loadMimes: async () => ({entries: [{id: 1, name: 'image/png'}], names: [{id: 1, name: 'image/png'}]}),
}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

describe('search filter controls', () => {
	let view: HTMLDivElement;
	let root: Root;
	const onAdd = vi.fn();
	beforeEach(async () => {
		view = document.createElement('div');
		root = createRoot(view);
		onAdd.mockClear();
		await act(() => root.render(<SearchFilterInput onAdd={onAdd}/>));
	});
	afterEach(async () => {
		await act(() => root.unmount());
	});

	async function select(label: string, value: string) {
		await act(() => {
			const element = view.querySelector<HTMLSelectElement>(`[aria-label="${label}"]`)!;
			element.value = value;
			element.dispatchEvent(new Event('change', {bubbles: true}));
		});
	}

	async function input(label: string, value: string) {
		await act(() => {
			const element = view.querySelector<HTMLInputElement>(`[aria-label="${label}"]`)!;
			Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(element, value);
			element.dispatchEvent(new Event('input', {bubbles: true}));
		});
	}

	it('builds a size filter from labeled controls', async () => {
		await select('Filter', 'system:filesize');
		await select('Comparison', '>');
		await input('Filter value', '2');
		await select('Units', '1048576');
		await act(() => view.querySelector('button')!.click());
		expect(onAdd).toHaveBeenCalledWith('system:filesize > 2097152');
	});
	it('adds flag filters without an argument', async () => {
		await select('Filter', 'system:has audio');
		expect(view.querySelector('input')).toBeNull();
		await act(() => view.querySelector('button')!.click());
		expect(onAdd).toHaveBeenCalledWith('system:has audio');
	});
	it('offers file types instead of requiring MIME syntax', async () => {
		await select('Filter', 'system:mime');
		await select('File type', 'image/png');
		await act(() => view.querySelector('button')!.click());
		expect(onAdd).toHaveBeenCalledWith('system:mime = image/png');
	});
	it('requires a valid reference and hash distance', async () => {
		await select('Filter', 'system:nearby');
		expect(view.querySelector('button')!.disabled).toBe(true);
		await input('Record ID', '12');
		await input('Hash distance', '65');
		expect(view.querySelector('button')!.disabled).toBe(true);
		await input('Hash distance', '8');
		await act(() => view.querySelector('button')!.click());
		expect(onAdd).toHaveBeenCalledWith('system:nearby 12 distance 8');
	});
});
