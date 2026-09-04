/**
 * Wire types for the IDHAN REST API. These mirror the JSON the server emits (see
 * IDHANServer/src/api) and cover only the fields the WebUI reads, not every column.
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

/** Response of GET /auth/verify. Reaching it at all means the API key was accepted. */
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
}

/**
 * One record's metadata, identical in shape to GET /records/{id}/info.
 *
 * Only `record_id` and `hashes` are always present. A record with no file stops there; an unparsed
 * one adds `parsed: false`; everything past `simple_type` depends on which category the file is.
 */
export interface RecordMetadata {
    record_id: number;
    hashes: { sha256: string };

    /** Absent when the record has no file at all. */
    size?: number;
    mime?: string;
    extension?: string;
    /** The file's own last-write time as Unix microseconds. */
    file_mtime?: number;
    /** The file's own birth time as Unix microseconds. Absent when the filesystem records none. */
    file_ctime?: number;

    /** False when no module has produced metadata for this record yet. */
    parsed?: boolean;
    simple_type?: SimpleType;
    /** Free-form extra fields the parsing module chose to surface. */
    extra?: Record<string, unknown> | null;

    /** image, video, animation, image_project. */
    width?: number;
    height?: number;
    /** image, image_project, audio. */
    channels?: number;
    /** Lowercase 64-bit perceptual hash for static raster images. */
    phash?: string;
    /**
     * image. Embedded metadata blocks the file carries. Absent, rather than false, on an image
     * parsed before they were looked for.
     */
    has_exif?: boolean;
    has_gps?: boolean;
    has_xmp?: boolean;
    has_iptc?: boolean;
    has_icc_profile?: boolean;
    /** image_project. */
    layers?: number;

    /** video, animation, audio. */
    duration?: number;
    /** video, audio. */
    bitrate?: number;
    /** video. */
    framerate?: number;
    has_audio?: boolean;
    /** audio. */
    sample_rate?: number;
    /** animation. */
    frame_count?: number;
    loops?: boolean;

    /** archive. */
    archive_id?: number;
    encrypted?: boolean;
    file_count?: number;

    [key: string]: unknown;
}

export type SimpleType = 'none' | 'image' | 'video' | 'animation' | 'audio' | 'archive' | 'image_project';

export interface MetadataResponse {
    records: RecordMetadata[];
  missing: number[];
}

/** Duplicate and alternative neighbours of one record (GET /relationships/{record_id}). */
export interface FileRelationships {
    inferior: number[];
    superior: number[];
    /** Records directly paired with this one as alternatives, record-id-ordered. */
    alternatives: number[];
}

/** What POST /relationships/clear actually removed between two records. */
export interface ClearedRelationship {
    /** A direct better/worse pair existed in either direction and was deleted. */
    duplicate_removed: boolean;
    /** The two were paired as alternatives, and that pair was deleted. */
    alternative_removed: boolean;
}

/** A close pair no one has ruled on yet (GET /relationships/duplicates/undecided). */
export interface UndecidedPair {
    record_id_a: number;
    record_id_b: number;
    /** Differing bits between the two perceptual hashes. */
    distance: number;
}

/** The next pair awaiting a duplicate decision, or none left within the distance. */
export interface UndecidedDuplicate {
    /** The maximum distance that was searched, echoed back. */
    distance: number;
    /** Pairs already marked coincidental lookalikes were eligible. */
    include_unrelated: boolean;
    /** Null once every pair within the distance has been ruled on. */
    pair: UndecidedPair | null;
}

/** One record within a perceptual-hash distance of the probe. */
export interface SimilarRecord {
    record_id: number;
    /** Differing bits between this record's perceptual hash and the probe's; 0 is a pixel-identical match. */
    distance: number;
    /** This pair was marked a coincidental lookalike, so it only appears when include_unrelated is set. */
    unrelated: boolean;
}

/** Perceptual-hash neighbourhood of one record (GET /relationships/{record_id}/similar). */
export interface SimilarRecords {
    record_id: number;
    /** The distance that was searched, echoed back. */
    distance: number;
    limit: number;
    /** Records marked unrelated to the probe were kept in the results. */
    include_unrelated: boolean;
    /** Records already related to the probe were kept in the results. */
    include_related: boolean;
    /** The limit cut the results short, so more records may sit within the distance. */
    truncated: boolean;
    /** Nearest first, then by record id. Never contains the probe itself. */
    results: SimilarRecord[];
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

/** One row of server-stored layout metadata from GET /layouts. The document is fetched lazily. */
export interface ServerLayoutMeta {
  id: string;
  name: string;
  schema: number;
  /** Unix seconds. */
  created_at: number;
  updated_at: number;
}

export interface DownloadSessionInfo {
    id: number;
    name: string;
    created_at: number;
    last_used_at: number;
    /** Requests logged by this session that did not answer 200. */
    error_count: number;
}

export type DownloadSessionUrlState = 'pending' | 'processing' | 'completed' | 'failed' | 'skipped';

export interface DownloadSessionUrlJob {
    id: number;
    /** Null on an inserted URL; set on every URL the parser discovered from one. */
    parent_id: number | null;
    url: string;
    state: DownloadSessionUrlState;
    created_at: number;
    finished_at: number | null;
    error: string | null;
    /** Why a URL was skipped. "Already in the database" means this session first encountered an
     * existing record; "Already imported earlier in this session" means it was already associated
     * with this session. */
    note: string | null;
    record_id: number | null;
}

export interface DownloadSessionUrlNode extends DownloadSessionUrlJob {
    children: DownloadSessionUrlNode[];
}

export interface DownloadSessionUrlFlat extends DownloadSessionUrlJob {
    urls: DownloadSessionUrlJob[];
    record_ids: number[];
}

export interface DownloadSessionRecords {
    record_ids: number[];
}

/** One settled transfer that did not answer 200. */
export interface DownloadSessionError {
    id: number;
    /** The URL row the request belonged to, or null once that row was retried away. */
    url_id: number | null;
    url: string;
    /** Null when the transfer never produced a response. */
    status: number | null;
    lane: string;
    message: string | null;
    occurred_at: number;
}

export interface DownloadSessionErrorTally {
    status: number | null;
    count: number;
}

export interface DownloadSessionErrorLog {
    /** Every status the session has logged, filter or no filter, so the filter chips stay put. */
    statuses: DownloadSessionErrorTally[];
    errors: DownloadSessionError[];
}

export type DebugWorkPhase = 'queued' | 'module' | 'parser' | 'transferring';

export type DebugSessionState = 'running' | 'idle' | 'closing' | 'cancelled';

export type DebugEventKind =
    | 'started'
    | 'completed'
    | 'failed'
    | 'request'
    | 'request_failed'
    | 'imported'
    | 'import_failed'
    | 'followed'
    | 'finished';

export interface DebugWorkItem {
    work_id: number;
    parent_work_id: number | null;
    url_id: number | null;
    url: string;
    url_class: string;
    parser: string;
    phase: DebugWorkPhase;
    outstanding: number;
    age_ms: number;
}

export interface DebugPendingRequest {
    work_id: number;
    url_id: number | null;
    url: string;
    lane: string;
    import: boolean;
    age_ms: number;
}

export interface DebugSessionCounters {
    work_started: number;
    work_completed: number;
    work_failed: number;
    requests: number;
    request_bytes: number;
    requests_failed: number;
    imported: number;
    import_bytes: number;
    import_failed: number;
    follows_queued: number;
    follows_filtered: number;
    follows_already_queued: number;
    follows_already_explored: number;
    follows_already_imported: number;
}

export interface DebugSessionEvent {
    /** Monotonic per session and never reused; the cursor for the next poll. */
    sequence: number;
    at: number;
    kind: DebugEventKind;
    work_id: number;
    url_id: number | null;
    url: string;
    /** Lane key, content type, follow status, or error text, depending on the kind. */
    detail: string;
    status: number | null;
    bytes: number;
    record_id: number | null;
}

export interface DebugSession {
    id: number;
    name: string | null;
    root_url: string;
    state: DebugSessionState;
    closed: boolean;
    cancelled: boolean;
    idle: boolean;
    queued: number;
    running: number;
    in_flight: number;
    in_flight_limit: number;
    outstanding: number;
    counters: DebugSessionCounters;
    work: DebugWorkItem[];
    requests: DebugPendingRequest[];
    events: DebugSessionEvent[];
    event_sequence: number;
    /** Events evicted from the server ring before this reader reached them. */
    events_dropped: number;
}

export interface DebugLane {
    scheduling_key: string;
    group: string;
    throttled: boolean;
    effective_interval_ms: number;
    remaining_ms: number;
    consecutive_limits: number;
    backed_off: boolean;
    in_flight: number;
    queued: number;
    shards: number;
    bytes_per_second: number;
    active: boolean;
}

export interface DownloaderDebug {
    sessions: DebugSession[];
    lanes: DebugLane[];
}

export type DownloaderSecrets = Record<string, string>;

export interface RateLimitLane {
    scheduling_key: string;
    throttled: boolean;
    requests: number;
    seconds: number;
    effective_interval_ms: number;
    remaining_ms: number;
    consecutive_limits: number;
    active: boolean;
}

export interface RateLimitsMessage {
    type: 'rate_limits';
    limits: RateLimitLane[];
}

export interface RateLimitMessage {
    type: 'rate_limit';
    limit: RateLimitLane;
}

/** A tag service domain (GET /tags/domain/list). */
export interface TagDomain {
  tag_domain_id: number;
  domain_name: string;
}

/**
 * One active tag on a record with its provenance (GET /records/{id}/tags/active/verbose). Carries tag
 * ids only; resolve them via GET /tags/{id}/info. `aliased_from` and `inherited_from` are the ids
 * this tag was derived from, through a sibling/alias or a parent respectively.
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
 * A tag's relationships within one domain (GET /tags/{domain_id}/{tag_id}/relationships), as ids.
 * `parents` and `children` are the two directions of the parent relation. `aliases` are tags that
 * alias to this one, `aliased` the tags this one aliases to.
 */
export interface TagRelationships {
  parents: number[];
  children: number[];
  older_siblings: number[];
  younger_siblings: number[];
  aliases: number[];
  aliased: number[];
}

/**
 * GET /mime: every mime id in the table mapped to the name it reports, keyed by the id as a string.
 * Many-to-one, so it cannot be inverted: a Pixiv Ugoira (5002) reports "application/zip" just as
 * APPLICATION_ZIP (5000) does.
 */
export type MimeMap = Record<string, string>;

/** One row of a mime breakdown (count + total bytes). `mime` is null for files not yet obtained. */
export interface MimeCount {
  mime: string | null;
  count: number;
  /** Total on-disk bytes (file_info.size) across records with this mime. */
  bytes: number;
}

/**
 * Aggregate database counts (GET /db/stats). `mappings` is normally a reltuples estimate over the
 * partitioned tag_mappings table, since an exact COUNT would be a full scan; the server falls back to
 * an exact count if the table has not been analysed yet. `mappings_estimated` says which was used.
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

/** What a job-dispatching endpoint answers with; poll `/jobs/{job_id}/status` from there. */
export interface JobDispatch {
    job_id: number;
    status: string;
    /** Records the job was scoped to, when the endpoint takes a record list. */
    record_count?: number;
}
