/**
 * The record the File Relationships panel is pinned to. It is deliberately not the app-wide
 * selection: ruling on duplicates walks its own path through the collection, and the grid and
 * viewer should stay where the user left them while it does. The last record sent is kept here so a
 * panel opened after the grid sent one still finds it.
 */

import type {BusApi, RecordId} from '../../host/types';

export const RELATIONSHIPS_FOCUS_TOPIC = 'relationships:focus';

let lastSent: RecordId | null = null;

export function sendToRelationships(bus: BusApi, id: RecordId): void {
    lastSent = id;
    bus.emit(RELATIONSHIPS_FOCUS_TOPIC, id);
}

export function lastRelationshipsFocus(): RecordId | null {
    return lastSent;
}
