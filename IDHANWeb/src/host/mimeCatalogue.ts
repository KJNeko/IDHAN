/**
 * The mime table, loaded once and kept for the session.
 *
 * GET /mime returns id -> name, and the map is many-to-one: 5000 and 5002 both report
 * "application/zip". Both directions are useful, so the flattened list keeps every id as its own
 * entry and a separate name list carries each distinct name once.
 *
 * The table only changes when the server is rebuilt with new mime ids, so a single load per session
 * is enough; `reloadMimes()` exists for the case where one is added under a running UI.
 */

import {api} from '../api/client';

export interface MimeEntry {
    id: number;
    name: string;
}

export interface MimeCatalogue {
    /** One entry per mime id, ascending. Names repeat where ids share one. */
    entries: readonly MimeEntry[];
    /** Each distinct name once, with the lowest id carrying it: what a bare name search resolves to. */
    names: readonly MimeEntry[];
}

const EMPTY: MimeCatalogue = {entries: [], names: []};

let cached: MimeCatalogue | null = null;
let inFlight: Promise<MimeCatalogue> | null = null;

function build(raw: Record<string, string>): MimeCatalogue {
    const entries = Object.entries(raw)
        .map(([id, name]) => ({id: Number(id), name}))
        .filter((entry) => Number.isInteger(entry.id))
        .sort((a, b) => a.id - b.id);

    const seen = new Set<string>();
    const names: MimeEntry[] = [];
    for (const entry of entries) {
        if (seen.has(entry.name)) continue;
        seen.add(entry.name);
        names.push(entry);
    }

    return {entries, names};
}

/**
 * The catalogue, fetching it on first call. Concurrent callers share one request. A failed load
 * resolves to an empty catalogue and is retried next time rather than cached.
 */
export async function loadMimes(signal?: AbortSignal): Promise<MimeCatalogue> {
    if (cached) return cached;
    if (inFlight) return inFlight;

    inFlight = api
        .listMimes(signal)
        .then((raw) => {
            cached = build(raw);
            return cached;
        })
        .catch(() => EMPTY)
        .finally(() => {
            inFlight = null;
        });

    return inFlight;
}

/** What has been loaded so far, without triggering a fetch. Empty until loadMimes() resolves. */
export const peekMimes = (): MimeCatalogue => cached ?? EMPTY;

/** Test/maintenance hook: drop the catalogue so the next loadMimes() refetches. */
export function reloadMimes(): void {
    cached = null;
}
