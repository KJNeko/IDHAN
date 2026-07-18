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

/** Response of POST /auth/session. Under IDHAN_DISABLE_API_AUTH this is a placeholder (all-zero key). */
export interface SessionGrant {
  session_key: string;
  /** Unix seconds. 0 for the auth-disabled placeholder. */
  expires_at: number;
}

/** Response of GET /auth/session — reaching it at all means the key was accepted. */
export interface SessionCheck {
  authenticated: boolean;
}

/** Response of DELETE /auth/session. */
export interface SessionRevoke {
  revoked: boolean;
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
}

export interface SearchResponse {
  record_ids: number[];
  count: number;
  truncated: boolean;
  query_ms: number;
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
