import type {JobStatus} from './types';
/**
 * Typed IDHAN REST client.
 *
 * The permanent API key is presented on every authenticated call via the `X-API-Key` header; see
 * src/auth/session.ts for how the credential is obtained and stored.
 *
 * This is the raw client. The coalescing and caching host API (batched metadata, thumbnail LRU) is a
 * layer built on top of these methods, not baked in here.
 */

import type { LayoutDocument } from '../layout/document';
import type {
  AutocompleteResult,
    AutocompleteOptions,
  MetadataRequest,
  MetadataResponse,
    FileRelationships,
    JobDispatch,
    MimeMap,
  PluginManifest,
    ClearedRelationship,
  SearchRequest,
  SearchResponse,
    SimilarRecords,
    UndecidedDuplicate,
  ServerLayoutMeta,
    KeyCheck,
  DatabaseStats,
    DownloadSessionErrorLog,
    DownloadSessionInfo,
    DownloadSessionRecords,
    DownloadSessionUrlFlat,
    DownloadSessionUrlNode,
    DownloaderDebug,
    DownloaderSecrets,
    RateLimitLane,
  StorageNode,
  TagDomain,
  TagInfo,
  TagRelationships,
  VerboseTag,
  VersionInfo,
} from './types';

/** Header the credential rides in. The server also honours Authorization/IDHAN-API-Key and the rest. */
const KEY_HEADER = 'X-API-Key';

/** Non-2xx responses throw this, carrying the status and decoded JSON or plain-text body. */
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

    if (!response.ok) throw await readApiError(response, `${options.method ?? 'GET'} ${path}`);

  const text = await response.text();
  const parsed: unknown = text ? safeParse(text) : undefined;
  return parsed as T;
}

/** Also used by panels making requests through the authenticated host fetch API. Consumes the response body. */
export async function readApiError(response: Response, request: string): Promise<ApiError> {
    const text = await response.text();
    const parsed: unknown = text ? safeParse(text) : undefined;
    return new ApiError(response.status, errorMessage(parsed) ?? `${request} → ${response.status}`, parsed);
}

function errorMessage(parsed: unknown): string | null {
    if (typeof parsed === 'string') return parsed.trim() || null;
    if (typeof parsed !== 'object' || parsed === null) return null;
    const detail = (parsed as { error?: unknown }).error;
    return typeof detail === 'string' && detail.length > 0 ? detail : null;
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

    downloader: {
        /** Maps session IDs to last-seen event sequences; omitted sessions return retained history. */
        debug(eventCursors: ReadonlyMap<number, number> = new Map(), signal?: AbortSignal): Promise<DownloaderDebug> {
            const cursorText = [...eventCursors].map(([id, sequence]) => `${id}:${sequence}`).join(',');
            const query = cursorText === '' ? '' : `?event_cursors=${encodeURIComponent(cursorText)}`;
            return request<DownloaderDebug>(`/downloader/debug${query}`, {signal});
        },
        secrets(signal?: AbortSignal): Promise<DownloaderSecrets> {
            return request<DownloaderSecrets>('/downloader/secrets', {signal});
        },
        setSecrets(secrets: DownloaderSecrets, signal?: AbortSignal): Promise<DownloaderSecrets> {
            return request<DownloaderSecrets>('/downloader/secrets', {method: 'POST', body: secrets, signal});
        },
    },

    downloadSessions: {
        list(signal?: AbortSignal): Promise<DownloadSessionInfo[]> {
            return request<DownloadSessionInfo[]>('/download_sessions', {signal});
        },
        get(sessionId: number, signal?: AbortSignal): Promise<DownloadSessionInfo> {
            return request<DownloadSessionInfo>(`/download_sessions/${sessionId}`, {signal});
        },
        create(name: string, signal?: AbortSignal): Promise<DownloadSessionInfo> {
            return request<DownloadSessionInfo>('/download_sessions', {method: 'POST', body: {name}, signal});
        },
        submitUrl(sessionId: number, url: string, signal?: AbortSignal): Promise<{
            id: number;
            url: string;
            state: string
        }> {
            return request<{ id: number; url: string; state: string }>(`/download_sessions/${sessionId}/urls`, {
                method: 'POST',
                body: {url},
                signal,
            });
        },
        submitUrlSession(url: string, name?: string, signal?: AbortSignal): Promise<{
            session: DownloadSessionInfo;
            url: DownloadSessionUrlNode;
        }> {
            return request('/download_sessions/urls', {
                method: 'POST',
                body: name === undefined ? {url} : {url, name},
                signal,
            });
        },
        urls(sessionId: number, signal?: AbortSignal): Promise<DownloadSessionUrlNode[]> {
            return request<DownloadSessionUrlNode[]>(`/download_sessions/${sessionId}/urls`, {signal});
        },
        urlsFlat(sessionId: number, signal?: AbortSignal): Promise<DownloadSessionUrlFlat[]> {
            return request<DownloadSessionUrlFlat[]>(`/download_sessions/${sessionId}/urls?flatten=true`, {signal});
        },
        /** Rejects with 409 while the URL is running. */
        retryUrl(sessionId: number, urlId: number, signal?: AbortSignal): Promise<DownloadSessionUrlNode> {
            return request<DownloadSessionUrlNode>(`/download_sessions/${sessionId}/urls/${urlId}/retry`, {
                method: 'POST',
                signal,
            });
        },
        records(sessionId: number, signal?: AbortSignal): Promise<DownloadSessionRecords> {
            return request<DownloadSessionRecords>(`/download_sessions/${sessionId}/records`, {signal});
        },
        /** An empty `statuses` asks for every logged status; a null entry asks for the requests that never answered. */
        errors(
            sessionId: number,
            statuses: readonly (number | null)[] = [],
            signal?: AbortSignal,
        ): Promise<DownloadSessionErrorLog> {
            const wanted = statuses.map((status) => (status === null ? 'none' : String(status)));
            const filter = wanted.length === 0 ? '' : `?status=${wanted.join(',')}`;
            return request<DownloadSessionErrorLog>(`/download_sessions/${sessionId}/errors${filter}`, {signal});
        },
        destroy(sessionId: number, signal?: AbortSignal): Promise<{ deleted: boolean }> {
            return request<{ deleted: boolean }>(`/download_sessions/${sessionId}`, {method: 'DELETE', signal});
        },
    },

  search(body: SearchRequest, signal?: AbortSignal): Promise<SearchResponse> {
    return request<SearchResponse>('/search', { method: 'POST', body, signal });
  },

  recordsMetadata(body: MetadataRequest, signal?: AbortSignal): Promise<MetadataResponse> {
    return request<MetadataResponse>('/records/metadata', { method: 'POST', body, signal });
  },

    deleteRecordFile(recordId: number, signal?: AbortSignal): Promise<{ deleted: boolean }> {
        return request<{ deleted: boolean }>(`/records/${recordId}/file`, {method: 'DELETE', signal});
    },

    /**
     * Dispatches a metadata reparse over `recordIds`, or over every record when none are given
     * (POST /jobs/metadata/rescan). Returns as soon as the job is queued.
     */
    rescanMetadata(recordIds?: readonly number[], signal?: AbortSignal): Promise<JobDispatch> {
        return request<JobDispatch>('/jobs/metadata/rescan', {
            method: 'POST',
            body: recordIds ? {records: [...recordIds]} : {},
            signal,
        });
    },

    reorganizeUrls(signal?: AbortSignal): Promise<JobDispatch> {
        return request<JobDispatch>('/jobs/urls/reorganize', {method: 'POST', signal});
    },

    recordRelationships(recordId: number, signal?: AbortSignal): Promise<FileRelationships> {
        return request<FileRelationships>(`/relationships/${recordId}`, {signal});
    },

    /** Makes `betterId` king of the duplicate group containing both records. */
    setBetterDuplicate(worseId: number, betterId: number, signal?: AbortSignal): Promise<void> {
        return request<void>('/relationships/duplicates/add', {
            method: 'POST',
            body: {worse_id: worseId, better_id: betterId},
            signal,
        });
    },

    /** Pairs every listed record with every other as alternatives. */
    addAlternatives(recordIds: readonly number[], signal?: AbortSignal): Promise<void> {
        return request<void>('/relationships/alternatives/add', {method: 'POST', body: [...recordIds], signal});
    },

    /** Marks two records as coincidental lookalikes (POST /relationships/unrelated/add). */
    setUnrelated(recordIdA: number, recordIdB: number, signal?: AbortSignal): Promise<void> {
        return request<void>('/relationships/unrelated/add', {
            method: 'POST',
            body: {record_id_a: recordIdA, record_id_b: recordIdB},
            signal,
        });
    },

    /**
     * Drops whichever direct pair the two records hold: duplicate, alternative or unrelated
     * (POST /relationships/clear).
     */
    clearRelationship(recordIdA: number, recordIdB: number, signal?: AbortSignal): Promise<ClearedRelationship> {
        return request<ClearedRelationship>('/relationships/clear', {
            method: 'POST',
            body: {record_id_a: recordIdA, record_id_b: recordIdB},
            signal,
        });
    },

    /**
     * Return undecided pairs ordered by record ID, then distance. `limit` controls lookahead
     * size.
     */
    nextUndecidedDuplicate(
        opts: { distance?: number; limit?: number } = {},
        signal?: AbortSignal,
    ): Promise<UndecidedDuplicate> {
        const params = new URLSearchParams();
        if (opts.distance !== undefined) params.set('distance', String(opts.distance));
        if (opts.limit !== undefined) params.set('limit', String(opts.limit));
        const query = params.toString();
        return request<UndecidedDuplicate>(`/relationships/duplicates/undecided${query ? `?${query}` : ''}`, {signal});
    },

    /** Records whose perceptual hash is within `distance` differing bits of one record's. */
    recordsSimilar(
        recordId: number,
        opts: { distance?: number; limit?: number; includeUnrelated?: boolean; includeRelated?: boolean } = {},
        signal?: AbortSignal,
    ): Promise<SimilarRecords> {
        const params = new URLSearchParams();
        if (opts.distance !== undefined) params.set('distance', String(opts.distance));
        if (opts.limit !== undefined) params.set('limit', String(opts.limit));
        if (opts.includeUnrelated !== undefined) params.set('include_unrelated', String(opts.includeUnrelated));
        if (opts.includeRelated !== undefined) params.set('include_related', String(opts.includeRelated));
        const query = params.toString();
        return request<SimilarRecords>(`/relationships/${recordId}/similar${query ? `?${query}` : ''}`, {signal});
    },

  async autocompleteTags(
    prefix: string,
    opts: AutocompleteOptions = {},
    signal?: AbortSignal,
  ): Promise<AutocompleteResult[]> {
    const params = new URLSearchParams({ tag: prefix });
      const domains = opts.domains ?? (opts.domain !== undefined ? [opts.domain] : []);
      for (const domain of domains) params.append('tag_domains', String(domain));
      params.set('tag_display_type', opts.display ?? 'display');
      params.set('include_unused', String(opts.includeUnused ?? true));
    if (opts.limit !== undefined) params.set('limit', String(opts.limit));
    const raw = await request<AutocompleteRow[]>(`/tags/autocomplete?${params.toString()}`, { signal });
    return raw.map((row) => ({
      tag_id: row.tag_id,
      text: row.tag_text ?? row.value ?? '',
      count: row.count,
      similarity: row.similarity,
    }));
  },

    /** Every mime id the server knows, mapped to its name (GET /mime). */
    listMimes(signal?: AbortSignal): Promise<MimeMap> {
        return request<MimeMap>('/mime', {signal});
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

    info(tagId: number, options: { domains?: readonly number[]; signal?: AbortSignal } = {}): Promise<TagInfo> {
        const params = new URLSearchParams();
        for (const domain of options.domains ?? []) params.append('tag_domains', String(domain));
        const query = params.toString();
        return request<TagInfo>(`/tags/${tagId}/info${query ? `?${query}` : ''}`, {signal: options.signal});
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
   * ids are removed from each. Removal is by id only, so resolve text to an id first.
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
/** Watches coalesced session-tree updates. Returns a close function. */
export function watchDownloadSessions(handlers: {
    onSession: (session: DownloadSessionInfo, urls: DownloadSessionUrlNode[]) => void;
    onRemoved?: (sessionId: number) => void;
    onError?: (message: string) => void;
    onOpen?: () => void;
}): () => void {
    const refreshing = new Map<number, boolean>();
    const refresh = async (sessionId: number) => {
        if (refreshing.has(sessionId)) {
            refreshing.set(sessionId, true);
            return;
        }

        refreshing.set(sessionId, false);
        while (refreshing.has(sessionId)) {
            refreshing.set(sessionId, false);
            try {
                const [session, urls] = await Promise.all([
                    api.downloadSessions.get(sessionId), api.downloadSessions.urls(sessionId),
                ]);
                handlers.onSession(session, urls);
            } catch (error) {
                if (error instanceof ApiError && error.status === 404) handlers.onRemoved?.(sessionId);
                else handlers.onError?.('Unable to refresh a download session.');
            }

            if (refreshing.get(sessionId) !== true) {
                refreshing.delete(sessionId);
                return;
            }
        }
    };

    return sharedWatchSSE([EVENT_TYPES.downloadSessionChanged], {
        onOpen: handlers.onOpen,
        onError: handlers.onError,
        onEvent: (name, data, _timestamp, type) => {
            if (type !== EVENT_TYPES.downloadSessionChanged || name === 'shutdown') return;

            const payload = safeParse(data);
            if (typeof payload !== 'object' || payload === null) return;

            // The server opens every subscription with the sessions that already exist, so a reload
            // shows downloads already in progress rather than waiting for one of them to change.
            if (name === 'snapshot') {
                const sessions = (payload as { sessions?: unknown }).sessions;
                if (Array.isArray(sessions)) {
                    void Promise.all(sessions.filter((id): id is number => typeof id === 'number').map(refresh));
                    return;
                }
                const id = (payload as { id?: unknown }).id;
                if (typeof id === 'number') void refresh(id);
                return;
            }

            if (name !== 'update') return;
            const event = payload as { id?: unknown; removed?: unknown };
            if (typeof event.id !== 'number') return;
            if (event.removed === true) handlers.onRemoved?.(event.id);
            else void refresh(event.id);
        },
    });
}

export function isRateLimitLane(value: unknown): value is RateLimitLane {
    if (typeof value !== 'object' || value === null) return false;
    const lane = value as Record<string, unknown>;
    const requests = lane.requests;
    const seconds = lane.seconds;
    const effectiveInterval = lane.effective_interval_ms;
    const remaining = lane.remaining_ms;
    const consecutive = lane.consecutive_limits;
    return typeof lane.scheduling_key === 'string'
        && typeof lane.throttled === 'boolean'
        && typeof lane.active === 'boolean'
        && [requests, seconds, effectiveInterval, remaining, consecutive]
            .every((field) => typeof field === 'number' && Number.isFinite(field) && field >= 0)
        && typeof requests === 'number'
        && typeof seconds === 'number'
        && (lane.throttled ? requests > 0 && seconds > 0 : requests === 0 && seconds === 0);
}

export type SSEHandlers = {
    /** `timestamp` is seconds since the epoch, stamped by the server when the event was generated. */
    onEvent: (name: string, data: string, timestamp: number | null, type: string) => void;
    onError?: (message: string) => void;
    onOpen?: () => void;
};

export const EVENT_TYPES = {
    jobStatus: 'job.status',
    downloadSessionChanged: 'downloader.session',
    allDownloader: 'downloader.*',
    downloaderDebug: 'downloader.debug',
    downloaderRateLimit: 'downloader.rate_limit',
} as const;

type EventSubscription = { types: readonly string[] | null; handlers: SSEHandlers };

type SharedStream = {
    subscriptions: Set<EventSubscription>;
    close: () => void;
    path: string | null;
    opened: boolean;
    snapshotReady: boolean;
    snapshots: Map<string, Parameters<SSEHandlers['onEvent']>>;
};

let sharedStream: SharedStream | null = null;

function matchesEventType(types: readonly string[] | null, type: string): boolean {
    if (types === null || types.length === 0 || type === 'stream.snapshot.complete') return true;
    return types.some((selector) => selector === type
        || (selector.endsWith('.*') && type.startsWith(selector.slice(0, -1))));
}

function streamPath(subscriptions: Set<EventSubscription>): string | null {
    if (subscriptions.size === 0) return null;
    const selectors = new Set<string>();
    for (const subscription of subscriptions) {
        if (subscription.types === null || subscription.types.length === 0) return '/events';
        for (const type of subscription.types) selectors.add(type);
    }
    return `/events?types=${encodeURIComponent([...selectors].sort().join(','))}`;
}

function startSharedStream(stream: SharedStream): void {
    const path = streamPath(stream.subscriptions);
    if (path === stream.path) return;

    stream.close();
    stream.path = path;
    stream.opened = false;
    stream.snapshotReady = false;
    stream.snapshots.clear();
    if (path === null) return;

    stream.close = watchSSE(path, {
        onOpen: () => {
            stream.opened = true;
            stream.snapshotReady = false;
            stream.snapshots.clear();
            for (const subscription of [...stream.subscriptions]) subscription.handlers.onOpen?.();
        },
        onError: (message) => {
            stream.opened = false;
            for (const subscription of [...stream.subscriptions]) subscription.handlers.onError?.(message);
        },
        onEvent: (name, data, timestamp, type) => {
            const event = [name, data, timestamp, type] as Parameters<SSEHandlers['onEvent']>;
            if (type === 'stream.snapshot.complete') {
                stream.snapshotReady = true;
                for (const subscription of [...stream.subscriptions])
                    subscription.handlers.onEvent(name, data, timestamp, type);
                return;
            }

            const payload = safeParse(data) as {
                job_id?: unknown;
                id?: unknown;
                scheduling_key?: unknown;
                removed?: unknown
            };
            const identity = typeof payload === 'object' && payload !== null
                ? payload.job_id ?? payload.id ?? payload.scheduling_key ?? type
                : type;
            const key = `${type}:${String(identity)}`;
            if (name === 'update' && payload && typeof payload === 'object' && payload.removed === true) {
                stream.snapshots.delete(key);
            } else if (name === 'snapshot' || name === 'update') {
                if (type === EVENT_TYPES.downloaderDebug) {
                    const previous = stream.snapshots.get(key) ?? null;
                    const reduced = retainDebugSnapshot(previous, event);
                    if (reduced) stream.snapshots.set(key, reduced);
                } else {
                    stream.snapshots.set(key, event);
                }
            }
            for (const subscription of [...stream.subscriptions]) {
                if (matchesEventType(subscription.types, type))
                    subscription.handlers.onEvent(name, data, timestamp, type);
            }
        },
    });
}

/** One shared connection; adding/removing filters reconnects once with the union of requested types. */
function sharedWatchSSE(types: readonly string[] | null, handlers: SSEHandlers): () => void {
    if (sharedStream === null) {
        sharedStream = {
            subscriptions: new Set(), close: () => {
            }, path: null,
            opened: false, snapshotReady: false, snapshots: new Map(),
        };
    }
    const stream = sharedStream;
    const subscription = {types, handlers};
    stream.subscriptions.add(subscription);
    startSharedStream(stream);

    if (stream.opened) {
        queueMicrotask(() => {
            if (!stream.subscriptions.has(subscription)) return;
            handlers.onOpen?.();
            for (const snapshot of stream.snapshots.values()) {
                if (matchesEventType(types, snapshot[3])) handlers.onEvent(...snapshot);
            }
            if (stream.snapshotReady) handlers.onEvent('snapshot', '{}', null, 'stream.snapshot.complete');
        });
    }

    return () => {
        stream.subscriptions.delete(subscription);
        startSharedStream(stream);
        if (stream.subscriptions.size === 0 && sharedStream === stream) sharedStream = null;
    };
}

/** Subscribe to the unified event stream. Omit `types` for all types; `downloader.*` is a prefix filter. */
export function watchEvents(
    handlers: {
        onEvent: (event: { event: string; type: string; data: string; timestamp: number | null }) => void;
        onError?: (message: string) => void;
        onOpen?: () => void;
    },
    types?: readonly string[],
): () => void {
    const selected = types?.length ? [...new Set(types)].sort() : [];
    return sharedWatchSSE(selected.length ? selected : null, {
        onOpen: handlers.onOpen,
        onError: handlers.onError,
        onEvent: (event, data, timestamp, type) => handlers.onEvent({event, type, data, timestamp}),
    });
}

function watchSSE(path: string, handlers: SSEHandlers): () => void {
    let controller: AbortController | null = null;
    let retry: ReturnType<typeof setTimeout> | undefined;
    let closed = false;
    let backoff = 1000;

    const scheduleReconnect = () => {
        if (closed) return;
        retry = setTimeout(connect, backoff);
        backoff = Math.min(backoff * 2, 15000);
    };

    const connect = async () => {
        if (closed) return;

        controller = new AbortController();
        const headers = new Headers({Accept: 'text/event-stream'});
        if (currentKey) headers.set(KEY_HEADER, currentKey);

        try {
            const response = await fetch(path, {headers, signal: controller.signal});
            if (!response.ok || response.body === null) {
                const body = await response.text();
                handlers.onError?.(errorMessage(safeParse(body)) ?? `SSE connection failed: ${response.status}`);
                if (response.status === 401 || response.status === 403) {
                    closed = true;
                    return;
                }
                scheduleReconnect();
                return;
            }

            backoff = 1000;
            handlers.onOpen?.();

            const reader = response.body.pipeThrough(new TextDecoderStream()).getReader();
            let buffered = '';
            let eventName = 'message';
            let data: string[] = [];
            let timestamp: number | null = null;
            let type = '';

            const dispatch = () => {
                if (data.length > 0) handlers.onEvent(eventName, data.join('\n'), timestamp, type);
                eventName = 'message';
                data = [];
                timestamp = null;
                type = '';
            };

            const consumeLine = (line: string) => {
                if (line === '') {
                    dispatch();
                    return;
                }
                if (line.startsWith(':')) return;

                const separator = line.indexOf(':');
                const field = separator === -1 ? line : line.slice(0, separator);
                const value = separator === -1 ? '' : line.slice(separator + 1).replace(/^ /, '');
                if (field === 'event') eventName = value;
                if (field === 'type') type = value;
                if (field === 'data') data.push(value);
                if (field === 'timestamp') {
                    const parsed = Number(value);
                    timestamp = Number.isFinite(parsed) ? parsed : null;
                }
            };

            while (!closed) {
                const next = await reader.read();
                if (next.done) break;

                buffered += next.value.replace(/\r/g, '');
                let newline = buffered.indexOf('\n');
                while (newline !== -1) {
                    consumeLine(buffered.slice(0, newline));
                    buffered = buffered.slice(newline + 1);
                    newline = buffered.indexOf('\n');
                }
            }
        } catch (error) {
            if (!closed && !(error instanceof DOMException && error.name === 'AbortError')) {
                handlers.onError?.('Lost the event stream; retrying.');
            }
        }

        scheduleReconnect();
    };

    void connect();

    return () => {
        closed = true;
        if (retry !== undefined) clearTimeout(retry);
        controller?.abort();
    };
}

type JobHandlers = {
    onJobs: (jobs: JobStatus[]) => void;
    onError?: (message: string) => void;
};

type JobStream = {
    jobs: Map<number, JobStatus>;
    handlers: Set<JobHandlers>;
    ready: boolean;
    close: () => void;
};

let jobStream: JobStream | null = null;

function isJobStatus(value: unknown): value is JobStatus {
    if (typeof value !== 'object' || value === null) return false;
    const job = value as Partial<JobStatus>;
    return typeof job.job_id === 'number' && typeof job.status === 'string' && typeof job.completed === 'boolean';
}

/** One shared connection and current snapshot for every job consumer, including late subscribers. */
export function watchJobs(handlers: JobHandlers): () => void {
    if (!jobStream) {
        const stream: JobStream = {
            jobs: new Map(), handlers: new Set(), ready: false, close: () => {
            }
        };
        jobStream = stream;
        stream.close = sharedWatchSSE([EVENT_TYPES.jobStatus], {
            onOpen: () => {
                stream.ready = false;
                stream.jobs.clear();
            },
            onError: (message) => {
                for (const handler of [...stream.handlers]) handler.onError?.(message);
            },
            onEvent: (name, data, _timestamp, type) => {
                if (type === 'stream.snapshot.complete') {
                    stream.ready = true;
                    const jobs = [...stream.jobs.values()].sort((a, b) => a.job_id - b.job_id);
                    for (const handler of [...stream.handlers]) handler.onJobs(jobs);
                    return;
                }
                if (type !== EVENT_TYPES.jobStatus) return;
                const payload = safeParse(data);
                if (typeof payload !== 'object' || payload === null) return;
                if (name === 'snapshot') {
                    const jobs = (payload as { jobs?: unknown }).jobs;
                    if (Array.isArray(jobs) && jobs.every(isJobStatus)) {
                        stream.jobs = new Map(jobs.map(job => [job.job_id, job]));
                    } else if (isJobStatus(payload)) {
                        stream.jobs.set(payload.job_id, payload);
                    } else return;
                } else if (name === 'update') {
                    const update = payload as { job_id?: unknown; removed?: unknown };
                    if (typeof update.job_id === 'number' && update.removed === true) stream.jobs.delete(update.job_id);
                    else if (isJobStatus(payload)) stream.jobs.set(payload.job_id, payload);
                    else return;
                } else return;
                if (stream.ready) {
                    const jobs = [...stream.jobs.values()].sort((a, b) => a.job_id - b.job_id);
                    for (const handler of [...stream.handlers]) handler.onJobs(jobs);
                }
            },
        });
    }
    const stream = jobStream;
    stream.handlers.add(handlers);
    queueMicrotask(() => {
        if (stream.ready && stream.handlers.has(handlers)) handlers.onJobs([...stream.jobs.values()].sort((a, b) => a.job_id - b.job_id));
    });
    return () => {
        stream.handlers.delete(handlers);
        if (stream.handlers.size > 0) return;
        stream.close();
        if (jobStream === stream) jobStream = null;
    };
}

function isDownloaderDebug(value: unknown): value is DownloaderDebug {
    if (typeof value !== 'object' || value === null) return false;
    const debug = value as Partial<DownloaderDebug>;
    return Array.isArray(debug.sessions) && Array.isArray(debug.lanes);
}

function retainDebugSnapshot(
    previous: Parameters<SSEHandlers['onEvent']> | null,
    event: Parameters<SSEHandlers['onEvent']>,
): Parameters<SSEHandlers['onEvent']> | null {
    const [name, data, timestamp, type] = event;
    const incoming = safeParse(data);
    if (!isDownloaderDebug(incoming)) return previous;
    if (name === 'snapshot' || !previous) return ['snapshot', data, timestamp, type];
    if (name !== 'update') return previous;
    const old = safeParse(previous[1]);
    if (!isDownloaderDebug(old)) return ['snapshot', data, timestamp, type];
    const sessions = incoming.sessions.map(session => {
        const prior = old.sessions.find(value => value.id === session.id);
        const events = new Map((prior?.events ?? []).map(value => [value.sequence, value]));
        for (const event of session.events) events.set(event.sequence, event);
        return {
            ...session, events: [...events.values()].sort((a, b) => a.sequence - b.sequence).slice(-500),
            events_dropped: (prior?.events_dropped ?? 0) + session.events_dropped
        };
    });
    return ['snapshot', JSON.stringify({...incoming, sessions}), timestamp, type];
}

/** Complete diagnostic state and incremental event history over one shared SSE subscription. */
export function watchDownloaderDebug(handlers: {
    onState: (debug: DownloaderDebug, reset: boolean) => void;
    onError?: (message: string) => void;
}): () => void {
    return sharedWatchSSE([EVENT_TYPES.downloaderDebug], {
        onError: handlers.onError,
        onEvent: (name, data, _timestamp, type) => {
            if (type !== EVENT_TYPES.downloaderDebug) return;
            if (name !== 'snapshot' && name !== 'update') return;
            const debug = safeParse(data);
            if (isDownloaderDebug(debug)) handlers.onState(debug, name === 'snapshot');
        },
    });
}

export function watchRateLimits(handlers: {
    onSnapshot: (limits: RateLimitLane[]) => void;
    onUpdate: (limit: RateLimitLane) => void;
    onError?: (message: string) => void;
    onOpen?: () => void;
}): () => void {
    let snapshotting = false;
    let snapshottingValues = new Map<string, RateLimitLane>();
    return sharedWatchSSE([EVENT_TYPES.downloaderRateLimit], {
        onOpen: () => {
            snapshotting = true;
            snapshottingValues.clear();
            handlers.onOpen?.();
        },
        onError: handlers.onError,
        onEvent: (name, data, _timestamp, type) => {
            if (name === 'shutdown') return;
            if (type === 'stream.snapshot.complete') {
                if (snapshotting) {
                    handlers.onSnapshot([...snapshottingValues.values()]);
                    snapshotting = false;
                    snapshottingValues.clear();
                }
                return;
            }
            if (type !== EVENT_TYPES.downloaderRateLimit) return;

            const payload = safeParse(data);
            if (name === 'snapshot' && isRateLimitLane(payload)) {
                snapshotting = true;
                snapshottingValues.set(payload.scheduling_key, payload);
                return;
            }

            if (name === 'update' && isRateLimitLane(payload)) handlers.onUpdate(payload);
        },
    });
}

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
