/**
 * Typed IDHAN REST client.
 *
 * One key is presented on every authenticated call via the `X-API-Key` header. The server accepts an
 * API key and a session key interchangeably in that slot, so this client is deliberately blind to
 * which kind it holds — see src/auth/session.ts for how the credential is obtained.
 *
 * This is the raw client. The coalescing/caching host API (batched metadata, thumbnail LRU) is a
 * later layer (M3) built on top of these methods, not baked in here.
 */

import type {
  AutocompleteResult,
  MetadataRequest,
  MetadataResponse,
  SearchRequest,
  SearchResponse,
  SessionCheck,
  SessionGrant,
  SessionRevoke,
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
    /** Exchange a permanent API key for a session key. 401 if the key is invalid or already a session. */
    createSession(apiKey: string, signal?: AbortSignal): Promise<SessionGrant> {
      return request<SessionGrant>('/auth/session', { method: 'POST', key: apiKey, signal });
    },
    /** Confirm a key is currently accepted. Throws ApiError(401) if not. */
    checkSession(key: string, signal?: AbortSignal): Promise<SessionCheck> {
      return request<SessionCheck>('/auth/session', { key, signal });
    },
    /** Revoke a session key. A no-op (revoked:false) for a permanent API key. */
    deleteSession(key: string, signal?: AbortSignal): Promise<SessionRevoke> {
      return request<SessionRevoke>('/auth/session', { method: 'DELETE', key, signal });
    },
  },

  search(body: SearchRequest, signal?: AbortSignal): Promise<SearchResponse> {
    return request<SearchResponse>('/search', { method: 'POST', body, signal });
  },

  recordsMetadata(body: MetadataRequest, signal?: AbortSignal): Promise<MetadataResponse> {
    return request<MetadataResponse>('/records/metadata', { method: 'POST', body, signal });
  },

  autocompleteTags(prefix: string, opts: { domain?: number; limit?: number } = {}, signal?: AbortSignal): Promise<
    AutocompleteResult[]
  > {
    const params = new URLSearchParams({ tag: prefix });
    if (opts.domain !== undefined) params.set('tag_domain', String(opts.domain));
    if (opts.limit !== undefined) params.set('limit', String(opts.limit));
    return request<AutocompleteResult[]>(`/tags/autocomplete?${params.toString()}`, { signal });
  },
};

/**
 * URL for an <img> or <video> whose element cannot set request headers. The credential therefore
 * rides in the `idhan_key` query parameter, which the server accepts alongside the header. This is
 * why the browser holds a session key rather than the permanent API key: a session key in a URL
 * (logs, history) is revocable and expiring, so the exposure is bounded.
 */
export function thumbnailUrl(recordId: number, size: 128 | 256 | 512 = 256): string {
  const params = new URLSearchParams({ size: String(size) });
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
