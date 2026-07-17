/**
 * Layout state. Split of authority:
 *  - dockview owns *structure* (which panels exist and where) — serialized into engine.tree on change.
 *  - this store owns *config* (per-instance settings) plus persistence and the named-layout catalog.
 *
 * The working document is auto-saved to localStorage. Named layouts are snapshots the user saves and
 * can reload; server push/pull arrives in M5. Identity is the document uuid — the name is renameable.
 */

import { create } from 'zustand';
import type { DockviewApi } from 'dockview-react';
import { getPanel } from '../panels/registry';
import {
  createEmptyLayout,
  migrateLayout,
  type LayoutDocument,
  type PanelState,
  type SerializedDockview,
} from './document';

const WORKING_KEY = 'idhan.layout.working';
const SAVED_KEY = 'idhan.layouts';

export interface LayoutMeta {
  id: string;
  name: string;
}

function safeParse(text: string | null): unknown {
  if (!text) return null;
  try {
    return JSON.parse(text);
  } catch {
    return null;
  }
}

function loadWorking(): LayoutDocument {
  try {
    const migrated = migrateLayout(safeParse(localStorage.getItem(WORKING_KEY)));
    if (migrated) return migrated;
  } catch {
    // fall through to a fresh default
  }
  return createEmptyLayout('Default');
}

function persistWorking(doc: LayoutDocument): void {
  try {
    localStorage.setItem(WORKING_KEY, JSON.stringify(doc));
  } catch {
    // storage full/disabled — the in-memory doc still works this session
  }
}

function loadSaved(): Record<string, LayoutDocument> {
  let text: string | null = null;
  try {
    text = localStorage.getItem(SAVED_KEY);
  } catch {
    text = null;
  }
  const raw = safeParse(text);
  const out: Record<string, LayoutDocument> = {};
  if (raw && typeof raw === 'object') {
    for (const [id, value] of Object.entries(raw as Record<string, unknown>)) {
      const doc = migrateLayout(value);
      if (doc) out[id] = doc;
    }
  }
  return out;
}

function persistSaved(saved: Record<string, LayoutDocument>): void {
  try {
    localStorage.setItem(SAVED_KEY, JSON.stringify(saved));
  } catch {
    // ignore
  }
}

function metaList(saved: Record<string, LayoutDocument>): LayoutMeta[] {
  return Object.values(saved)
    .map((doc) => ({ id: doc.id, name: doc.name }))
    .sort((a, b) => a.name.localeCompare(b.name));
}

/** Instance ids still present in a serialized dockview tree. */
function liveInstanceIds(tree: SerializedDockview | null): Set<string> {
  const panels = (tree as { panels?: Record<string, unknown> } | null)?.panels;
  return new Set(panels ? Object.keys(panels) : []);
}

interface LayoutStore {
  doc: LayoutDocument;
  api: DockviewApi | null;
  editMode: boolean;
  /** Increments when the whole tree is swapped (load/new/reset) to force a Workspace remount. */
  generation: number;
  savedLayouts: LayoutMeta[];

  setApi: (api: DockviewApi | null) => void;
  toggleEditMode: () => void;

  /** Persist the serialized engine tree and prune config for panels the user closed. */
  setEngineTree: (tree: SerializedDockview) => void;
  addPanel: (type: string) => void;

  getPanelConfig: (instanceId: string) => Record<string, unknown>;
  setPanelConfig: (instanceId: string, patch: Record<string, unknown>) => void;

  renameLayout: (name: string) => void;
  newLayout: (name: string) => void;
  saveNamedLayout: () => void;
  loadNamedLayout: (id: string) => void;
  deleteNamedLayout: (id: string) => void;
}

export const useLayoutStore = create<LayoutStore>((set, get) => ({
  doc: loadWorking(),
  api: null,
  editMode: false,
  generation: 0,
  savedLayouts: metaList(loadSaved()),

  setApi: (api) => set({ api }),

  toggleEditMode: () => set((s) => ({ editMode: !s.editMode })),

  setEngineTree: (tree) => {
    const { doc } = get();
    const live = liveInstanceIds(tree);
    const panels: Record<string, PanelState> = {};
    for (const [id, state] of Object.entries(doc.panels)) {
      if (live.has(id)) panels[id] = state;
    }
    const next: LayoutDocument = { ...doc, engine: { ...doc.engine, tree }, panels };
    set({ doc: next });
    persistWorking(next);
  },

  addPanel: (type) => {
    const { api, doc } = get();
    if (!api) return;
    const def = getPanel(type);

    if (def?.singleton) {
      const existing = Object.entries(doc.panels).find(([, state]) => state.type === type);
      if (existing) {
        api.getPanel(existing[0])?.api.setActive();
        return;
      }
    }

    const instanceId = crypto.randomUUID();
    const config = def?.defaultConfig ? structuredClone(def.defaultConfig) : {};
    const configVersion = def?.configVersion ?? 1;

    // Write config before adding the panel so the component can read it on first render.
    const next: LayoutDocument = {
      ...doc,
      panels: { ...doc.panels, [instanceId]: { type, config, configVersion } },
    };
    set({ doc: next });
    persistWorking(next);

    api.addPanel({ id: instanceId, component: type, params: { instanceId }, title: def?.title ?? type });
    // dockview fires a layout change → setEngineTree persists the new tree.
  },

  getPanelConfig: (instanceId) => (get().doc.panels[instanceId]?.config ?? {}) as Record<string, unknown>,

  setPanelConfig: (instanceId, patch) => {
    const { doc } = get();
    const existing = doc.panels[instanceId];
    if (!existing) return;
    const config = { ...(existing.config as Record<string, unknown>), ...patch };
    const next: LayoutDocument = {
      ...doc,
      panels: { ...doc.panels, [instanceId]: { ...existing, config } },
    };
    set({ doc: next });
    persistWorking(next);
  },

  renameLayout: (name) => {
    const next: LayoutDocument = { ...get().doc, name };
    set({ doc: next });
    persistWorking(next);
  },

  newLayout: (name) => {
    const doc = createEmptyLayout(name);
    set((s) => ({ doc, generation: s.generation + 1 }));
    persistWorking(doc);
  },

  saveNamedLayout: () => {
    const { doc } = get();
    const saved = loadSaved();
    saved[doc.id] = structuredClone(doc);
    persistSaved(saved);
    set({ savedLayouts: metaList(saved) });
  },

  loadNamedLayout: (id) => {
    const saved = loadSaved();
    const found = saved[id];
    if (!found) return;
    const doc = structuredClone(found);
    set((s) => ({ doc, generation: s.generation + 1 }));
    persistWorking(doc);
  },

  deleteNamedLayout: (id) => {
    const saved = loadSaved();
    delete saved[id];
    persistSaved(saved);
    set({ savedLayouts: metaList(saved) });
  },
}));
