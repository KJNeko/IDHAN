/**
 * Wire types for the IDHAN REST API.
 *
 * These mirror the JSON the server actually emits (see IDHANServer/src/api). Keep them narrow — only
 * the fields the WebUI reads — rather than trying to model every column.
 */

export interface SemVer {
  major: number;
  minor: number;
  patch: number;
  string: string;
}

export interface VersionInfo {
  idhan_server_version: SemVer;
  /** An object, unlike hydrus_api_version, which really is a bare number. */
  idhan_api_version: SemVer;
  hydrus_api_version: number;
  hydrus_version: number;
  branch: string;
  commit: string;
  build: string;
}

/** Response of GET /auth/verify — reaching it at all means the API key was accepted. */
export interface KeyCheck {
  authenticated: boolean;
}

export type SortOrder = 'asc' | 'desc';

export interface SearchRequest {
  tags?: string[];
  tag_ids?: number[];
  tag_domains?: number[];
  display?: 'display' | 'storage';
  sort?: { by: string; order: SortOrder };
  /** Omit both to get the full ordered id set (the grid's default). */
  limit?: number;
  offset?: number;
    /** Ask the server to return per-step row counts in `stats`. Always written to its debug log regardless. */
    debug?: boolean;
}

/**
 * `fetch` is one term's query and `rows` is what the database returned for it. `fold` is that same
 * term combined into the running result, carrying the *same* `step` label as its fetch, and `rows`
 * is what survived. `page` is the window actually returned.
 */
export type SearchStepKind = 'fetch' | 'fold' | 'page';

export interface SearchStep {
    step: string;
    rows: number;
    kind: SearchStepKind;
    /** When true, `rows` counts what is excluded rather than what matched. */
    inverted: boolean;
    /** Present on fetch and page steps only; folds are in-memory merges. */
    micros?: number;
}

export interface SearchResponse {
  record_ids: number[];
  count: number;
  truncated: boolean;
  query_ms: number;
    /** Only present when the request set `debug`. */
    stats?: SearchStep[];
}

export interface MetadataRequest {
  record_ids: number[];
  /** Defaults to ["basic"] server-side; tags are excluded unless asked for. */
  include?: string[];
}

/** Loosely typed: the metadata payload shape depends on `include`. Panels narrow it themselves. */
export interface MetadataResponse {
  records: Array<Record<string, unknown> & { record_id: number }>;
  missing: number[];
}

export interface AutocompleteResult {
  tag_id: number;
  text: string;
  count?: number;
  /** Trigram similarity to the query, server-ranked. Present for server results, absent for cache reuse. */
  similarity?: number;
}

/** A panel a plugin advertises in its manifest (for display before the bundle is loaded). */
export interface PluginPanelInfo {
  type: string;
  title?: string;
}

/**
 * One entry from GET /plugins. Mirrors the plugin's manifest.json with two server-resolved fields:
 * `dir` (its directory name) and `entry` (the bundle URL to dynamic-import, e.g. /plugins/hello/index.js).
 */
export interface PluginManifest {
  id: string;
  name: string;
  version: string;
  /** Semver range of the host API this plugin targets, e.g. "^1.0.0". */
  hostApi: string;
  description?: string;
  dir: string;
  entry: string;
  panels?: PluginPanelInfo[];
}

/** One row from GET /layouts — server-stored layout metadata. The document itself is fetched lazily. */
export interface ServerLayoutMeta {
  id: string;
  name: string;
  schema: number;
  /** Unix seconds. */
  created_at: number;
  updated_at: number;
}

/** A tag service domain (GET /tags/domain/list). */
export interface TagDomain {
  tag_domain_id: number;
  domain_name: string;
}

/**
 * One active tag on a record with its provenance (GET /records/{id}/tags/active/verbose). Note it
 * carries only tag *ids* — resolve them to text via GET /tags/{id}/info. `aliased_from`/`inherited_from`
 * are the ids this tag was derived from (via a sibling/alias, or a parent respectively).
 */
export interface VerboseTag {
  tag_id: number;
  tag_domain_id: number;
  explicit: boolean;
  aliased_from: number[];
  inherited_from: number[];
}

/** A single tag's namespace/subtag decomposition (GET /tags/{id}/info). */
export interface TagInfo {
  tag_id: number;
  namespace: { id: number; text: string };
  subtag: { id: number; text: string };
  items_count: number;
}

/**
 * A tag's relationships within one domain (GET /tags/{domain_id}/{tag_id}/relationships). All ids;
 * resolve to text only for display. `parents`/`children` are the parent-tag and child-tag directions
 * of the parent relation; `aliases` are tags that alias *to* this one, `aliased` the tag(s) this one
 * aliases to.
 */
export interface TagRelationships {
  parents: number[];
  children: number[];
  older_siblings: number[];
  younger_siblings: number[];
  aliases: number[];
  aliased: number[];
}

/** One row of a mime breakdown (count + total bytes). `mime` is null for files not yet obtained. */
export interface MimeCount {
  mime: string | null;
  count: number;
  /** Total on-disk bytes (file_info.size) across records with this mime. */
  bytes: number;
}

/**
 * Aggregate database counts (GET /db/stats). `mappings` is normally a fast reltuples estimate over the
 * partitioned tag_mappings table (exact COUNT would be a full scan); when the estimate is unavailable
 * (table not yet analysed) the server falls back to an exact count. `mappings_estimated` says which,
 * so the UI only shows a "≈" qualifier when it really is an estimate. Pairs with GET /db/stats/sunburst
 * (per-table storage) to back the Database Stats panel.
 */
export interface DatabaseStats {
  records: number;
  tags: number;
  mappings: number;
  /** True when `mappings` is a reltuples estimate rather than an exact count. */
  mappings_estimated: boolean;
  clusters: number;
}

/** A node in the GET /db/stats/sunburst storage tree: a table (with index children) or an index. */
export interface StorageNode {
  name: string;
  value: number;
  children?: StorageNode[];
}
