/**
 * The right-click menu every panel that shows a record uses.
 *
 * A panel calls useRecordMenu(host, extras) and gets back a context-menu handler plus the menu node
 * to render, so an operation added to RECORD_ACTIONS below appears wherever a record is shown. Only
 * entries that are meaningless outside one panel (the grid's select-all, say) are passed as extras.
 *
 * Actions are async and report their own outcome by toast; the menu closes the moment one is picked.
 */

import {useCallback, useEffect, useState} from 'react';
import type {MouseEvent as ReactMouseEvent, ReactNode} from 'react';
import type {RecordMetadata} from '../../api/types';
import type {HostApi, RecordId} from '../../host/types';
import {formatBytes, formatDuration} from './RecordInfoView';
import {sendToRelationships} from './relationshipsFocus';

/** Bus topic the grid emits on double-click or Enter; the viewer focuses the activated record. */
export const RECORD_ACTIVATE_TOPIC = 'record:activate';

export interface RecordActionContext {
    host: HostApi;
    /** Records the menu was opened on, never empty. */
    ids: readonly RecordId[];
    /** The one that was right-clicked, for actions that act on a record rather than a set. */
    target: RecordId;
}

export interface RecordAction {
    key: string;
    /** A separator is drawn wherever this differs between neighbouring entries. */
    group: string;

    label(ctx: RecordActionContext): string;

    /** Left out of the menu entirely when this returns false. Shown always when absent. */
    available?(ctx: RecordActionContext): boolean;

    run(ctx: RecordActionContext): void | Promise<void>;
}

const plural = (ids: RecordActionContext['ids']): string => (ids.length === 1 ? '' : 's');

async function copyText(host: HostApi, text: string, what: string): Promise<void> {
    if (!navigator.clipboard) {
        host.ui.toast('The browser only grants the clipboard to a secure page', {kind: 'error'});
        return;
    }
    await navigator.clipboard.writeText(text);
    host.ui.toast(`Copied ${what}`, {kind: 'success'});
}

/** The record's own address, without the api key fileUrl carries for <img> elements. */
function shareableFileUrl(host: HostApi, id: RecordId): string {
    const url = new URL(host.records.fileUrl(id), window.location.origin);
    url.searchParams.delete('idhan_key');
    return url.toString();
}

/** Metadata for the menu's records, ordered as they were given and skipping any the server lost. */
async function metadataFor(ctx: RecordActionContext): Promise<RecordMetadata[]> {
    const {records} = await ctx.host.records.getMetadata(ctx.ids);
    const byId = new Map(records.map((record) => [record.record_id, record]));
    return ctx.ids.map((id) => byId.get(id)).filter((record): record is RecordMetadata => record !== undefined);
}

/** The fields worth pasting into a bug report or a message, one labelled line each. */
export function describeRecord(record: RecordMetadata): string {
    const lines = [`record: #${record.record_id}`, `sha256: ${record.hashes.sha256}`];
    if (record.mime) lines.push(`mime: ${record.mime}`);
    if (record.size !== undefined) lines.push(`size: ${formatBytes(record.size)} (${record.size} bytes)`);
    if (record.width && record.height) lines.push(`dimensions: ${record.width}x${record.height}`);
    if (record.duration !== undefined) lines.push(`duration: ${formatDuration(record.duration)}`);
    if (record.phash) lines.push(`phash: ${record.phash}`);
    return lines.join('\n');
}

/**
 * Every operation that applies to a record wherever it is shown. Panels do not filter this list, so
 * an entry that only makes sense for one record guards itself through `available`.
 */
export const RECORD_ACTIONS: RecordAction[] = [
    {
        key: 'open',
        group: 'view',
        label: () => 'Open',
        available: ({ids}) => ids.length === 1,
        run: ({host, target}) => host.bus.emit(RECORD_ACTIVATE_TOPIC, target),
    },
    {
        key: 'send-to-relationships',
        group: 'view',
        label: () => 'Send to Relationships View',
        available: ({ids}) => ids.length === 1,
        run: ({host, target}) => sendToRelationships(host.bus, target),
    },
    {
        key: 'regenerate-metadata',
        group: 'maintenance',
        label: ({target}) => `Regenerate metadata for #${target}`,
        run: async ({host, target}) => {
            host.ui.toast(`Regenerating metadata for #${target}…`, {kind: 'info'});
            await host.records.regenerateMetadata([target]);
            host.ui.toast(`Regenerated metadata for #${target}`, {kind: 'success'});
        },
    },
    {
        key: 'copy-ids',
        group: 'copy',
        label: ({ids}) => `Copy id${plural(ids)}`,
        run: ({host, ids}) => copyText(host, ids.join(', '), `id${plural(ids)}`),
    },
    {
        key: 'copy-hashes',
        group: 'copy',
        label: ({ids}) => `Copy SHA-256${plural(ids)}`,
        run: async (ctx) => {
            const records = await metadataFor(ctx);
            if (records.length === 0) {
                ctx.host.ui.toast('No metadata to copy', {kind: 'error'});
                return;
            }
            await copyText(ctx.host, records.map((record) => record.hashes.sha256).join('\n'), 'SHA-256');
        },
    },
    {
        key: 'copy-link',
        group: 'copy',
        label: ({ids}) => `Copy file link${plural(ids)}`,
        run: ({host, ids}) =>
            copyText(host, ids.map((id) => shareableFileUrl(host, id)).join('\n'), `file link${plural(ids)}`),
    },
    {
        key: 'copy-details',
        group: 'copy',
        label: () => 'Copy details',
        run: async (ctx) => {
            const records = await metadataFor(ctx);
            if (records.length === 0) {
                ctx.host.ui.toast('No metadata to copy', {kind: 'error'});
                return;
            }
            await copyText(ctx.host, records.map(describeRecord).join('\n\n'), 'details');
        },
    },
];

interface MenuState {
    x: number;
    y: number;
    ids: RecordId[];
    target: RecordId;
}

export interface RecordMenu {
    /** Render somewhere inside the panel; null while the menu is closed. */
    recordMenu: ReactNode;

    /**
     * Bind to a record's onContextMenu; the event's default menu is suppressed here. `target` is the
     * record that was clicked, and defaults to the first id: pass it when `ids` is a whole selection.
     */
    openRecordMenu(event: ReactMouseEvent, ids: readonly RecordId[], target?: RecordId): void;

    closeRecordMenu(): void;
}

/** `extras` are appended after the shared actions, for entries that only mean anything in one panel. */
export function useRecordMenu(host: HostApi, extras: readonly RecordAction[] = []): RecordMenu {
    const [menu, setMenu] = useState<MenuState | null>(null);

    const openRecordMenu = useCallback((event: ReactMouseEvent, ids: readonly RecordId[], target?: RecordId) => {
        event.preventDefault();
        const clicked = target ?? ids[0];
        if (clicked === undefined) return;
        setMenu({x: event.clientX, y: event.clientY, ids: [...ids], target: clicked});
    }, []);

    const closeRecordMenu = useCallback(() => setMenu(null), []);

    // Close on any outside interaction, the same way a native menu does.
    useEffect(() => {
        if (!menu) return;
        const close = () => setMenu(null);
        const onKey = (event: KeyboardEvent) => {
            if (event.key === 'Escape') setMenu(null);
        };
        window.addEventListener('click', close);
        window.addEventListener('scroll', close, true);
        window.addEventListener('keydown', onKey);
        return () => {
            window.removeEventListener('click', close);
            window.removeEventListener('scroll', close, true);
            window.removeEventListener('keydown', onKey);
        };
    }, [menu]);

    if (!menu) return {openRecordMenu, closeRecordMenu, recordMenu: null};

    const ctx: RecordActionContext = {host, ids: menu.ids, target: menu.target};
    const entries = [...RECORD_ACTIONS, ...extras].filter((action) => action.available?.(ctx) ?? true);

    const recordMenu = (
        <ul className="context-menu" style={{top: menu.y, left: menu.x}} onClick={(event) => event.stopPropagation()}>
            {entries.map((action, index) => (
                <li
                    key={action.key}
                    className={index > 0 && entries[index - 1]!.group !== action.group ? 'context-menu-break' : undefined}
                >
                    <button
                        type="button"
                        onClick={() => {
                            setMenu(null);
                            // A rejected action still has to say so: nothing else is watching this promise.
                            void Promise.resolve(action.run(ctx)).catch((error: unknown) => {
                                host.ui.toast(error instanceof Error ? error.message : String(error), {kind: 'error'});
                            });
                        }}
                    >
                        {action.label(ctx)}
                    </button>
                </li>
            ))}
        </ul>
    );

    return {openRecordMenu, closeRecordMenu, recordMenu};
}
