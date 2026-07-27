/**
 * Layout state. Split of authority:
 *  - dockview owns *structure* (which panels exist and where) — serialized into engine.tree on change.
 *  - this store owns *config* (per-instance settings) plus persistence and the named-layout catalog.
 *
 * The working document is auto-saved to localStorage. Named layouts are snapshots the user saves and
 * can reload. A layout can also be pushed to / pulled from the server (M5) to move it between
 * browsers; localStorage stays the source of truth. Identity is the document uuid — the name is
 * renameable.
 */

import { create } from 'zustand';
import type { DockviewApi } from 'dockview-react';
import { layouts as layoutApi } from '../api/client';
import type { ServerLayoutMeta } from '../api/types';
import { getPanel } from '../panels/registry';
import {
  createEmptyLayout,
  migrateLayout,
  type LayoutDocument,
  type PanelState,
  type SerializedDockview,
} from './document';
import { uuid } from '../util/uuid';

const WORKING_KEY = 'idhan.layout.working';
const SAVED_KEY = 'idhan.layouts';

export interface LayoutMeta {
  id: string;
  name: string;
}

/** Where to place a new panel relative to an existing one; mirrors dockview's position option. */
export interface PanelPosition {
  referencePanel?: string;
  direction?: 'left' | 'right' | 'above' | 'below' | 'within';
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
  /** Increments when the panel catalog changes (e.g. plugins finish loading), to refresh the picker. */
  catalogVersion: number;
  savedLayouts: LayoutMeta[];
  /** Metadata for layouts stored on the server; refreshed on demand, empty until first fetch. */
  serverLayouts: ServerLayoutMeta[];
  /** True while a push/pull/refresh is in flight, so the UI can disable server actions. */
  serverBusy: boolean;

  setApi: (api: DockviewApi | null) => void;
  toggleEditMode: () => void;
  /** Signal that the registered panel catalog changed (plugins loaded), so views re-read the registry. */
  bumpCatalog: () => void;

  /** Persist the serialized engine tree and prune config for panels the user closed. */
  setEngineTree: (tree: SerializedDockview) => void;
  /**
   * Add a panel of `type`, optionally positioned relative to an existing panel (dockview position).
   * Returns the new instance id (or the existing one for a focused singleton), or null if no engine.
   */
  addPanel: (type: string, position?: PanelPosition) => string | null;

  getPanelConfig: (instanceId: string) => Record<string, unknown>;
  setPanelConfig: (instanceId: string, patch: Record<string, unknown>) => void;

  renameLayout: (name: string) => void;
  newLayout: (name: string) => void;
  saveNamedLayout: () => void;
  loadNamedLayout: (id: string) => void;
  deleteNamedLayout: (id: string) => void;

  /**
   * Adopt an externally-provided blob (a file import) as the working document. Migrated on the way in,
   * so an older or corrupt file is upgraded or rejected. Returns false if it isn't a readable layout.
   */
  importLayout: (raw: unknown) => boolean;

  /** Fetch the server layout list into `serverLayouts`. Rejects (with ApiError) on failure. */
  refreshServerLayouts: () => Promise<void>;
  /** Push the working document to the server (upsert), then refresh the list. */
  pushLayoutToServer: () => Promise<void>;
  /** Replace the working document with a server-stored one (migrated on the way in). */
  pullLayoutFromServer: (id: string) => Promise<void>;
  /** Delete a server-stored layout, then refresh the list. */
  deleteServerLayout: (id: string) => Promise<void>;
}

export const useLayoutStore = create<LayoutStore>((set, get) => ({
  doc: loadWorking(),
  api: null,
  editMode: false,
  generation: 0,
  catalogVersion: 0,
  savedLayouts: metaList(loadSaved()),
  serverLayouts: [],
  serverBusy: false,

  setApi: (api) => set({ api }),

  toggleEditMode: () => set((s) => ({ editMode: !s.editMode })),

  bumpCatalog: () => set((s) => ({ catalogVersion: s.catalogVersion + 1 })),

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

  addPanel: (type, position) => {
    const { api, doc } = get();
    if (!api) return null;
    const def = getPanel(type);

    if (def?.singleton) {
      const existing = Object.entries(doc.panels).find(([, state]) => state.type === type);
      if (existing) {
        api.getPanel(existing[0])?.api.setActive();
        return existing[0];
      }
    }

    const instanceId = uuid();
    const config = def?.defaultConfig ? structuredClone(def.defaultConfig) : {};
    const configVersion = def?.configVersion ?? 1;

    // Write config before adding the panel so the component can read it on first render.
    const next: LayoutDocument = {
      ...doc,
      panels: { ...doc.panels, [instanceId]: { type, config, configVersion } },
    };
    set({ doc: next });
    persistWorking(next);

    const referencePanel = position?.referencePanel;
    api.addPanel({
      id: instanceId,
      component: type,
      params: { instanceId },
      title: def?.title ?? type,
      ...(position?.direction && referencePanel
        ? { position: { referencePanel, direction: position.direction } }
        : {}),
    });
    // dockview fires a layout change → setEngineTree persists the new tree.
    return instanceId;
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

  importLayout: (raw) => {
    const doc = migrateLayout(raw);
    if (!doc) return false;
    set((s) => ({ doc, generation: s.generation + 1 }));
    persistWorking(doc);
    return true;
  },

  refreshServerLayouts: async () => {
    set({ serverBusy: true });
    try {
      const list = await layoutApi.list();
      set({ serverLayouts: list });
    } finally {
      set({ serverBusy: false });
    }
  },

  pushLayoutToServer: async () => {
    set({ serverBusy: true });
    try {
      await layoutApi.push(get().doc);
      const list = await layoutApi.list();
      set({ serverLayouts: list });
    } finally {
      set({ serverBusy: false });
    }
  },

  pullLayoutFromServer: async (id) => {
    set({ serverBusy: true });
    try {
      // Migrate the server copy on the way in — it may predate the current schema, and this also
      // rejects a corrupt document instead of adopting it as the working layout.
      const doc = migrateLayout(await layoutApi.get(id));
      if (!doc) throw new Error('The server returned an unreadable layout document');
      set((s) => ({ doc, generation: s.generation + 1 }));
      persistWorking(doc);
    } finally {
      set({ serverBusy: false });
    }
  },

  deleteServerLayout: async (id) => {
    set({ serverBusy: true });
    try {
      await layoutApi.remove(id);
      const list = await layoutApi.list();
      set({ serverLayouts: list });
    } finally {
      set({ serverBusy: false });
    }
  },
}));
