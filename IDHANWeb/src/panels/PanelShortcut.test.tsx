import {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import {PanelShortcut} from './PanelShortcut';

const state = vi.hoisted(() => ({
	doc: {panels: {} as Record<string, { type: string }>},
	api: {getPanel: vi.fn()},
	addPanel: vi.fn(),
}));
vi.mock('../layout/store', () => ({useLayoutStore: {getState: () => state}}));
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it.each([false, true])('opens a destination and reuses an existing panel: %s', async exists => {
	const activate = vi.fn();
	state.doc.panels = exists ? {search1: {type: 'search'}} : {};
	state.api.getPanel.mockReset().mockReturnValue(exists ? {api: {setActive: activate}} : undefined);
	state.addPanel.mockClear();
	const view = document.createElement('div');
	const root = createRoot(view);
	try {
		await act(() => root.render(<PanelShortcut type="search">Search files</PanelShortcut>));
		await act(() => view.querySelector('button')!.click());
		if (exists) {
			expect(activate).toHaveBeenCalledOnce();
			expect(state.addPanel).not.toHaveBeenCalled();
		} else {
			expect(state.addPanel).toHaveBeenCalledWith('search');
		}
	} finally {
		await act(() => root.unmount());
	}
});
