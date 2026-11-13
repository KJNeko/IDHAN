import {watchEvents} from './client';
import type {Subscription, SubscriptionGroup, SubscriptionsEvent, SubscriptionsSnapshot} from './types';

type Listener = {
	onChange: (snapshot: SubscriptionsSnapshot) => void;
	onError?: (message: string) => void;
};

type Store = {
	state: SubscriptionsSnapshot;
	ready: boolean;
	listeners: Set<Listener>;
	close: () => void;
};

let shared: Store | null = null;

function mergeRows<T extends { id: number }>(rows: T[], patches: (Partial<T> & { id: number })[]): T[] {
	const next = new Map(rows.map(row => [row.id, row]));
	for (const patch of patches) next.set(patch.id, {...next.get(patch.id), ...patch} as T);
	return [...next.values()].sort((a, b) => a.id - b.id);
}

/** Missing fields are unchanged; explicit nulls clear values, including nested map replacement. */
export function applySubscriptionsEvent(state: SubscriptionsSnapshot, event: SubscriptionsEvent): SubscriptionsSnapshot {
	if (event.type === 'snapshot') return {groups: event.groups, items: event.items};
	if (event.type === 'remove') {
		const groups = new Set(event.groups);
		const items = new Set(event.items);
		return {
			...state, groups: state.groups.filter(group => !groups.has(group.id)),
			items: state.items.filter(item => !items.has(item.id) && !groups.has(item.group_id))
		};
	}
	return {
		groups: event.groups ? mergeRows<SubscriptionGroup>(state.groups, event.groups) : state.groups,
		items: event.items ? mergeRows<Subscription>(state.items, event.items) : state.items,
	};
}

function isSubscriptionsEvent(payload: Record<string, unknown>): payload is Record<string, unknown> & SubscriptionsEvent {
	if (!['snapshot', 'patch', 'remove'].includes(String(payload.type))) return false;
	const snapshot = payload.type === 'snapshot';
	for (const key of ['groups', 'items'] as const) {
		const rows = payload[key];
		if (rows === undefined && !snapshot) continue;
		if (!Array.isArray(rows)) return false;
		if (!rows.every(row => payload.type === 'remove'
			? Number.isSafeInteger(row) && row > 0
			: row !== null && typeof row === 'object' && Number.isSafeInteger(row.id) && row.id > 0)) return false;
	}
	return true;
}

/** One subscription store per tab, carried by the same connection as session and job events. */
export function watchSubscriptions(listener: Listener): () => void {
	if (!shared) {
		const store: Store = {
			state: {groups: [], items: []},
			ready: false, listeners: new Set(), close: () => undefined,
		};
		shared = store;
		store.close = watchEvents({
			onOpen: () => {
				store.ready = false;
			},
			onError: message => {
				for (const subscriber of store.listeners) subscriber.onError?.(message);
			},
			onEvent: ({payload}) => {
				if (!isSubscriptionsEvent(payload)) return;
				if (!store.ready && payload.type !== 'snapshot') return;
				store.state = applySubscriptionsEvent(store.state, payload);
				store.ready = true;
				for (const subscriber of store.listeners) subscriber.onChange(store.state);
			},
		}, ['subscriptions']);
	}
	const store = shared;
	store.listeners.add(listener);
	if (store.ready) {
		queueMicrotask(() => {
			if (store.listeners.has(listener)) listener.onChange(store.state);
		});
	}
	return () => {
		store.listeners.delete(listener);
		if (store.listeners.size > 0) return;
		store.close();
		if (shared === store) shared = null;
	};
}
