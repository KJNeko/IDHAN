/**
 * @idhan/host — the surface a panel talks to instead of reaching into app internals.
 *
 * Built-in panels consume exactly this, the same as third-party plugins will (M6). Because every
 * panel shares the JS realm, this is a convention boundary, not a security boundary — it exists so
 * panels don't couple to app internals, not to sandbox them.
 *
 * For M3 the types are the frozen-ish contract; a few methods (jobs.watch) are intentionally minimal
 * and will harden as real panels exercise them in M4.
 */

import type { ComponentType } from 'react';
import type { AutocompleteResult, MetadataResponse, SearchRequest, SearchResponse } from '../api/types';

/** Bumped on breaking changes to the surface below; plugin manifests will declare a compatible range. */
export const HOST_API_VERSION = '1.0.0';

export type RecordId = number;

/** Stable id for one placed panel within a layout; distinct from the panel *type*. */
export type PanelInstanceId = string;

export type Unsubscribe = () => void;

/** Cross-panel record selection, shared through the host so e.g. the grid and the viewer agree. */
export interface SelectionApi {
  get(): readonly RecordId[];
  set(ids: readonly RecordId[]): void;
  subscribe(listener: (ids: readonly RecordId[]) => void): Unsubscribe;
}

/** Minimal typed pub/sub for cross-panel messages that are not the selection. */
export interface BusApi {
  emit(topic: string, payload: unknown): void;
  on(topic: string, listener: (payload: unknown) => void): Unsubscribe;
}

/**
 * An ordered, immutable set of record ids produced by a search. This is what backs the grid — kept
 * separate from `selection` (which is the user's chosen subset). Ids are an `Int32Array` so a 100k
 * result set costs ~400 KB rather than a boxed `number[]`, and index math against it is pure
 * arithmetic (see the grid's virtualization).
 */
export interface SearchResultSet {
  readonly ids: Int32Array;
  /** Server-reported query time in ms, for display. */
  readonly queryMs: number;
  /** Human-readable tokens describing the query that produced this set. */
  readonly query: readonly string[];
}

/**
 * The active search result set, shared through the host so the Search panel can publish and the grid,
 * viewer, and info panels all page against the same ordered ids. Retains its last value, so a panel
 * mounted after a search still sees the current results.
 */
export interface ResultsApi {
  get(): SearchResultSet;
  set(next: SearchResultSet): void;
  subscribe(listener: (results: SearchResultSet) => void): Unsubscribe;
}

/** Per-instance persisted settings. Writes land in the layout document, so they survive reload. */
export interface SettingsApi<T = Record<string, unknown>> {
  get(): T;
  set(next: Partial<T>): void;
  subscribe(listener: (value: T) => void): Unsubscribe;
}

export type ThemeMode = 'dark' | 'light';

export interface ThemeApi {
  mode(): ThemeMode;
  subscribe(listener: (mode: ThemeMode) => void): Unsubscribe;
}

export interface ToastOptions {
  kind?: 'info' | 'success' | 'error';
  timeoutMs?: number;
}

export interface UiApi {
  toast(message: string, options?: ToastOptions): void;
}

export interface SearchApi {
  run(request: SearchRequest, signal?: AbortSignal): Promise<SearchResponse>;
}

export interface RecordsApi {
  /**
   * The one metadata path. The host coalesces concurrent requests across panels into batched calls
   * behind a shared LRU, so panels must not fetch metadata directly — that is what keeps a 100k grid
   * from melting to the first naive panel.
   */
  getMetadata(ids: readonly RecordId[], include?: string[]): Promise<MetadataResponse>;
  thumbnailUrl(id: RecordId, size?: 128 | 256 | 512): string;
  fileUrl(id: RecordId, opts?: { download?: boolean }): string;
}

export interface TagsApi {
  autocomplete(
    prefix: string,
    opts?: { domain?: number; limit?: number },
    signal?: AbortSignal,
  ): Promise<AutocompleteResult[]>;
}

export interface JobHandle {
  cancel(): void;
}

export interface JobsApi {
  /**
   * Watch a job to completion. Encapsulates the reap-on-first-terminal-poll trap in one place: a 404
   * after a non-terminal status was previously seen is treated as completed-and-reaped, not failure.
   */
  watch(jobId: number, onUpdate: (status: unknown) => void): JobHandle;
}

/** The full host handed to a panel instance. */
export interface HostApi {
  readonly version: string;
  readonly instanceId: PanelInstanceId;
  search: SearchApi;
  records: RecordsApi;
  tags: TagsApi;
  selection: SelectionApi;
  results: ResultsApi;
  bus: BusApi;
  settings: SettingsApi;
  theme: ThemeApi;
  ui: UiApi;
  jobs: JobsApi;
  /** Escape hatch: authenticated fetch against the API. Trusted only; a hard wall just gets bypassed. */
  http: { fetch(input: string, init?: RequestInit): Promise<Response> };
}

/** Props every panel component receives. Config is read via `host.settings`, not passed here. */
export interface PanelProps {
  host: HostApi;
}

/** A panel type in the catalog (built-in now; third-party plugin later). */
export interface PanelDefinition<TConfig = Record<string, unknown>> {
  type: string;
  title: string;
  description?: string;
  component: ComponentType<PanelProps>;
  defaultConfig?: TConfig;
  /** Bumped when the shape of this panel's config changes; migrateConfig upgrades older blobs. */
  configVersion?: number;
  migrateConfig?: (config: unknown, fromVersion: number) => TConfig;
  /** If true, only one instance may exist at a time. Default false. */
  singleton?: boolean;
}
