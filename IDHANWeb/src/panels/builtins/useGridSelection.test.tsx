import {act, type MouseEvent} from 'react';
import {createRoot} from 'react-dom/client';
import {afterEach, expect, it, vi} from 'vitest';
import type {PanelProps} from '../../host/types';
import {useGridSelection} from './useGridSelection';

const openRecordMenu = vi.fn();
vi.mock('./recordActions', () => ({
    RECORD_ACTIVATE_TOPIC: 'record:activate',
    useRecordMenu: () => ({openRecordMenu, recordMenu: null}),
}));

Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});
afterEach(() => vi.clearAllMocks());

it('shares range, toggle, context targeting, and activation across grid layouts', async () => {
    let selected: number[] = [];
    let listener: (ids: number[]) => void = () => {
    };
    const emit = vi.fn();
    const host = {
        selection: {
            get: () => selected,
            set: (ids: number[]) => {
                selected = ids;
                listener(ids);
            },
            subscribe: (next: typeof listener) => {
                listener = next;
                return () => {
                };
            },
        },
        bus: {emit},
    } as unknown as PanelProps['host'];
    let grid!: ReturnType<typeof useGridSelection>;

    function Harness({ids}: { ids: Int32Array }) {
        grid = useGridSelection(host, ids);
        return null;
    }

    const root = createRoot(document.createElement('div'));
    const event = (keys = {}) => ({shiftKey: false, ctrlKey: false, metaKey: false, ...keys}) as MouseEvent;
    try {
        await act(() => root.render(<Harness ids={new Int32Array([10, 20, 30, 40])}/>));
        await act(() => grid.onTileClick(event(), 1));
        await act(() => grid.onTileClick(event({shiftKey: true}), 3));
        expect(selected).toEqual([20, 30, 40]);
        await act(() => grid.onTileClick(event({ctrlKey: true}), 2));
        expect(selected).toEqual([20, 40]);
        await act(() => grid.onTileContextMenu(event(), 1));
        expect(openRecordMenu.mock.lastCall?.slice(1)).toEqual([[20, 40], 20]);
        await act(() => grid.onTileContextMenu(event(), 0));
        expect(selected).toEqual([10]);
        await act(() => grid.activate(2));
        expect(selected).toEqual([30]);
        expect(emit).toHaveBeenCalledWith('record:activate', 30);
        await act(() => root.render(<Harness ids={new Int32Array([50])}/>));
        await act(() => grid.onTileClick(event({shiftKey: true}), 0));
        expect(selected).toEqual([50]);
    } finally {
        await act(() => root.unmount());
    }
});
