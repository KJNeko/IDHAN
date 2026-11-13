import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {EmbeddingTermInput} from './EmbeddingTermInput';

Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

describe('embedding term controls', () => {
	let view: HTMLDivElement;
	let root: Root;
	const onAdd = vi.fn();
	beforeEach(() => {
		view = document.createElement('div');
		root = createRoot(view);
		onAdd.mockClear();
	});
	afterEach(async () => {
		await act(() => root.unmount());
	});

	async function input(value: string) {
		await act(() => {
			const field = view.querySelector('input')!;
			Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(field, value);
			field.dispatchEvent(new Event('input', {bubbles: true}));
		});
	}

	it('adds literal text without interpreting weight or record syntax', async () => {
		await act(() => root.render(<EmbeddingTermInput textEnabled onAdd={onAdd}/>));
		await input('-record:1234:0.5');
		await act(() => view.querySelector('button')!.click());
		expect(onAdd).toHaveBeenCalledWith({kind: 'text', text: '-record:1234:0.5'});
		expect(view.querySelector('input')!.value).toBe('');
	});
	it('offers numeric record input for image-only models and validates IDs', async () => {
		await act(() => root.render(<EmbeddingTermInput textEnabled={false} onAdd={onAdd}/>));
		expect(view.querySelector('select')!.value).toBe('record');
		expect(view.querySelector('option[value=text]')?.hasAttribute('disabled')).toBe(true);
		for (const value of ['0', '-2', '3.5', '2147483648']) {
			await input(value);
			expect(view.querySelector('button')!.disabled).toBe(true);
		}
		await input('1234');
		await act(() => view.querySelector('button')!.click());
		expect(onAdd).toHaveBeenCalledWith({kind: 'record', recordId: 1234});
	});
	it('clears text when switching term types', async () => {
		await act(() => root.render(<EmbeddingTermInput textEnabled onAdd={onAdd}/>));
		await input('cat');
		await act(() => {
			const select = view.querySelector('select')!;
			select.value = 'record';
			select.dispatchEvent(new Event('change', {bubbles: true}));
		});
		expect(view.querySelector('input')!.type).toBe('number');
		expect(view.querySelector('input')!.value).toBe('');
		expect(view.querySelector('button')!.disabled).toBe(true);
	});
});
