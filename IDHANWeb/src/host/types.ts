/**
 * The surface a panel talks to instead of reaching into app internals. Built-in panels consume
 * exactly this, the same as third-party plugins. Every panel shares the JS realm, so this is a
 * convention boundary and not a sandbox.
 */

import type { ComponentType } from 'react';
import type {
  AutocompleteResult,
  DatabaseStats,
  MetadataResponse,
  SearchRequest,
  SearchResponse,
  StorageNode,
  TagDomain,
  TagRelationships,
  VerboseTag,
} from '../api/types';

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
 * An ordered, immutable set of record ids produced by a search. Backs the grid, and is separate from
 * `selection`, which is the user's chosen subset. Ids are an `Int32Array` so a 100k result set costs
 * around 400 KB rather than a boxed `number[]`.
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
   * behind a shared LRU, so panels must not fetch metadata directly.
   */
  getMetadata(ids: readonly RecordId[], include?: string[]): Promise<MetadataResponse>;
  /** URL for a square thumbnail at any positive edge length (px). Defaults to 256. */
  thumbnailUrl(id: RecordId, size?: number): string;
  fileUrl(id: RecordId, opts?: { download?: boolean }): string;
}

export interface TagsApi {
  autocomplete(
    prefix: string,
    opts?: { domain?: number; limit?: number },
    signal?: AbortSignal,
  ): Promise<AutocompleteResult[]>;
  /** List the tag service domains a record's tags can live in. */
  listDomains(signal?: AbortSignal): Promise<TagDomain[]>;

    /** Active tags on a record with provenance. Carries ids only; pair with resolve() for text. */
  activeVerbose(recordId: RecordId, signal?: AbortSignal): Promise<VerboseTag[]>;
  /** Resolve tag ids to "namespace:subtag" text, cached and coalesced across panels. */
  resolve(tagIds: readonly number[]): Promise<Map<number, string>>;
  /** A tag's parent/child/alias/sibling relationships in one domain, as ids (pair with resolve() for text). */
  relationships(tagDomainId: number, tagId: number, signal?: AbortSignal): Promise<TagRelationships>;
  /** Add tags (text "namespace:subtag" or an existing tag id) to every record, in one domain. */
  addToRecords(
    recordIds: readonly RecordId[],
    tags: ReadonlyArray<string | number>,
    tagDomainId: number,
    signal?: AbortSignal,
  ): Promise<void>;
  /** Remove tag ids from every record, in one domain. */
  removeFromRecords(
    recordIds: readonly RecordId[],
    tagIds: readonly number[],
    tagDomainId: number,
    signal?: AbortSignal,
  ): Promise<void>;
}

/** Read-only database statistics, for the Database Stats panel. */
export interface StatsApi {
  /** Aggregate counts (records, tags, mappings estimate, clusters) and record-count-by-mime. */
  database(signal?: AbortSignal): Promise<DatabaseStats>;
  /** Per-table PostgreSQL storage as a tree (table → indexes), sized in bytes. */
  storage(signal?: AbortSignal): Promise<StorageNode>;
}

export interface JobHandle {
  cancel(): void;
}

export interface JobsApi {
  /**
   * Watches a job to completion. A 404 after a non-terminal status was already seen means the job
   * completed and was reaped, not that it failed.
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
  stats: StatsApi;
    /** Escape hatch: authenticated fetch against the API. */
  http: { fetch(input: string, init?: RequestInit): Promise<Response> };
}

/** Props every panel component receives. Config is read via `host.settings`, not passed here. */
export interface PanelProps {
  host: HostApi;
}

/** A panel type in the catalog. */
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
