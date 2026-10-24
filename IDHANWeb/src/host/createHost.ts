/**
 * Builds the HostApi handed to one panel instance. Global services (selection, bus, theme, toasts)
 * come from globalServices; per-instance settings come from a binding the Workspace wires to the
 * layout store, so the host stays decoupled from the store's internals.
 */

import {watchJobs, api, getKey, stats as statsApi, tags as tagApi} from '../api/client';
import { thumbnailUrl as buildThumbnailUrl, fileUrl as buildFileUrl } from '../api/client';
import {forgetMetadata, getMetadata} from './metadataCache';
import { autocomplete } from './autocompleteCache';
import { resolveTags } from './tagInfoCache';
import { globalServices } from './globalServices';
import type {HostApi, JobHandle, PanelInstanceId, RecordId, SettingsApi} from './types';
import { HOST_API_VERSION } from './types';

/** A panel-instance settings object, backed by the layout store. */
export type SettingsBinding = SettingsApi;

const KEY_HEADER = 'X-API-Key';

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

/** Watches the shared jobs stream until completion or cancellation. */
function watchJob(jobId: number, onUpdate: (status: unknown) => void): JobHandle {
  let cancelled = false;
    const close = watchJobs({
        onJobs: (jobs) => {
            if (cancelled) return;
            const job = jobs.find(value => value.job_id === jobId);
            if (!job) return;
            onUpdate(job);
            if (isTerminalStatus(job)) {
                cancelled = true;
                close();
            }
    },
    });
    return {
        cancel: () => {
            cancelled = true;
            close();
        }
    };
}

/**
 * Reparses metadata server-side, settling when the job does rather than when it is queued: the
 * cached copies are only worth dropping once the new ones exist.
 */
async function regenerateMetadata(ids: readonly RecordId[], signal?: AbortSignal): Promise<void> {
    const dispatched = await api.rescanMetadata([...ids], signal);
    const final = await new Promise<unknown>((resolve) => {
        watchJob(dispatched.job_id, (status) => {
            if (isTerminalStatus(status)) resolve(status);
        });
    });
    forgetMetadata(ids);

    const state = (final as { status?: unknown }).status;
    if (state === 'failed' || state === 'error') {
        const reason = (final as { error?: unknown }).error;
        throw new Error(typeof reason === 'string' ? reason : 'Metadata regeneration failed');
    }
}

export function createHost(instanceId: PanelInstanceId, settings: SettingsBinding): HostApi {
  return {
    version: HOST_API_VERSION,
    instanceId,
    search: {
      run: (request, signal) => api.search(request, signal),
    },
    records: {
        getMetadata: (ids) => getMetadata(ids),
        relationships: (id, signal) => api.recordRelationships(id, signal),
        similar: (id, opts, signal) => api.recordsSimilar(id, opts, signal),
        nextUndecidedDuplicate: (opts, signal) => api.nextUndecidedDuplicate(opts, signal),
        setBetterDuplicate: (worseId, betterId, signal) => api.setBetterDuplicate(worseId, betterId, signal),
        addAlternatives: (ids, signal) => api.addAlternatives([...ids], signal),
        setUnrelated: (idA, idB, signal) => api.setUnrelated(idA, idB, signal),
        clearRelationship: (idA, idB, signal) => api.clearRelationship(idA, idB, signal),
        regenerateMetadata: (ids, signal) => regenerateMetadata(ids, signal),
      thumbnailUrl: (id, size) => buildThumbnailUrl(id, size),
      fileUrl: (id, opts) => buildFileUrl(id, opts),
    },
    tags: {
      autocomplete: (prefix, opts, signal) => autocomplete(prefix, opts, signal),
      listDomains: (signal) => tagApi.listDomains(signal),
      activeVerbose: (recordId, signal) => tagApi.activeVerbose(recordId, signal),
      resolve: (tagIds) => resolveTags(tagIds),
      relationships: (tagDomainId, tagId, signal) => tagApi.relationships(tagDomainId, tagId, signal),
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
    stats: {
      database: (signal) => statsApi.database(signal),
      storage: (signal) => statsApi.storage(signal),
    },
    http: {
      fetch: (input, init) => fetch(input, { ...init, headers: authHeaders(init?.headers) }),
    },
  };
}
