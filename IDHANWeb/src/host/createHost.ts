/**
 * Builds the HostApi handed to one panel instance. Global services (selection, bus, theme, toasts)
 * come from globalServices; per-instance settings come from a binding the Workspace wires to the
 * layout store, so the host stays decoupled from the store's internals.
 */

import { api, getKey, tags as tagApi } from '../api/client';
import { thumbnailUrl as buildThumbnailUrl, fileUrl as buildFileUrl } from '../api/client';
import { getMetadata } from './metadataCache';
import { autocomplete } from './autocompleteCache';
import { resolveTags } from './tagInfoCache';
import { globalServices } from './globalServices';
import type { HostApi, JobHandle, PanelInstanceId, SettingsApi } from './types';
import { HOST_API_VERSION } from './types';

/** A panel-instance settings object, backed by the layout store. */
export type SettingsBinding = SettingsApi;

const KEY_HEADER = 'X-API-Key';
const JOB_POLL_MS = 750;

function authHeaders(init?: HeadersInit): Headers {
  const headers = new Headers(init);
  const key = getKey();
  if (key && !headers.has(KEY_HEADER)) headers.set(KEY_HEADER, key);
  return headers;
}

function isTerminalStatus(status: unknown): boolean {
  if (typeof status !== 'object' || status === null) return false;
  const value = (status as { status?: unknown }).status;
  return value === 'completed' || value === 'failed' || value === 'error' || value === 'cancelled';
}

/**
 * Polls a job to completion, treating a 404 that follows a previously-observed non-terminal status as
 * completed-and-reaped rather than an error — the reap-on-first-terminal-poll trap, handled here so no
 * panel has to know about it.
 */
function watchJob(jobId: number, onUpdate: (status: unknown) => void): JobHandle {
  let cancelled = false;
  let sawNonTerminal = false;

  const tick = async (): Promise<void> => {
    if (cancelled) return;
    try {
      const res = await fetch(`/jobs/${jobId}/status`, { headers: authHeaders() });
      if (res.status === 404) {
        if (sawNonTerminal) {
          onUpdate({ status: 'completed', reaped: true });
          return;
        }
      } else if (res.ok) {
        const data: unknown = await res.json();
        onUpdate(data);
        if (isTerminalStatus(data)) return;
        sawNonTerminal = true;
      }
    } catch {
      // transient; keep polling
    }
    if (!cancelled) setTimeout(() => void tick(), JOB_POLL_MS);
  };

  void tick();
  return {
    cancel: () => {
      cancelled = true;
    },
  };
}

export function createHost(instanceId: PanelInstanceId, settings: SettingsBinding): HostApi {
  return {
    version: HOST_API_VERSION,
    instanceId,
    search: {
      run: (request, signal) => api.search(request, signal),
    },
    records: {
      getMetadata: (ids, include) => getMetadata(ids, include),
      thumbnailUrl: (id, size) => buildThumbnailUrl(id, size),
      fileUrl: (id, opts) => buildFileUrl(id, opts),
    },
    tags: {
      autocomplete: (prefix, opts, signal) => autocomplete(prefix, opts, signal),
      listDomains: (signal) => tagApi.listDomains(signal),
      activeVerbose: (recordId, signal) => tagApi.activeVerbose(recordId, signal),
      resolve: (tagIds) => resolveTags(tagIds),
      addToRecords: (recordIds, tagsToAdd, tagDomainId, signal) =>
        tagApi.addToRecords([...recordIds], [...tagsToAdd], tagDomainId, signal),
      removeFromRecords: (recordIds, tagIds, tagDomainId, signal) =>
        tagApi.removeFromRecords([...recordIds], [...tagIds], tagDomainId, signal),
    },
    selection: globalServices.selection,
    results: globalServices.results,
    bus: globalServices.bus,
    settings,
    theme: {
      mode: globalServices.theme.mode,
      subscribe: globalServices.theme.subscribe,
    },
    ui: {
      toast: (message, options) => globalServices.toasts.push(message, options),
    },
    jobs: {
      watch: watchJob,
    },
    http: {
      fetch: (input, init) => fetch(input, { ...init, headers: authHeaders(init?.headers) }),
    },
  };
}
