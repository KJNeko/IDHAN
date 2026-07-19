/**
 * The layout document — our envelope *wrapping* the engine's serialization, not equal to it.
 *
 * The dockview tree is opaque and stores only which panel instance sits where. Per-panel config lives
 * in our own `panels` map keyed by instance id, so config survives an engine swap and migrates
 * independently per panel type. Two version axes: `schema` (this envelope, migrated by a chain) and
 * each panel's `configVersion` (migrated by the panel definition). An unknown panel type is never
 * dropped — its config blob is preserved and rendered as a tombstone.
 */

import type { DockviewApi } from 'dockview-react';
import type { PanelInstanceId } from '../host/types';
import { uuid } from '../util/uuid';

/** dockview's serialized tree. Derived from the API so we don't depend on the type being re-exported. */
export type SerializedDockview = ReturnType<DockviewApi['toJSON']>;

export const LAYOUT_SCHEMA_VERSION = 1 as const;
/** Bumped if we change how we drive dockview, independent of dockview's own format. */
export const DOCKVIEW_ENGINE_VERSION = 1;

export interface PanelState {
  type: string;
  configVersion: number;
  config: unknown;
}

export interface LayoutDocument {
  schema: typeof LAYOUT_SCHEMA_VERSION;
  /** uuid — the stable identity. The name is user-facing and renameable; it is NOT identity. */
  id: string;
  name: string;
  engine: {
    kind: 'dockview';
    version: number;
    /** null for a fresh document whose default panels have not been materialized yet. */
    tree: SerializedDockview | null;
  };
  panels: Record<PanelInstanceId, PanelState>;
}

function newId(): string {
  return uuid();
}

export function createEmptyLayout(name: string): LayoutDocument {
  return {
    schema: LAYOUT_SCHEMA_VERSION,
    id: newId(),
    name,
    engine: { kind: 'dockview', version: DOCKVIEW_ENGINE_VERSION, tree: null },
    panels: {},
  };
}

/** Schema migrations, keyed by the version they upgrade *from*. Empty until schema 2 exists. */
const migrations: Record<number, (doc: LayoutDocument) => LayoutDocument> = {};

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}

/**
 * Validate and upgrade a persisted blob to the current schema. Returns null if it is not a recognisable
 * layout document (the caller then falls back to a default), so a corrupt entry can never crash boot.
 */
export function migrateLayout(raw: unknown): LayoutDocument | null {
  if (!isRecord(raw)) return null;
  if (typeof raw.id !== 'string' || typeof raw.name !== 'string') return null;
  if (!isRecord(raw.engine) || raw.engine.kind !== 'dockview') return null;
  if (!isRecord(raw.panels)) return null;

  let version = typeof raw.schema === 'number' ? raw.schema : 0;
  let doc = raw as unknown as LayoutDocument;

  while (version < LAYOUT_SCHEMA_VERSION) {
    const migrate = migrations[version];
    if (!migrate) return null; // no path forward — treat as unreadable rather than guess
    doc = migrate(doc);
    version += 1;
  }
  if (version !== LAYOUT_SCHEMA_VERSION) return null;

  return { ...doc, schema: LAYOUT_SCHEMA_VERSION };
}
