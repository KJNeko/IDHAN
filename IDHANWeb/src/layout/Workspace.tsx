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
import { useLayoutStore } from './store';

const DEFAULT_SEED_PANEL = 'server-status';

export function Workspace() {
  const generation = useLayoutStore((s) => s.generation);
  const setApi = useLayoutStore((s) => s.setApi);
  const setEngineTree = useLayoutStore((s) => s.setEngineTree);
  const [mode, setMode] = useState(() => globalServices.theme.mode());

  useEffect(() => globalServices.theme.subscribe(setMode), []);

  // Recomputed on layout swap; includes tombstones for any unknown panel types the document references.
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
  }, [generation]);

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
        useLayoutStore.getState().addPanel(DEFAULT_SEED_PANEL);
      }

      api.onDidLayoutChange(() => setEngineTree(api.toJSON()));
    },
    [setApi, setEngineTree],
  );

  return (
    <div className="workspace">
      <DockviewReact
        key={generation}
        components={components}
        onReady={onReady}
        theme={mode === 'light' ? themeLight : themeAbyss}
      />
    </div>
  );
}
