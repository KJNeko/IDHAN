import {afterEach, expect, it, vi} from 'vitest';
import {watchSubscriptions} from './subscriptionsStream';
import {watchEvents} from './client';

vi.mock('./client', () => ({watchEvents: vi.fn(() => vi.fn())}));

const initial = {
	type: 'snapshot',
	groups: [{id: 7, name: 'Artists', enabled: true}],
	items: [{
		id: 42, group_id: 7, tags: 'artist', parameters: {tags: 'artist'},
		imported_count: 1, last_error: 'failed', active_session_id: 9
	}],
};

function send(payload: unknown) {
	vi.mocked(watchEvents).mock.lastCall![0].onEvent({
		stream: 'subscriptions', type: '', payload: payload as Record<string, unknown>,
	});
}

afterEach(() => vi.clearAllMocks());

it('merges disjoint fields, preserves omissions and clears explicit nulls', () => {
	const onChange = vi.fn();
	const close = watchSubscriptions({onChange});
	try {
		send(initial);
		send({type: 'patch', items: [{id: 42, imported_count: 18}]});
		send({type: 'patch', items: [{id: 42, last_error: null, active_session_id: null}]});
		expect(onChange.mock.lastCall![0].items).toEqual([{
			...initial.items[0], imported_count: 18, last_error: null, active_session_id: null,
		}]);
		// Parameters are replaced as a complete field, so removed keys do not survive.
		send({type: 'patch', items: [{id: 42, parameters: {}}]});
		expect(onChange.mock.lastCall![0].items[0].parameters).toEqual({});
		expect(initial.items[0].last_error).toBe('failed');
	} finally {
		close();
	}
});

it('shares its store, catches up late listeners, and closes after the final listener', async () => {
	const first = vi.fn();
	const second = vi.fn();
	const closeFirst = watchSubscriptions({onChange: first});
	send(initial);
	const closeSecond = watchSubscriptions({onChange: second});
	try {
		await Promise.resolve();
		expect(watchEvents).toHaveBeenCalledTimes(1);
		expect(watchEvents).toHaveBeenCalledWith(expect.any(Object), ['subscriptions']);
		expect(second).toHaveBeenCalledWith(first.mock.lastCall![0]);
		closeFirst();
		expect(vi.mocked(watchEvents).mock.results[0].value).not.toHaveBeenCalled();
	} finally {
		closeSecond();
	}
	expect(vi.mocked(watchEvents).mock.results[0].value).toHaveBeenCalledOnce();
});

it('applies cascade removals and replaces state after reconnect', () => {
	const onChange = vi.fn();
	const close = watchSubscriptions({onChange});
	try {
		send(initial);
		send({type: 'remove', groups: [7]});
		expect(onChange.mock.lastCall![0]).toMatchObject({groups: [], items: []});
		vi.mocked(watchEvents).mock.lastCall![0].onOpen?.();
		const calls = onChange.mock.calls.length;
		send({type: 'patch', items: [{id: 42, imported_count: 99}]});
		expect(onChange).toHaveBeenCalledTimes(calls);
		send(initial);
		expect(onChange.mock.lastCall![0].items).toEqual(initial.items);
	} finally {
		close();
	}
});

it('accepts new entities and id-only deletion, ignoring malformed events', () => {
	const onChange = vi.fn();
	const close = watchSubscriptions({onChange});
	try {
		send(initial);
		send({type: 'patch', items: [{...initial.items[0], id: 43}]});
		send({type: 'remove', items: [42]});
		expect(onChange.mock.lastCall![0].items.map((row: { id: number }) => row.id)).toEqual([43]);
		const calls = onChange.mock.calls.length;
		for (const payload of [{type: 'patch', items: null}, {type: 'snapshot'},
			{type: 'patch', items: [{id: '43'}]}]) send(payload);
		expect(onChange).toHaveBeenCalledTimes(calls);
	} finally {
		close();
	}
});


it('assembles snapshot chunks once, then applies queued live updates', () => {
	const onChange = vi.fn();
	const close = watchSubscriptions({onChange});
	try {
		send({type: 'snapshot_start'});
		send({type: 'snapshot_chunk', groups: initial.groups, items: []});
		send({type: 'snapshot_chunk', groups: [], items: initial.items});
		expect(onChange).not.toHaveBeenCalled();
		send({type: 'snapshot_complete'});
		expect(onChange).toHaveBeenCalledExactlyOnceWith({groups: initial.groups, items: initial.items});
		send({type: 'patch', items: [{id: 42, imported_count: 99}]});
		expect(onChange.mock.lastCall![0].items[0].imported_count).toBe(99);
	} finally {
		close();
	}
});

it('discards partial snapshots on reconnect and accepts an empty baseline', () => {
	const onChange = vi.fn();
	const close = watchSubscriptions({onChange});
	try {
		send(initial);
		send({type: 'snapshot_start'});
		send({type: 'snapshot_chunk', groups: initial.groups, items: initial.items});
		vi.mocked(watchEvents).mock.lastCall![0].onOpen?.();
		onChange.mockClear();
		send({type: 'snapshot_complete'});
		send({type: 'snapshot_chunk', groups: initial.groups, items: initial.items});
		expect(onChange).not.toHaveBeenCalled();
		send({type: 'snapshot_start'});
		send({type: 'snapshot_chunk', groups: [], items: null});
		send({type: 'snapshot_complete'});
		expect(onChange).toHaveBeenCalledExactlyOnceWith({groups: [], items: []});
	} finally {
		close();
	}
});

it('holds late listeners until the chunked baseline is complete', async () => {
	const first = vi.fn();
	const second = vi.fn();
	const closeFirst = watchSubscriptions({onChange: first});
	let closeSecond: () => void = () => undefined;
	try {
		send({type: 'snapshot_start'});
		send({type: 'snapshot_chunk', groups: initial.groups, items: []});
		closeSecond = watchSubscriptions({onChange: second});
		await Promise.resolve();
		expect(second).not.toHaveBeenCalled();
		send({type: 'snapshot_chunk', groups: [], items: initial.items});
		send({type: 'snapshot_complete'});
		expect(first).toHaveBeenCalledExactlyOnceWith({groups: initial.groups, items: initial.items});
		expect(second).toHaveBeenCalledExactlyOnceWith({groups: initial.groups, items: initial.items});
	} finally {
		closeFirst();
		closeSecond();
	}
});
