import type {JobStatus, Subscription, SubscriptionInput, SubscriptionGroup, SubscriptionGroupInput} from './types';
import type {DownloadBandwidth} from './types';
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
    DownloadSessionCounts,
    DownloadSessionErrorLog,
    DownloadSessionInfo,
    DownloadSessionRecords,
    DownloadSessionState,
    DownloadSessionUrlJob,
    DownloadSessionUrlState,
    DownloadSessionUrlFlat,
    DownloadSessionUrlNode,
    DownloaderSecrets,
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
	subscriptionGroups: {
		list(signal?: AbortSignal): Promise<SubscriptionGroup[]> {
			return request('/subscription_groups', {signal});
		},
		create(body: SubscriptionGroupInput): Promise<SubscriptionGroup> {
			return request('/subscription_groups', {method: 'POST', body});
		},
		update(id: number, body: SubscriptionGroupInput): Promise<SubscriptionGroup> {
			return request(`/subscription_groups/${id}`, {method: 'PATCH', body});
		},
		run(id: number): Promise<SubscriptionGroup> {
			return request(`/subscription_groups/${id}/run`, {method: 'POST'});
		},
		remove(id: number): Promise<{ deleted: boolean }> {
			return request(`/subscription_groups/${id}`, {method: 'DELETE'});
		},
	},
	subscriptions: {
		batch(groupId: number, items: SubscriptionInput[]): Promise<{ ids: number[]; added: number; skipped: number }> {
			return request(`/subscription_groups/${groupId}/subscriptions/batch`, {method: 'POST', body: {items}});
		},
		records(id: number): Promise<{ record_ids: number[] }> {
			return request(`/subscriptions/${id}/records`);
		},
		list(signal?: AbortSignal): Promise<Subscription[]> {
			return request('/subscriptions', {signal});
		},
		create(groupId: number, body: SubscriptionInput): Promise<Subscription> {
			return request(`/subscription_groups/${groupId}/subscriptions`, {method: 'POST', body});
		},
		update(id: number, body: SubscriptionInput): Promise<Subscription> {
			return request(`/subscriptions/${id}`, {method: 'PATCH', body});
		},
		run(id: number): Promise<Subscription> {
			return request(`/subscriptions/${id}/run`, {method: 'POST'});
		},
		remove(id: number): Promise<{ deleted: boolean }> {
			return request(`/subscriptions/${id}`, {method: 'DELETE'});
		},
	},
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
		enableLane(lane: string): Promise<{ reset: string }> {
			return request(`/rate_limits/reset?lane=${encodeURIComponent(lane)}`, {method: 'POST'});
		},
		cache(signal?: AbortSignal): Promise<import('./types').ParserCacheSnapshot> {
			return request('/downloader/cache', {signal});
		},
		rateLimits(signal?: AbortSignal): Promise<import('./types').RateLimitLane[]> {
			return request('/rate_limits', {signal});
		},
        secrets(signal?: AbortSignal): Promise<DownloaderSecrets> {
            return request<DownloaderSecrets>('/downloader/secrets', {signal});
        },
        setSecrets(secrets: DownloaderSecrets, signal?: AbortSignal): Promise<DownloaderSecrets> {
            return request<DownloaderSecrets>('/downloader/secrets', {method: 'POST', body: secrets, signal});
        },
    },

    downloaderHosts: {
        list(signal?: AbortSignal): Promise<import('./types').DownloaderParserHost[]> {
            return request('/downloader/hosts', {signal});
        },
        set(host: string, parser: string): Promise<import('./types').DownloaderParserHost[]> {
            return request('/downloader/hosts', {method: 'POST', body: {host, parser}});
        },
        remove(host: string): Promise<{ deleted: boolean }> {
            return request(`/downloader/hosts?${new URLSearchParams({host})}`, {method: 'DELETE'});
        },
    },
    downloadSessions: {
		installParser(filename: string, source: string, replace: boolean): Promise<{ installed: boolean }> {
			return request('/download_sessions/parsers', {method: 'POST', body: {filename, source, replace}});
		},
		async downloadParser(filename: string): Promise<Blob> {
			const path = `/download_sessions/parsers/${encodeURIComponent(filename)}`;
			const headers = new Headers();
			if (currentKey) headers.set(KEY_HEADER, currentKey);
			const response = await fetch(path, {headers});
			if (!response.ok) throw await readApiError(response, `GET ${path}`);
			return response.blob();
		},
		parsers(signal?: AbortSignal): Promise<{ parsers: import('./types').DownloaderParser[] }> {
			return request('/download_sessions/parsers', {signal});
		},
        options(signal?: AbortSignal): Promise<import('./types').DownloaderOptions> {
            return request('/download_sessions/options', {signal});
        },
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
        submitUrlSession(url: string, name?: string, signal?: AbortSignal, options?: import('./types').DownloadSettings): Promise<{
            session: DownloadSessionInfo;
            url: DownloadSessionUrlNode;
        }> {
            return request('/download_sessions/urls', {
                method: 'POST',
                body: {...(name === undefined ? {url} : {url, name}), ...(options === undefined ? {} : {options})},
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

	deduplicateUrls(signal?: AbortSignal): Promise<JobDispatch> {
		return request<JobDispatch>('/jobs/urls/deduplicate', {method: 'POST', signal});
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

/** Every frame's `event:` names its stream; the payload's `type` field names the event within it. */
export const EVENT_STREAMS = {
    downloader: 'downloader',
    job: 'job',
	subscriptions: 'subscriptions',
} as const;

export type EventStream = typeof EVENT_STREAMS[keyof typeof EVENT_STREAMS];

type StreamPayload = Record<string, unknown>;

type StreamHandlers = {
    onEvent: (stream: string, payload: StreamPayload) => void;
    onError?: (message: string) => void;
    onOpen?: () => void;
};

type EventSubscription = {
    streams: readonly EventStream[] | null;
    handlers: StreamHandlers;
    sessionIds?: readonly number[];
};

type SharedStream = {
    subscriptions: Set<EventSubscription>;
    close: () => void;
    path: string | null;
};

let sharedStream: SharedStream | null = null;

function streamPath(subscriptions: Set<EventSubscription>): string | null {
    if (subscriptions.size === 0) return null;
    const streams = new Set<string>();
    for (const subscription of subscriptions) {
        if (subscription.streams === null || subscription.streams.length === 0) return '/events';
        for (const stream of subscription.streams) streams.add(stream);
    }
    const downloader = [...subscriptions].filter(s => s.streams?.includes(EVENT_STREAMS.downloader));
    const ids = downloader.length && downloader.every(s => s.sessionIds?.length)
        ? [...new Set(downloader.flatMap(s => [...s.sessionIds!]))].sort((a, b) => a - b) : [];
    return `/events?filter=${encodeURIComponent([...streams].sort().join(','))}${ids.length ? `&session_ids=${ids.join(',')}` : ''}`;
}

function matchesSubscription(subscription: EventSubscription, stream: string, payload: StreamPayload): boolean {
    if (subscription.streams?.length && !subscription.streams.includes(stream as EventStream)) return false;
    if (stream !== EVENT_STREAMS.downloader || !subscription.sessionIds?.length) return true;
    const id = payload.session_id;
    return typeof id !== 'number' || subscription.sessionIds.includes(id);
}

function startSharedStream(stream: SharedStream): void {
    const path = streamPath(stream.subscriptions);
    if (path === stream.path) return;

    stream.close();
    stream.path = path;
    if (path === null) return;

    stream.close = watchSSE(path, {
        onOpen: () => {
            for (const subscription of [...stream.subscriptions]) subscription.handlers.onOpen?.();
        },
        onError: (message) => {
            for (const subscription of [...stream.subscriptions]) subscription.handlers.onError?.(message);
        },
        onEvent: (name, data) => {
            const parsed = safeParse(data);
            if (typeof parsed !== 'object' || parsed === null) return;
            const payload = parsed as StreamPayload;
            for (const subscription of [...stream.subscriptions]) {
                if (matchesSubscription(subscription, name, payload)) subscription.handlers.onEvent(name, payload);
            }
        },
    });
}

/** One shared connection; adding or removing subscribers reconnects once with the union of their filters. */
function sharedWatchSSE(
    streams: readonly EventStream[] | null,
    handlers: StreamHandlers,
    sessionIds?: readonly number[],
): () => void {
    if (sharedStream === null) {
        sharedStream = {
            subscriptions: new Set(), close: () => {
            }, path: null,
        };
    }
    const stream = sharedStream;
    const subscription = {streams, handlers, sessionIds};
    stream.subscriptions.add(subscription);
    startSharedStream(stream);

    return () => {
        stream.subscriptions.delete(subscription);
        startSharedStream(stream);
        if (stream.subscriptions.size === 0 && sharedStream === stream) sharedStream = null;
    };
}

/** Raw events from the selected streams. Omit sessionIds for all download sessions. Nothing is replayed. */
export function watchEvents(
    handlers: {
        onEvent: (event: { stream: string; type: string; payload: StreamPayload }) => void;
        onError?: (message: string) => void;
        onOpen?: () => void;
    },
    streams?: readonly EventStream[],
    sessionIds?: readonly number[],
): () => void {
    const selected = streams?.length ? [...new Set(streams)].sort() : [];
    return sharedWatchSSE(selected.length ? selected : null, {
        onOpen: handlers.onOpen,
        onError: handlers.onError,
        onEvent: (stream, payload) => handlers.onEvent({
            stream, type: typeof payload.type === 'string' ? payload.type : '', payload,
        }),
    }, sessionIds);
}

function watchSSE(path: string, handlers: {
    onEvent: (name: string, data: string) => void;
    onError?: (message: string) => void;
    onOpen?: () => void
}): () => void {
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

            const dispatch = () => {
                if (data.length > 0) handlers.onEvent(eventName, data.join('\n'));
                eventName = 'message';
                data = [];
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
                if (field === 'data') data.push(value);
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

const DOWNLOAD_URL_STATES: readonly DownloadSessionUrlState[] = [
    'pending', 'downloading', 'processing', 'importing', 'completed', 'imported', 'imported_previously',
    'skipped_no_url_class', 'skipped_already_seen', 'skipped_already_imported', 'failed', 'cancelled',
];

const NETWORK_FIELDS = [
    'downloading', 'waiting', 'waiting_parsers', 'waiting_requests', 'waiting_http_rate', 'waiting_http_shard',
	'waiting_storage', 'downloaded_bytes', 'average_import_bytes',
] as const;

type DownloadSessionListener = {
    onSession: (session: DownloadSessionInfo, urls: DownloadSessionUrlNode[]) => void;
	onBandwidth?: (bandwidth: DownloadBandwidth) => void;
    onRemoved?: (sessionId: number) => void;
    onError?: (message: string) => void;
    onOpen?: () => void;
    sessionIds?: readonly number[];
};

type StoredSession = {
    info: DownloadSessionInfo;
    rows: Map<number, DownloadSessionUrlJob>;
    errors: Set<number>;
    /** Rebuilt only after a URL changes, so listeners can tell URL changes from summary changes. */
    tree: DownloadSessionUrlNode[] | null;
};

type DownloaderStore = {
    sessions: Map<number, StoredSession>;
	bandwidth: DownloadBandwidth | null;
    /** Set once the connection's snapshot has fully arrived. */
    ready: boolean;
    opened: boolean;
    listeners: Set<DownloadSessionListener>;
    dirty: Set<number>;
    flushQueued: boolean;
    sessionFilter: string;
    close: () => void;
};

let downloaderStore: DownloaderStore | null = null;

function optionalNumber(value: unknown): number | null {
    return typeof value === 'number' ? value : null;
}

function optionalString(value: unknown): string | null {
    return typeof value === 'string' ? value : null;
}

function isUrlState(value: unknown): value is DownloadSessionUrlState {
    return typeof value === 'string' && (DOWNLOAD_URL_STATES as readonly string[]).includes(value);
}

/** Applies a payload's state fields; fields it omits are cleared. */
function applyUrlState(row: DownloadSessionUrlJob, payload: StreamPayload): void {
    if (isUrlState(payload.state)) row.state = payload.state;
    row.record_id = optionalNumber(payload.record_id);
    row.size = optionalNumber(payload.size);
    row.stage = optionalString(payload.stage) as DownloadSessionUrlJob['stage'];
    row.error = optionalString(payload.error);
    row.finished_at = optionalNumber(payload.finished_at);
    const transferring = row.state === 'downloading';
    row.bytes = transferring && typeof payload.bytes === 'number' ? payload.bytes : undefined;
    row.total = transferring && typeof payload.total === 'number' ? payload.total : undefined;
}

function urlRow(payload: StreamPayload): DownloadSessionUrlJob | null {
    if (typeof payload.url_id !== 'number' || typeof payload.url !== 'string') return null;
    const row: DownloadSessionUrlJob = {
        id: payload.url_id,
        parent_id: optionalNumber(payload.parent_id),
        url: payload.url,
        state: 'pending',
        created_at: optionalNumber(payload.created_at) ?? 0,
        finished_at: null,
        error: null,
        stage: null,
        record_id: null,
        size: null,
    };
    applyUrlState(row, payload);
    return row;
}

function buildTree(rows: Map<number, DownloadSessionUrlJob>): DownloadSessionUrlNode[] {
    const nodes = new Map<number, DownloadSessionUrlNode>();
    const ordered = [...rows.values()].sort((a, b) => a.id - b.id);
    for (const row of ordered) nodes.set(row.id, {...row, children: []});
    const roots: DownloadSessionUrlNode[] = [];
    for (const node of nodes.values()) {
        const parent = node.parent_id === null ? undefined : nodes.get(node.parent_id);
        if (parent) parent.children.push(node);
        else roots.push(node);
    }
    // Newest submissions first, matching the REST listing.
    return roots.reverse();
}

function countStates(rows: Map<number, DownloadSessionUrlJob>): DownloadSessionCounts {
    const counts = Object.fromEntries(DOWNLOAD_URL_STATES.map(state => [state, 0])) as DownloadSessionCounts;
    for (const row of rows.values()) counts[row.state] += 1;
    return counts;
}

function removeSubtree(rows: Map<number, DownloadSessionUrlJob>, id: number): void {
    const removed = new Set([id]);
    // Children always have higher ids than their parents, so one ordered pass finds every descendant.
    for (const row of [...rows.values()].sort((a, b) => a.id - b.id)) {
        if (row.parent_id !== null && removed.has(row.parent_id)) removed.add(row.id);
    }
    for (const value of removed) rows.delete(value);
}

function listenerWants(listener: DownloadSessionListener, sessionId: number): boolean {
    return !listener.sessionIds?.length || listener.sessionIds.includes(sessionId);
}

function sessionView(stored: StoredSession): [DownloadSessionInfo, DownloadSessionUrlNode[]] {
    if (stored.tree === null) stored.tree = buildTree(stored.rows);
    return [{...stored.info, counts: countStates(stored.rows), error_count: stored.errors.size}, stored.tree];
}

function flushDownloader(store: DownloaderStore): void {
    store.flushQueued = false;
	if (!store.ready) return;

    const dirty = [...store.dirty];
    store.dirty.clear();
    for (const id of dirty) {
        const stored = store.sessions.get(id);
        if (!stored) continue;
        const [info, tree] = sessionView(stored);
        for (const listener of [...store.listeners]) {
            if (listenerWants(listener, id)) listener.onSession(info, tree);
        }
    }
}

function markDirty(store: DownloaderStore, sessionId: number): void {
    store.dirty.add(sessionId);
	// A baseline can arrive across many reads. Rebuilding its growing trees for each chunk
	// repeats sorting, counting, metadata invalidation, and rendering before ingestion finishes.
	if (!store.ready) return;

    if (store.flushQueued) return;
    store.flushQueued = true;
    queueMicrotask(() => flushDownloader(store));
}

function upsertSession(store: DownloaderStore, payload: StreamPayload, id: number): StoredSession | undefined {
    if (typeof payload.name !== 'string') return store.sessions.get(id);
    const existing = store.sessions.get(id);
    const info: DownloadSessionInfo = {
        ...existing?.info,
        id,
        name: payload.name,
        created_at: optionalNumber(payload.created_at) ?? existing?.info.created_at ?? 0,
        last_used_at: optionalNumber(payload.last_used_at) ?? existing?.info.last_used_at ?? 0,
        state: (optionalString(payload.state) as DownloadSessionState | null) ?? existing?.info.state ?? 'idle',
        error_count: existing?.errors.size ?? 0,
    };
    if (existing) {
        existing.info = info;
        return existing;
    }
    const created: StoredSession = {info, rows: new Map(), errors: new Set(), tree: null};
    store.sessions.set(id, created);
    return created;
}

function applyUrlEvent(stored: StoredSession, type: string, payload: StreamPayload): boolean {
    if (type === 'urls_snapshot' || type === 'url_created') {
        const entries = type === 'urls_snapshot' && Array.isArray(payload.urls) ? payload.urls : [payload];
        for (const entry of entries) {
            if (typeof entry !== 'object' || entry === null) continue;
            const row = urlRow(entry as StreamPayload);
            if (row) stored.rows.set(row.id, row);
        }
        return true;
    }
    if (typeof payload.url_id !== 'number') return false;
    const row = stored.rows.get(payload.url_id);
    if (type === 'url_removed') {
        removeSubtree(stored.rows, payload.url_id);
        return true;
    }
    if (!row) return false;
    if (type === 'url_updated') {
        stored.rows.set(row.id, {...row});
        applyUrlState(stored.rows.get(row.id)!, payload);
        return true;
    }
    if (type === 'url_progress') {
        stored.rows.set(row.id, {
            ...row,
            bytes: optionalNumber(payload.bytes) ?? undefined,
            total: optionalNumber(payload.total) ?? undefined,
        });
        return true;
    }
    return false;
}

function applyErrorEvent(stored: StoredSession, type: string, payload: StreamPayload): boolean {
    if (type === 'errors_snapshot' && Array.isArray(payload.errors)) {
        for (const entry of payload.errors) {
            const id = (entry as StreamPayload | null)?.error_id;
            if (typeof id === 'number') stored.errors.add(id);
        }
        return true;
    }
    if (type === 'request_error' && typeof payload.error_id === 'number') {
        stored.errors.add(payload.error_id);
        return true;
    }
    if (type === 'request_errors_cleared' && Array.isArray(payload.error_ids)) {
        for (const id of payload.error_ids) if (typeof id === 'number') stored.errors.delete(id);
        return true;
    }
    return false;
}

function applyDownloaderEvent(store: DownloaderStore, payload: StreamPayload): void {
    const type = typeof payload.type === 'string' ? payload.type : '';
	if (type === 'bandwidth') {
		const speed = payload.download_bytes_per_second;
		if (typeof speed !== 'number' || !Number.isFinite(speed) || speed < 0 || !Array.isArray(payload.sessions)) return;
		const sessions: DownloadBandwidth['sessions'] = [];
		for (const value of payload.sessions) {
			if (typeof value !== 'object' || value === null) return;
			const entry = value as StreamPayload;
			if (typeof entry.session_id !== 'number' || typeof entry.download_bytes_per_second !== 'number') return;
			if (!Number.isFinite(entry.download_bytes_per_second) || entry.download_bytes_per_second < 0) return;
			sessions.push({session_id: entry.session_id, download_bytes_per_second: entry.download_bytes_per_second});
		}
		store.bandwidth = {download_bytes_per_second: speed, sessions};
		for (const listener of [...store.listeners]) publishBandwidth(listener, store.bandwidth);
		return;
	}
    if (type === 'snapshot_complete') {
        store.ready = true;
		for (const id of store.dirty) markDirty(store, id);
        return;
    }
    const id = payload.session_id;
    if (typeof id !== 'number') return;
    if (type === 'session_deleted') {
        store.sessions.delete(id);
        store.dirty.delete(id);
        for (const listener of [...store.listeners]) if (listenerWants(listener, id)) listener.onRemoved?.(id);
        return;
    }
    if (type === 'session_snapshot' || type === 'session_created') {
		if (payload.is_subscription === true) return;
        upsertSession(store, payload, id);
        markDirty(store, id);
        return;
    }
    const stored = store.sessions.get(id);
    if (!stored) return;
    if (type === 'session_state') {
        if (typeof payload.state === 'string') stored.info = {
            ...stored.info,
            state: payload.state as DownloadSessionState
        };
        if (typeof payload.last_used_at === 'number') stored.info = {
            ...stored.info,
            last_used_at: payload.last_used_at
        };
        markDirty(store, id);
        return;
    }
    if (type === 'network') {
        const network: Partial<DownloadSessionInfo> = {};
        for (const field of NETWORK_FIELDS) {
            const value = payload[field];
            if (typeof value === 'number') network[field] = value;
        }
        stored.info = {...stored.info, ...network};
        markDirty(store, id);
        return;
    }
    if (applyUrlEvent(stored, type, payload)) {
        stored.tree = null;
        markDirty(store, id);
        return;
    }
    if (applyErrorEvent(stored, type, payload)) markDirty(store, id);
}

function downloaderSessionFilter(store: DownloaderStore): readonly number[] | undefined {
    const listeners = [...store.listeners];
    if (listeners.length === 0 || !listeners.every(listener => listener.sessionIds?.length)) return undefined;
    return [...new Set(listeners.flatMap(listener => [...listener.sessionIds!]))].sort((a, b) => a - b);
}

function publishBandwidth(listener: DownloadSessionListener, bandwidth: DownloadBandwidth): void {
	listener.onBandwidth?.({
		download_bytes_per_second: bandwidth.download_bytes_per_second,
		sessions: bandwidth.sessions.filter(session => listenerWants(listener, session.session_id)),
	});
}

function connectDownloader(store: DownloaderStore): void {
    const filter = downloaderSessionFilter(store);
    const key = filter?.join(',') ?? '';
    if (key === store.sessionFilter && store.opened) return;

    const previous = store.close;
    store.sessionFilter = key;
    store.opened = true;
    store.close = sharedWatchSSE([EVENT_STREAMS.downloader], {
        onOpen: () => {
            store.sessions.clear();
			store.bandwidth = null;
            store.dirty.clear();
            store.ready = false;
            for (const listener of [...store.listeners]) listener.onOpen?.();
        },
        onError: (message) => {
            for (const listener of [...store.listeners]) listener.onError?.(message);
        },
        onEvent: (_stream, payload) => applyDownloaderEvent(store, payload),
    }, filter);
    previous();
}

function downloader(): DownloaderStore {
    if (downloaderStore === null) {
        downloaderStore = {
			sessions: new Map(), bandwidth: null, ready: false, opened: false, listeners: new Set(), dirty: new Set(),
            flushQueued: false, sessionFilter: '', close: () => {
            },
        };
    }
    return downloaderStore;
}

function releaseDownloader(store: DownloaderStore): void {
    if (store.listeners.size > 0) {
        connectDownloader(store);
        return;
    }
    store.close();
    if (downloaderStore === store) downloaderStore = null;
}

/** Sessions with their URL trees, kept current from the downloader stream and shared by every caller. */
export function watchDownloadSessions(handlers: {
    onSession: (session: DownloadSessionInfo, urls: DownloadSessionUrlNode[]) => void;
	onBandwidth?: (bandwidth: DownloadBandwidth) => void;
    onRemoved?: (sessionId: number) => void;
    onError?: (message: string) => void;
    onOpen?: () => void;
}, options: { sessionIds?: readonly number[] } = {}): () => void {
    const store = downloader();
    const listener: DownloadSessionListener = {...handlers, sessionIds: options.sessionIds};
    store.listeners.add(listener);
    const reconnects = store.opened && store.sessionFilter !== (downloaderSessionFilter(store)?.join(',') ?? '');
    connectDownloader(store);
    if (!reconnects && store.sessions.size > 0) {
        queueMicrotask(() => {
            if (!store.listeners.has(listener)) return;
            listener.onOpen?.();
			if (store.bandwidth) publishBandwidth(listener, store.bandwidth);
			if (!store.ready) return;

            for (const [id, stored] of store.sessions) {
                if (listenerWants(listener, id)) listener.onSession(...sessionView(stored));
            }
        });
    }
    return () => {
        store.listeners.delete(listener);
        releaseDownloader(store);
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

function applyJobEvent(stream: JobStream, payload: StreamPayload): boolean {
    if (payload.type === 'snapshot') {
        const jobs = payload.jobs;
        if (!Array.isArray(jobs) || !jobs.every(isJobStatus)) return false;
        stream.jobs = new Map(jobs.map(job => [job.job_id, job]));
        return true;
	}
	if (payload.type === 'response' && typeof payload.job_id === 'number') {
		const job = stream.jobs.get(payload.job_id);
		if (!job) return false;
		stream.jobs.set(payload.job_id, {...job, response: payload.response});
		return true;
    }
    if (payload.type !== 'status') return false;
    if (typeof payload.job_id === 'number' && payload.removed === true) {
        stream.jobs.delete(payload.job_id);
        return true;
    }
    if (!isJobStatus(payload)) return false;
    stream.jobs.set(payload.job_id, payload);
    return true;
}

/** One shared connection and current snapshot for every job consumer, including late subscribers. */
export function watchJobs(handlers: JobHandlers): () => void {
    if (!jobStream) {
        const stream: JobStream = {
            jobs: new Map(), handlers: new Set(), ready: false, close: () => {
            }
        };
        jobStream = stream;
        stream.close = sharedWatchSSE([EVENT_STREAMS.job], {
            onOpen: () => {
                stream.ready = true;
                stream.jobs.clear();
                for (const handler of [...stream.handlers]) handler.onJobs([]);
            },
            onError: (message) => {
                for (const handler of [...stream.handlers]) handler.onError?.(message);
            },
            onEvent: (_stream, payload) => {
                if (!applyJobEvent(stream, payload)) return;
                if (!stream.ready) return;
                const jobs = [...stream.jobs.values()].sort((a, b) => a.job_id - b.job_id);
                for (const handler of [...stream.handlers]) handler.onJobs(jobs);
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
