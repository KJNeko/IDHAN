/** Shared selection and activation behavior for both thumbnail layouts. */
import {useCallback, useEffect, useRef, useState} from 'react';
import type {PanelProps, RecordId} from '../../host/types';
import {RECORD_ACTIVATE_TOPIC, useRecordMenu, type RecordAction} from './recordActions';

export function useGridSelection(host: PanelProps['host'], ids: Int32Array) {
    const [selected, setSelected] = useState<ReadonlySet<RecordId>>(() => new Set(host.selection.get()));
    const anchorIndex = useRef<number | null>(null);
    useEffect(() => host.selection.subscribe((value) => setSelected(new Set(value))), [host]);
    useEffect(() => {
        anchorIndex.current = null;
    }, [ids]);

    const applySelection = useCallback(
        (next: Set<RecordId>) => {
            // Push to the host; the subscription above mirrors it back into local state.
            host.selection.set([...next]);
        },
        [host],
    );

    /** Entries that only mean anything in a grid; everything record-scoped lives in RECORD_ACTIONS. */
    const gridActions: RecordAction[] = [
        {
            key: 'select-all',
            group: 'grid',
            label: () => 'Select all',
            available: () => ids.length > 0,
            run: () => applySelection(new Set(ids)),
        },
        {
            key: 'clear-selection',
            group: 'grid',
            label: () => 'Clear selection',
            available: () => selected.size > 0,
            run: () => applySelection(new Set()),
        },
    ];

    const {openRecordMenu, recordMenu} = useRecordMenu(host, gridActions);

    function onTileClick(event: React.MouseEvent, index: number) {
        const id = ids[index]!;
        if (event.shiftKey && anchorIndex.current !== null) {
            const lo = Math.min(anchorIndex.current, index);
            const hi = Math.max(anchorIndex.current, index);
            const range = new Set<RecordId>(event.ctrlKey || event.metaKey ? selected : []);
            for (let i = lo; i <= hi; i++) range.add(ids[i]!);
            applySelection(range);
        } else if (event.ctrlKey || event.metaKey) {
            const next = new Set(selected);
            if (next.has(id)) next.delete(id);
            else next.add(id);
            anchorIndex.current = index;
            applySelection(next);
        } else {
            anchorIndex.current = index;
            applySelection(new Set([id]));
        }
    }

    function activate(index: number) {
        const id = ids[index]!;
        anchorIndex.current = index;
        applySelection(new Set([id]));
        host.bus.emit(RECORD_ACTIVATE_TOPIC, id);
    }

    function onTileContextMenu(event: React.MouseEvent, index: number) {
        const id = ids[index]!;
        // Right-clicking outside the current selection targets just that record.
        const targetIds = selected.has(id) ? [...selected] : [id];
        if (!selected.has(id)) {
            anchorIndex.current = index;
            applySelection(new Set([id]));
        }
        openRecordMenu(event, targetIds, id);
    }

    return {selected, onTileClick, activate, onTileContextMenu, recordMenu};
}
