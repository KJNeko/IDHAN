/**
 * The dockview surface. Restores the working document's tree on ready (or seeds a default panel for a
 * fresh layout), then persists the serialized tree back to the store on every layout change. Remounts
 * only when the whole layout is swapped (generation bump), never on an incremental edit.
 */

import { useCallback, useEffect, useMemo, useState } from 'react';
import { DockviewReact, themeAbyss, themeLight, type DockviewReadyEvent } from 'dockview-react';
import 'dockview/dist/styles/dockview.css';
import { globalServices } from '../host/globalServices';
import { hasPanel } from '../panels/registry';
import { buildComponents } from './panelComponents';
import { useLayoutStore, type PanelPosition } from './store';

/**
 * The workspace a fresh install boots into: Search on the left, the Grid filling the centre, and a
 * right column with the Media Viewer over a Record Info / Tag Editor tab pair. Only seeded when there
 * is no saved tree; users reshape it freely and it persists.
 */
function seedDefaultLayout(add: (type: string, position?: PanelPosition) => string | null): void {
  const search = add('search');
  const grid = add('grid', search ? { referencePanel: search, direction: 'right' } : undefined);
  const viewer = add('media-viewer', grid ? { referencePanel: grid, direction: 'right' } : undefined);
  const info = add('record-info', viewer ? { referencePanel: viewer, direction: 'below' } : undefined);
  if (info) add('tag-editor', { referencePanel: info, direction: 'within' });
}

export function Workspace() {
  const generation = useLayoutStore((s) => s.generation);
  // A catalog bump (plugins finished loading) rebuilds components so newly-registered types render
  // instead of showing as tombstones. Remounting is fine — the tree is persisted and restored on ready.
  const catalogVersion = useLayoutStore((s) => s.catalogVersion);
  const setApi = useLayoutStore((s) => s.setApi);
  const setEngineTree = useLayoutStore((s) => s.setEngineTree);
  const [mode, setMode] = useState(() => globalServices.theme.mode());

  useEffect(() => globalServices.theme.subscribe(setMode), []);

  // Recomputed on layout swap or catalog change; includes tombstones for still-unknown panel types.
  const components = useMemo(() => {
    const { doc } = useLayoutStore.getState();
    const unknown = [
      ...new Set(
        Object.values(doc.panels)
          .map((panel) => panel.type)
          .filter((type) => !hasPanel(type)),
      ),
    ];
    return buildComponents(unknown);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [generation, catalogVersion]);

  const onReady = useCallback(
    (event: DockviewReadyEvent) => {
      const api = event.api;
      setApi(api);

      const { doc } = useLayoutStore.getState();
      if (doc.engine.tree) {
        try {
          api.fromJSON(doc.engine.tree);
        } catch {
          // Corrupt/incompatible tree: start clean rather than crash.
        }
      }
      if (api.panels.length === 0) {
        seedDefaultLayout(useLayoutStore.getState().addPanel);
      }

      api.onDidLayoutChange(() => setEngineTree(api.toJSON()));
    },
    [setApi, setEngineTree],
  );

  return (
    <div className="workspace">
      <DockviewReact
        key={`${generation}.${catalogVersion}`}
        components={components}
        onReady={onReady}
        theme={mode === 'light' ? themeLight : themeAbyss}
      />
    </div>
  );
}
