/**
 * Typed IDHAN REST client.
 *
 * The permanent API key is presented on every authenticated call via the `X-API-Key` header — see
 * src/auth/session.ts for how the credential is obtained and stored.
 *
 * This is the raw client. The coalescing/caching host API (batched metadata, thumbnail LRU) is a
 * later layer (M3) built on top of these methods, not baked in here.
 */

import type { LayoutDocument } from '../layout/document';
import type {
  AutocompleteResult,
  MetadataRequest,
  MetadataResponse,
  PluginManifest,
  SearchRequest,
  SearchResponse,
  ServerLayoutMeta,
    KeyCheck,
  DatabaseStats,
  StorageNode,
  TagDomain,
  TagInfo,
  TagRelationships,
  VerboseTag,
  VersionInfo,
} from './types';

/** Header the credential rides in. The server also honours Authorization/IDHAN-API-Key and the rest. */
const KEY_HEADER = 'X-API-Key';

/** Non-2xx responses throw this, carrying the status and any parsed JSON body for callers to inspect. */
export class ApiError extends Error {
  constructor(
    readonly status: number,
    message: string,
    readonly body?: unknown,
  ) {
    super(message);
    this.name = 'ApiError';
  }
}

/** The credential presented on authenticated requests. Set by the auth layer after login/restore. */
let currentKey: string | null = null;

export function setKey(key: string | null): void {
  currentKey = key;
}

export function getKey(): string | null {
  return currentKey;
}

interface RequestOptions {
  method?: string;
  /** JSON-encoded into the body if present. */
  body?: unknown;
  signal?: AbortSignal;
  /**
   * Credential to present. Defaults to the stored key; pass a specific key to authenticate a call
   * before the client's key is committed (login), or `null` to force an unauthenticated request.
   */
  key?: string | null;
}

async function request<T>(path: string, options: RequestOptions = {}): Promise<T> {
  const headers = new Headers();
  const key = 'key' in options ? options.key : currentKey;
  if (key) headers.set(KEY_HEADER, key);

  let body: string | undefined;
  if (options.body !== undefined) {
    headers.set('Content-Type', 'application/json');
    body = JSON.stringify(options.body);
  }

  const response = await fetch(path, { method: options.method ?? 'GET', headers, body, signal: options.signal });

  const text = await response.text();
  const parsed: unknown = text ? safeParse(text) : undefined;

  if (!response.ok) {
    throw new ApiError(response.status, `${options.method ?? 'GET'} ${path} → ${response.status}`, parsed);
  }
  return parsed as T;
}

function safeParse(text: string): unknown {
  try {
    return JSON.parse(text);
  } catch {
    return text;
  }
}

export const api = {
  /** Unauthenticated; also serves as a plain reachability probe for the server. */
  version(signal?: AbortSignal): Promise<VersionInfo> {
    return request<VersionInfo>('/version', { key: null, signal });
  },

  auth: {
      /** Confirm an API key is currently accepted. Throws ApiError(401) if not. */
      verifyKey(key: string, signal?: AbortSignal): Promise<KeyCheck> {
          return request<KeyCheck>('/auth/verify', {key, signal});
    },
  },

  search(body: SearchRequest, signal?: AbortSignal): Promise<SearchResponse> {
    return request<SearchResponse>('/search', { method: 'POST', body, signal });
  },

  recordsMetadata(body: MetadataRequest, signal?: AbortSignal): Promise<MetadataResponse> {
    return request<MetadataResponse>('/records/metadata', { method: 'POST', body, signal });
  },

  async autocompleteTags(
    prefix: string,
    opts: { domain?: number; limit?: number } = {},
    signal?: AbortSignal,
  ): Promise<AutocompleteResult[]> {
    const params = new URLSearchParams({ tag: prefix });
    if (opts.domain !== undefined) params.set('tag_domain', String(opts.domain));
    if (opts.limit !== undefined) params.set('limit', String(opts.limit));
    const raw = await request<AutocompleteRow[]>(`/tags/autocomplete?${params.toString()}`, { signal });
    return raw.map((row) => ({
      tag_id: row.tag_id,
      text: row.tag_text ?? row.value ?? '',
      count: row.count,
      similarity: row.similarity,
    }));
  },
};

/** Raw wire row from GET /tags/autocomplete, before normalisation to AutocompleteResult. */
interface AutocompleteRow {
  tag_id: number;
  tag_text?: string;
  value?: string;
  count?: number;
  similarity?: number;
}

export const tags = {
  listDomains(signal?: AbortSignal): Promise<TagDomain[]> {
    return request<TagDomain[]>('/tags/domain/list', { signal });
  },

  info(tagId: number, signal?: AbortSignal): Promise<TagInfo> {
    return request<TagInfo>(`/tags/${tagId}/info`, { signal });
  },

  /** A tag's parent/child/alias/sibling relationships in one domain, as ids (GET .../relationships). */
  relationships(tagDomainId: number, tagId: number, signal?: AbortSignal): Promise<TagRelationships> {
    return request<TagRelationships>(`/tags/${tagDomainId}/${tagId}/relationships`, { signal });
  },

  activeVerbose(recordId: number, signal?: AbortSignal): Promise<VerboseTag[]> {
    return request<VerboseTag[]>(`/records/${recordId}/tags/active/verbose`, { signal });
  },

  /**
   * Add tags (text "namespace:subtag", a bare subtag, or an existing tag id) to every record in the
   * list, in one domain. Unknown text tags are created server-side.
   */
  addToRecords(recordIds: number[], tagsToAdd: Array<string | number>, tagDomainId: number, signal?: AbortSignal): Promise<void> {
    return request<void>(`/records/tags/add?tag_domain_id=${tagDomainId}`, {
      method: 'POST',
      body: { records: recordIds, tags: tagsToAdd },
      signal,
    });
  },

  /**
   * Remove tag ids from records. The remove endpoint is per-record (a "set" per record), so the same
   * ids are removed from each. Removal is by id only — resolve text to an id first.
   */
  removeFromRecords(recordIds: number[], tagIds: number[], tagDomainId: number, signal?: AbortSignal): Promise<void> {
    return request<void>(`/records/tags/remove?tag_domain_id=${tagDomainId}`, {
      method: 'POST',
      body: { records: recordIds, sets: recordIds.map(() => tagIds) },
      signal,
    });
  },
};

/** Database statistics for the Database Stats panel. */
export const stats = {
  /** Aggregate counts (records, tags, mappings estimate, clusters) and record-count-by-mime. */
  database(signal?: AbortSignal): Promise<DatabaseStats> {
    return request<DatabaseStats>('/db/stats', { signal });
  },

  /** Per-table PostgreSQL storage as a tree (table → indexes), sized in bytes. */
  storage(signal?: AbortSignal): Promise<StorageNode> {
    return request<StorageNode>('/db/stats/sunburst', { signal });
  },
};

/** WebUI plugin discovery (M6). Returns the validated index; bundles are dynamic-imported client-side. */
export const plugins = {
  list(signal?: AbortSignal): Promise<PluginManifest[]> {
    return request<PluginManifest[]>('/plugins', { signal });
  },
};

/**
 * Server-stored named layouts (M5). The browser's localStorage remains the source of truth; these
 * endpoints move a layout between browsers. Identity is the document's own uuid, so `push` is an
 * upsert to that id.
 */
export const layouts = {
  /** Metadata for every server-stored layout (no documents). */
  list(signal?: AbortSignal): Promise<ServerLayoutMeta[]> {
    return request<ServerLayoutMeta[]>('/layouts', { signal });
  },

  /** The full stored document for `id`. Throws ApiError(404) if it isn't on the server. */
  get(id: string, signal?: AbortSignal): Promise<LayoutDocument> {
    return request<LayoutDocument>(`/layouts/${encodeURIComponent(id)}`, { signal });
  },

  /** Push-to-server upsert. Idempotent for the same id; ApiError(409) if the name belongs to another. */
  push(doc: LayoutDocument, signal?: AbortSignal): Promise<{ id: string }> {
    return request<{ id: string }>(`/layouts/${encodeURIComponent(doc.id)}`, {
      method: 'PUT',
      body: doc,
      signal,
    });
  },

  /** Remove a server-stored layout. `deleted` is false if nothing matched. */
  remove(id: string, signal?: AbortSignal): Promise<{ deleted: boolean }> {
    return request<{ deleted: boolean }>(`/layouts/${encodeURIComponent(id)}`, { method: 'DELETE', signal });
  },
};

/**
 * URL for an <img> or <video> whose element cannot set request headers. The credential therefore
 * rides in the `idhan_key` query parameter, which the server accepts alongside the header.
 */
export function thumbnailUrl(recordId: number, size = 256): string {
  // The server generates a square thumbnail at any requested edge length; clamp to a positive integer.
  const edge = Math.max(1, Math.round(size));
  const params = new URLSearchParams({ size: String(edge) });
  if (currentKey) params.set('idhan_key', currentKey);
  return `/records/${recordId}/thumbnail?${params.toString()}`;
}

export function fileUrl(recordId: number, opts: { download?: boolean } = {}): string {
  const params = new URLSearchParams();
  if (opts.download) params.set('download', 'true');
  if (currentKey) params.set('idhan_key', currentKey);
  const query = params.toString();
  return `/records/${recordId}/file${query ? `?${query}` : ''}`;
}
