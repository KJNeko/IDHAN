import {describe, expect, it} from 'vitest';
import {
    appendEvents,
    buildSortJobs,
    cursorsFrom,
    describeEvent,
    emptyEventBuffer,
    formatAge,
    formatEffectiveRateLimitRate,
    formatRateLimitRate,
    isRateLimitActive,
    mergeEventBuffers,
    mergeRateLimits,
    remainingRateLimitMs,
    sortRowsBySlowest,
    type SessionEventBuffer,
    type SortProfileRow,
} from './DebugPanel';
import {isRateLimitLane} from '../../api/client';
import type {DebugSession, DebugSessionEvent, RateLimitLane} from '../../api/types';

describe('buildSortJobs', () => {
    it('produces one job per sort option x direction, desc before asc', () => {
        const jobs = buildSortJobs([
            {value: 'mime', label: 'Filetype'},
            {value: 'hash', label: 'Hash'},
        ]);
        expect(jobs).toEqual([
            {value: 'mime', label: 'Filetype', order: 'desc'},
            {value: 'mime', label: 'Filetype', order: 'asc'},
            {value: 'hash', label: 'Hash', order: 'desc'},
            {value: 'hash', label: 'Hash', order: 'asc'},
        ]);
    });

    it('defaults to every registered sort option', () => {
        const jobs = buildSortJobs();
        expect(jobs.length).toBeGreaterThan(0);
        expect(jobs.length % 2).toBe(0); // desc + asc per option
    });
});

describe('rate limit helpers', () => {
    const lane = (scheduling_key: string, remaining_ms = 5000): RateLimitLane => ({
        scheduling_key, throttled: true, requests: 1, seconds: 5, effective_interval_ms: 5000,
        remaining_ms, consecutive_limits: 0, active: remaining_ms > 0,
    });

    it('replaces opening state, upserts updates, and orders lanes by key', () => {
        const opening = mergeRateLimits([], [lane('host:z.example'), lane('host:a.example')], 100);
        const updated = mergeRateLimits(opening, [lane('host:a.example', 2500)], 200);
        expect(updated.map((entry) => entry.scheduling_key)).toEqual(['host:a.example', 'host:z.example']);
        expect(updated[0]?.expiresAt).toBe(2700);
        expect(mergeRateLimits(updated, [lane('host:b.example')], 300, true).map((entry) => entry.scheduling_key)).toEqual(['host:b.example']);
    });

    it('expires an active lane locally from its captured remaining duration', () => {
        const [limit] = mergeRateLimits([], [lane('host:one.example', 1200)], 1000);
        expect(isRateLimitActive(limit!, 2199)).toBe(true);
        expect(remainingRateLimitMs(limit!, 2200)).toBe(0);
        expect(isRateLimitActive(limit!, 2200)).toBe(false);
    });

    it('formats configured and effective rates', () => {
        expect(formatRateLimitRate(lane('host:one.example'))).toBe('1 request / 5 seconds');
        expect(formatRateLimitRate({
            ...lane('host:two.example'),
            requests: 2,
            seconds: 1
        })).toBe('2 requests / 1 second');
        expect(formatEffectiveRateLimitRate(8000)).toBe('1 request / 8 seconds');
        expect(formatEffectiveRateLimitRate(2500)).toBe('1 request / 2.5 seconds');
    });

    it('accepts and labels an unthrottled lane', () => {
        const unlimited: RateLimitLane = {
            ...lane('host:unlimited.example', 0),
            throttled: false,
            requests: 0,
            seconds: 0,
            effective_interval_ms: 0,
        };

        expect(isRateLimitLane(unlimited)).toBe(true);
        expect(formatRateLimitRate(unlimited)).toBe('Unlimited');
    });
});

describe('sortRowsBySlowest', () => {
    const row = (value: string, queryMs: number): SortProfileRow => ({
        value: value as SortProfileRow['value'],
        label: value,
        order: 'desc',
        queryMs,
        roundTripMs: queryMs + 5,
        bytes: 100,
        count: 10,
        truncated: false,
    });

    it('orders rows by queryMs descending', () => {
        const rows = [row('fast', 5), row('slow', 500), row('mid', 50)];
        expect(sortRowsBySlowest(rows).map((r) => r.value)).toEqual(['slow', 'mid', 'fast']);
    });

    it('does not mutate the input array', () => {
        const rows = [row('a', 1), row('b', 2)];
        const original = [...rows];
        sortRowsBySlowest(rows);
        expect(rows).toEqual(original);
    });

    it('returns an empty array for no rows', () => {
        expect(sortRowsBySlowest([])).toEqual([]);
    });
});

function event(sequence: number, overrides: Partial<DebugSessionEvent> = {}): DebugSessionEvent {
    return {
        sequence,
        at: 1_700_000_000_000_000,
        kind: 'started',
        work_id: 1,
        url_id: null,
        url: 'https://example.invalid/a',
        detail: '',
        status: 0,
        bytes: 0,
        record_id: null,
        ...overrides,
    };
}

function session(id: number, events: DebugSessionEvent[], dropped = 0): DebugSession {
    return {
        id,
        name: `session ${id}`,
        root_url: 'https://example.invalid',
        state: 'running',
        closed: false,
        cancelled: false,
        idle: false,
        queued: 0,
        running: 0,
        in_flight: 0,
        in_flight_limit: 64,
        outstanding: 0,
        counters: {
            work_started: 0,
            work_completed: 0,
            work_failed: 0,
            requests: 0,
            request_bytes: 0,
            imported: 0,
            import_bytes: 0,
            import_failed: 0,
            follows_queued: 0,
            follows_filtered: 0,
            follows_already_queued: 0,
            follows_already_explored: 0,
            follows_already_imported: 0,
        },
        work: [],
        requests: [],
        events,
        event_sequence: events.length === 0 ? 0 : events[events.length - 1]!.sequence,
        events_dropped: dropped,
    };
}

describe('appendEvents', () => {
    it('appends new events and advances the cursor to the highest sequence', () => {
        const buffer = appendEvents(emptyEventBuffer(), [event(1), event(2), event(3)], 0);
        expect(buffer.events.map((e) => e.sequence)).toEqual([1, 2, 3]);
        expect(buffer.cursor).toBe(3);
        expect(buffer.dropped).toBe(0);
    });

    it('ignores events at or below the cursor so an overlapping poll cannot duplicate them', () => {
        const first = appendEvents(emptyEventBuffer(), [event(1), event(2)], 0);
        const second = appendEvents(first, [event(1), event(2), event(3)], 0);
        expect(second.events.map((e) => e.sequence)).toEqual([1, 2, 3]);
        expect(second.cursor).toBe(3);
    });

    it('returns the same buffer when a poll carries nothing new', () => {
        const first = appendEvents(emptyEventBuffer(), [event(1)], 0);
        expect(appendEvents(first, [], 0)).toBe(first);
        expect(appendEvents(first, [event(1)], 0)).toBe(first);
    });

    it('accumulates the server drop count across polls', () => {
        const first = appendEvents(emptyEventBuffer(), [event(1)], 4);
        const second = appendEvents(first, [event(2)], 7);
        expect(second.dropped).toBe(11);
    });

    it('records a drop even when the poll carried no new events', () => {
        const first = appendEvents(emptyEventBuffer(), [event(1)], 0);
        expect(appendEvents(first, [], 3).dropped).toBe(3);
    });

    it('trims the oldest events past the cap', () => {
        const incoming = Array.from({length: 10}, (_, index) => event(index + 1));
        const buffer = appendEvents(emptyEventBuffer(), incoming, 0, 4);
        expect(buffer.events.map((e) => e.sequence)).toEqual([7, 8, 9, 10]);
        expect(buffer.cursor).toBe(10);
    });
});

describe('mergeEventBuffers', () => {
    it('starts a buffer for a session it has not seen before', () => {
        const merged = mergeEventBuffers(new Map(), [session(1, [event(1), event(2)])]);
        expect(merged.get(1)?.cursor).toBe(2);
    });

    it('carries an existing buffer forward across polls', () => {
        const first = mergeEventBuffers(new Map(), [session(1, [event(1)])]);
        const second = mergeEventBuffers(first, [session(1, [event(2)])]);
        expect(second.get(1)?.events.map((e) => e.sequence)).toEqual([1, 2]);
    });

    it('forgets a session the server no longer reports', () => {
        const first = mergeEventBuffers(new Map(), [session(1, [event(1)]), session(2, [event(1)])]);
        const second = mergeEventBuffers(first, [session(2, [event(2)])]);
        expect([...second.keys()]).toEqual([2]);
    });
});

describe('cursorsFrom', () => {
    it('maps each session to its highest seen sequence', () => {
        const buffers = mergeEventBuffers(new Map(), [session(1, [event(9)]), session(2, [event(4)])]);
        expect([...cursorsFrom(buffers)]).toEqual([[1, 9], [2, 4]]);
    });

    it('omits a session with no events so it receives the whole ring', () => {
        const buffers = new Map<number, SessionEventBuffer>([[1, emptyEventBuffer()]]);
        expect(cursorsFrom(buffers).size).toBe(0);
    });
});

describe('formatAge', () => {
    it('uses milliseconds below a second', () => {
        expect(formatAge(0)).toBe('0 ms');
        expect(formatAge(999)).toBe('999 ms');
    });

    it('uses seconds below a minute', () => {
        expect(formatAge(1000)).toBe('1.0 s');
        expect(formatAge(59_400)).toBe('59.4 s');
    });

    it('uses minutes and seconds above a minute', () => {
        expect(formatAge(60_000)).toBe('1m 0s');
        expect(formatAge(125_000)).toBe('2m 5s');
    });
});

describe('describeEvent', () => {
    it('leads a request with its status', () => {
        const line = describeEvent(event(1, {kind: 'request', status: 404, bytes: 0, detail: 'example.invalid'}));
        expect(line.startsWith('404 ')).toBe(true);
        expect(line).toContain('example.invalid');
    });

    it('names the record an import produced', () => {
        expect(describeEvent(event(1, {kind: 'imported', record_id: 88_213}))).toContain('record 88213');
    });

    it('reports the follow outcome', () => {
        expect(describeEvent(event(1, {kind: 'followed', detail: 'already_imported'})))
            .toContain('already_imported');
    });

    it('puts the error after the url for a failure', () => {
        expect(describeEvent(event(1, {kind: 'failed', url: 'https://x', detail: 'TypeError'})))
            .toBe('https://x TypeError');
    });

    it('does not invent a url for a drain', () => {
        expect(describeEvent(event(1, {kind: 'finished', url: ''}))).toBe('session drained');
    });
});
