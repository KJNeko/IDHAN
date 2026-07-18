import { beforeEach, describe, expect, it } from 'vitest';
import type { DockviewApi } from 'dockview-react';
import { useLayoutStore } from './store';
import { createEmptyLayout, type SerializedDockview } from './document';
import { registerBuiltinPanels } from '../panels/builtins';

registerBuiltinPanels();

interface AddedPanel {
  id: string;
  component: string;
}

/** Minimal DockviewApi stand-in: records addPanel calls; the rest of the surface the store touches. */
function makeFakeApi(): { api: DockviewApi; added: AddedPanel[] } {
  const added: AddedPanel[] = [];
  const api = {
    addPanel: (opts: { id: string; component: string }) => {
      added.push({ id: opts.id, component: opts.component });
      return { id: opts.id };
    },
    getPanel: () => undefined,
    get panels() {
      return added.map((a) => ({ id: a.id }));
    },
  } as unknown as DockviewApi;
  return { api, added };
}

beforeEach(() => {
  localStorage.clear();
  useLayoutStore.setState({ doc: createEmptyLayout('Default'), api: null, generation: 0, savedLayouts: [] });
});

describe('layout store', () => {
  it('addPanel seeds default config and adds to the engine', () => {
    const { api, added } = makeFakeApi();
    useLayoutStore.getState().setApi(api);

    useLayoutStore.getState().addPanel('selection-inspector');

    expect(added).toHaveLength(1);
    expect(added[0]?.component).toBe('selection-inspector');

    const panels = Object.values(useLayoutStore.getState().doc.panels);
    expect(panels).toHaveLength(1);
    expect(panels[0]?.type).toBe('selection-inspector');
    expect((panels[0]?.config as { showIds?: boolean }).showIds).toBe(true);
  });

  it('singleton panels are not added twice', () => {
    const { api, added } = makeFakeApi();
    useLayoutStore.getState().setApi(api);

    useLayoutStore.getState().addPanel('server-status');
    useLayoutStore.getState().addPanel('server-status');

    expect(added).toHaveLength(1);
    expect(Object.keys(useLayoutStore.getState().doc.panels)).toHaveLength(1);
  });

  it('setEngineTree prunes config for panels no longer in the tree', () => {
    const doc = createEmptyLayout('L');
    doc.panels = {
      keep: { type: 'server-status', config: {}, configVersion: 1 },
      drop: { type: 'selection-inspector', config: { showIds: false }, configVersion: 1 },
    };
    useLayoutStore.setState({ doc });

    const tree = { panels: { keep: {} }, grid: {} } as unknown as SerializedDockview;
    useLayoutStore.getState().setEngineTree(tree);

    const panels = useLayoutStore.getState().doc.panels;
    expect(Object.keys(panels)).toEqual(['keep']);
  });

  it('named layouts save and load by id', () => {
    useLayoutStore.getState().renameLayout('First');
    const firstId = useLayoutStore.getState().doc.id;
    useLayoutStore.getState().saveNamedLayout();

    useLayoutStore.getState().newLayout('Second');
    expect(useLayoutStore.getState().doc.name).toBe('Second');
    expect(useLayoutStore.getState().doc.id).not.toBe(firstId);

    useLayoutStore.getState().loadNamedLayout(firstId);
    expect(useLayoutStore.getState().doc.name).toBe('First');
    expect(useLayoutStore.getState().doc.id).toBe(firstId);
  });

  it('importLayout adopts a valid document and rejects a bad one', () => {
    const incoming = createEmptyLayout('Imported');
    expect(useLayoutStore.getState().importLayout(incoming)).toBe(true);
    expect(useLayoutStore.getState().doc.id).toBe(incoming.id);
    expect(useLayoutStore.getState().doc.name).toBe('Imported');

    const before = useLayoutStore.getState().doc;
    expect(useLayoutStore.getState().importLayout({ nope: true })).toBe(false);
    expect(useLayoutStore.getState().doc).toBe(before); // unchanged on rejection
  });

  it('setPanelConfig merges a patch and persists', () => {
    const doc = createEmptyLayout('L');
    doc.panels = { inst: { type: 'selection-inspector', config: { showIds: true }, configVersion: 1 } };
    useLayoutStore.setState({ doc });

    useLayoutStore.getState().setPanelConfig('inst', { showIds: false });

    expect((useLayoutStore.getState().doc.panels.inst?.config as { showIds?: boolean }).showIds).toBe(false);
  });
});
