/**
 * Top bar: layout name, add-panel picker, edit-mode toggle, the named-layout menu, and sign-out.
 * The menu covers both localStorage snapshots ("Saved") and server-stored layouts ("On server",
 * push/pull); localStorage remains the source of truth and the server copy is opt-in.
 */

import {type ChangeEvent, type SyntheticEvent, useMemo, useRef, useState} from 'react';
import { ApiError } from '../api/client';
import type {PanelDefinition} from '../host/types';
import { listPanels } from '../panels/registry';
import { useLayoutStore } from './store';

const PANEL_GROUPS = [
    {title: 'Browse', types: ['search', 'grid', 'media-viewer']},
    {title: 'Record details', types: ['record-info', 'file-relationships', 'tag-editor', 'url-list', 'notes']},
    {title: 'Tags', types: ['tag-domain-manager', 'tag-relationships']},
    {title: 'Import & jobs', types: ['import', 'job-monitor']},
    {title: 'Embeddings', types: ['embedding-search', 'embedding-compare', 'embeddings']},
    {title: 'Administration', types: ['cluster-manager', 'sunburst-stats', 'log-viewer', 'server-status']},
    {title: 'Developer', types: ['selection-inspector', 'debug']},
] as const;

function groupPanels(panels: PanelDefinition[]): Array<{ title: string; panels: PanelDefinition[] }> {
    const byType = new Map(panels.map((panel) => [panel.type, panel]));
    const groups: Array<{ title: string; panels: PanelDefinition[] }> = PANEL_GROUPS.map((group) => ({
        title: group.title,
        panels: group.types.flatMap((type) => {
            const panel = byType.get(type);
            if (!panel) return [];
            byType.delete(type);
            return [panel];
        }),
    })).filter((group) => group.panels.length > 0);

    const extensions = [...byType.values()].sort((left, right) => left.title.localeCompare(right.title));
    if (extensions.length > 0) groups.push({title: 'Extensions', panels: extensions});
    return groups;
}

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
  const catalogVersion = useLayoutStore((s) => s.catalogVersion);
    const placedPanels = useLayoutStore((s) => s.doc.panels);
    const [panelQuery, setPanelQuery] = useState('');
    const panelMenu = useRef<HTMLDetailsElement>(null);
    const panelSearch = useRef<HTMLInputElement>(null);

  const store = useLayoutStore.getState();

  // Re-read the registry when the catalog changes (a plugin registered new panel types).
  const panels = useMemo(() => listPanels(), [catalogVersion]);
    const openPanelTypes = useMemo(() => new Set(Object.values(placedPanels).map((panel) => panel.type)), [placedPanels]);
    const panelGroups = useMemo(() => {
        const query = panelQuery.trim().toLocaleLowerCase();
        const filtered = query
            ? panels.filter((panel) =>
                [panel.title, panel.description ?? '', panel.type].some((value) =>
                    value.toLocaleLowerCase().includes(query),
                ),
            )
            : panels;
        return groupPanels(filtered);
    }, [panelQuery, panels]);

    function onAddPanel(type: string) {
        store.addPanel(type);
        setPanelQuery('');
        if (panelMenu.current) panelMenu.current.open = false;
    }

    function onPanelMenuToggle(event: SyntheticEvent<HTMLDetailsElement>) {
        if (event.currentTarget.open) window.requestAnimationFrame(() => panelSearch.current?.focus());
        else setPanelQuery('');
  }

  function onNewLayout() {
    const chosen = window.prompt('Name for the new layout:', 'Untitled');
    if (chosen) store.newLayout(chosen.trim() || 'Untitled');
  }

  // Download the working document as a JSON file so it can be moved without a server round-trip.
  function onExport() {
    const doc = useLayoutStore.getState().doc;
    const blob = new Blob([JSON.stringify(doc, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = `${doc.name || 'layout'}.idhan-layout.json`;
    anchor.click();
    URL.revokeObjectURL(url);
  }

  function onImport(event: ChangeEvent<HTMLInputElement>) {
    const file = event.target.files?.[0];
    event.target.value = ''; // let the same file be picked again after a failure
    if (!file) return;
    file
      .text()
      .then((text) => {
        let parsed: unknown;
        try {
          parsed = JSON.parse(text);
        } catch {
          window.alert('That file is not valid JSON.');
          return;
        }
        if (!store.importLayout(parsed)) window.alert('That file is not a readable IDHAN layout.');
      })
      .catch(() => window.alert('Could not read the file.'));
  }

  // Refresh the server list when the menu opens, so it reflects what other browsers have pushed.
  function onMenuToggle(event: SyntheticEvent<HTMLDetailsElement>) {
    if (event.currentTarget.open) run(store.refreshServerLayouts());
  }

  return (
    <header className="toolbar">
      <span className="toolbar-brand">IDHAN</span>

        <span className="toolbar-notice" role="note" title="Development UI, Not for real usage. Vibecoded slop">
        Development UI, Not for real usage. Vibecoded slop
      </span>

      <input
        className="toolbar-name"
        value={name}
        onChange={(e) => store.renameLayout(e.target.value)}
        aria-label="Layout name"
      />

        <details className="toolbar-menu panel-picker" ref={panelMenu} onToggle={onPanelMenuToggle}>
            <summary className="toolbar-button">Add panel <span aria-hidden="true">▾</span></summary>
            <div className="toolbar-dropdown panel-picker-dropdown">
                <div className="panel-picker-head">
                    <strong>Add a panel</strong>
                    <span>Choose a tool for this layout</span>
                </div>
                <input
                    ref={panelSearch}
                    className="panel-picker-search"
                    type="search"
                    value={panelQuery}
                    onChange={(event) => setPanelQuery(event.target.value)}
                    placeholder="Search panels…"
                    aria-label="Search panels"
                />
                <div className="panel-picker-list">
                    {panelGroups.map((group) => (
                        <section className="panel-picker-group" key={group.title}>
                            <h2>{group.title}</h2>
                            {group.panels.map((panel) => {
                                const alreadyOpen = panel.singleton && openPanelTypes.has(panel.type);
                                return (
                                    <button
                                        type="button"
                                        className="panel-picker-item"
                                        key={panel.type}
                                        onClick={() => onAddPanel(panel.type)}
                                    >
                      <span className="panel-picker-item-copy">
                        <strong>{panel.title}</strong>
                        <span>{panel.description ?? 'Add this panel to the current layout.'}</span>
                      </span>
                                        {alreadyOpen && <span className="panel-picker-open">Open</span>}
                                    </button>
                                );
                            })}
                        </section>
                    ))}
                    {panelGroups.length === 0 && <p className="panel-picker-empty">No panels match “{panelQuery}”.</p>}
                </div>
            </div>
        </details>

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
          <button type="button" className="dropdown-item" onClick={onExport}>
            Export to file…
          </button>
          <label className="dropdown-item">
            Import from file…
            <input type="file" accept="application/json,.json" onChange={onImport} hidden />
          </label>
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
