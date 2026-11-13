import type {ReactNode} from 'react';
import {useLayoutStore} from '../layout/store';

export function PanelShortcut({type, children}: { type: string; children: ReactNode }) {
	function open() {
		const store = useLayoutStore.getState();
		const existing = Object.entries(store.doc.panels).find(([, panel]) => panel.type === type);
		const panel = existing ? store.api?.getPanel(existing[0]) : undefined;
		if (panel) {
			panel.api.setActive();
			return;
		}
		store.addPanel(type);
	}

	return <button type="button" className="toolbar-button" onClick={open}>
		{children}
	</button>;
}
