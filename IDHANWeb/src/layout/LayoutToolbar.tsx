/**
 * Top bar: layout name, add-panel picker, edit-mode toggle, the named-layout menu, and sign-out.
 * The menu covers both localStorage snapshots ("Saved") and server-stored layouts ("On server",
 * push/pull); localStorage remains the source of truth and the server copy is opt-in.
 */

import { type ChangeEvent, type SyntheticEvent } from 'react';
import { ApiError } from '../api/client';
import { listPanels } from '../panels/registry';
import { useLayoutStore } from './store';

/** Runs a store server-action and surfaces any failure without leaving the promise unhandled. */
function run(promise: Promise<unknown>): void {
  promise.catch((error: unknown) => {
    const message =
      error instanceof ApiError
        ? `${error.message}${error.status === 409 ? ' (name already taken on the server)' : ''}`
        : error instanceof Error
          ? error.message
          : 'Unknown error';
    window.alert(`Layout sync failed: ${message}`);
  });
}

export function LayoutToolbar({ onSignOut }: { onSignOut: () => void }) {
  const name = useLayoutStore((s) => s.doc.name);
  const editMode = useLayoutStore((s) => s.editMode);
  const savedLayouts = useLayoutStore((s) => s.savedLayouts);
  const serverLayouts = useLayoutStore((s) => s.serverLayouts);
  const serverBusy = useLayoutStore((s) => s.serverBusy);
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

  // Refresh the server list when the menu opens, so it reflects what other browsers have pushed.
  function onMenuToggle(event: SyntheticEvent<HTMLDetailsElement>) {
    if (event.currentTarget.open) run(store.refreshServerLayouts());
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

      <details className="toolbar-menu" onToggle={onMenuToggle}>
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

          <div className="dropdown-divider" />
          <div className="dropdown-heading">On server</div>
          <button
            type="button"
            className="dropdown-item"
            disabled={serverBusy}
            onClick={() => run(store.pushLayoutToServer())}
          >
            Push “{name}” to server
          </button>
          {serverLayouts.map((layout) => (
            <div key={layout.id} className={`dropdown-row${layout.id === activeId ? ' is-active' : ''}`}>
              <button
                type="button"
                className="dropdown-item grow"
                disabled={serverBusy}
                onClick={() => run(store.pullLayoutFromServer(layout.id))}
              >
                {layout.name}
              </button>
              <button
                type="button"
                className="dropdown-item delete"
                disabled={serverBusy}
                aria-label={`Delete ${layout.name} from server`}
                onClick={() => run(store.deleteServerLayout(layout.id))}
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
