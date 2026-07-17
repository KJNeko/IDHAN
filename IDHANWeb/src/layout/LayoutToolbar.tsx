/**
 * Top bar: layout name, add-panel picker, edit-mode toggle, the named-layout menu, and sign-out.
 * Named layouts are localStorage snapshots for now; server push/pull lands in M5.
 */

import { type ChangeEvent } from 'react';
import { listPanels } from '../panels/registry';
import { useLayoutStore } from './store';

export function LayoutToolbar({ onSignOut }: { onSignOut: () => void }) {
  const name = useLayoutStore((s) => s.doc.name);
  const editMode = useLayoutStore((s) => s.editMode);
  const savedLayouts = useLayoutStore((s) => s.savedLayouts);
  const activeId = useLayoutStore((s) => s.doc.id);

  const store = useLayoutStore.getState();

  function onAddPanel(event: ChangeEvent<HTMLSelectElement>) {
    const type = event.target.value;
    if (type) store.addPanel(type);
    event.target.value = '';
  }

  function onNewLayout() {
    const chosen = window.prompt('Name for the new layout:', 'Untitled');
    if (chosen) store.newLayout(chosen.trim() || 'Untitled');
  }

  return (
    <header className="toolbar">
      <span className="toolbar-brand">IDHAN</span>

      <input
        className="toolbar-name"
        value={name}
        onChange={(e) => store.renameLayout(e.target.value)}
        aria-label="Layout name"
      />

      <select className="toolbar-add" value="" onChange={onAddPanel} aria-label="Add panel">
        <option value="">Add panel…</option>
        {listPanels().map((panel) => (
          <option key={panel.type} value={panel.type}>
            {panel.title}
          </option>
        ))}
      </select>

      <button type="button" className="toolbar-button" onClick={() => store.toggleEditMode()}>
        {editMode ? 'Done' : 'Edit'}
      </button>

      <details className="toolbar-menu">
        <summary className="toolbar-button">Layouts</summary>
        <div className="toolbar-dropdown">
          <button type="button" className="dropdown-item" onClick={() => store.saveNamedLayout()}>
            Save “{name}”
          </button>
          <button type="button" className="dropdown-item" onClick={onNewLayout}>
            New layout…
          </button>
          {savedLayouts.length > 0 && <div className="dropdown-divider" />}
          {savedLayouts.map((layout) => (
            <div key={layout.id} className={`dropdown-row${layout.id === activeId ? ' is-active' : ''}`}>
              <button type="button" className="dropdown-item grow" onClick={() => store.loadNamedLayout(layout.id)}>
                {layout.name}
              </button>
              <button
                type="button"
                className="dropdown-item delete"
                aria-label={`Delete ${layout.name}`}
                onClick={() => store.deleteNamedLayout(layout.id)}
              >
                ✕
              </button>
            </div>
          ))}
        </div>
      </details>

      <button type="button" className="toolbar-button ghost" onClick={onSignOut}>
        Sign out
      </button>
    </header>
  );
}
