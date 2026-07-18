/**
 * Thumbnail Grid — the workhorse view. It is backed directly by the shared result set's Int32Array
 * (host.results), so a 100k set costs ~400 KB and index math is pure arithmetic. Rows are virtualized
 * with @tanstack/react-virtual, keeping the DOM to the visible window plus a little overscan regardless
 * of set size; discrete tile sizes {128,256,512} give fixed rows so there is no measurement pass.
 *
 * Thumbnails are plain <img> with lazy/async decode — windowing already bounds how many load at once.
 * The velocity gate and concurrency cap the plan describes are M7 perf hardening; measure first.
 *
 * Selection is driven here (click / ctrl-toggle / shift-range) and pushed to host.selection so the
 * viewer, record info, and tag editor follow along. Double-click activates a record on the bus.
 */

import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { useVirtualizer } from '@tanstack/react-virtual';
import type { PanelProps, RecordId, SearchResultSet } from '../../host/types';

type TileSize = 128 | 256 | 512;
const TILE_SIZES: readonly TileSize[] = [128, 256, 512];
const GAP = 6;
const OVERSCAN_ROWS = 2;

type Config = { tileSize: TileSize };
const DEFAULT_CONFIG: Config = { tileSize: 256 };

/** Bus topic the grid emits on double-click / Enter; the viewer focuses the activated record. */
export const RECORD_ACTIVATE_TOPIC = 'record:activate';

function readTileSize(raw: Partial<Config>): TileSize {
  return TILE_SIZES.includes(raw.tileSize as TileSize) ? (raw.tileSize as TileSize) : DEFAULT_CONFIG.tileSize;
}

/** Track the scroll container's content width so we can pack a whole number of fixed-width tiles per row. */
function useColumns(ref: React.RefObject<HTMLDivElement | null>, tile: number): number {
  const [width, setWidth] = useState(0);
  useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;
    const observer = new ResizeObserver((entries) => {
      const entry = entries[0];
      if (entry) setWidth(entry.contentRect.width);
    });
    observer.observe(el);
    setWidth(el.clientWidth);
    return () => observer.disconnect();
  }, [ref]);
  return Math.max(1, Math.floor((width + GAP) / (tile + GAP)));
}

interface ContextMenuState {
  x: number;
  y: number;
  ids: RecordId[];
}

function GridPanel({ host }: PanelProps) {
  const [tileSize, setTileSize] = useState<TileSize>(() => readTileSize(host.settings.get() as Partial<Config>));
  const [results, setResults] = useState<SearchResultSet>(() => host.results.get());
  const [selected, setSelected] = useState<ReadonlySet<RecordId>>(() => new Set(host.selection.get()));
  const [menu, setMenu] = useState<ContextMenuState | null>(null);

  const scrollRef = useRef<HTMLDivElement>(null);
  const anchorIndex = useRef<number | null>(null);

  useEffect(() => host.results.subscribe(setResults), [host]);
  useEffect(() => host.selection.subscribe((ids) => setSelected(new Set(ids))), [host]);

  const ids = results.ids;
  const columns = useColumns(scrollRef, tileSize);
  const rowCount = Math.ceil(ids.length / columns);
  const rowHeight = tileSize + GAP;

  const virtualizer = useVirtualizer({
    count: rowCount,
    getScrollElement: () => scrollRef.current,
    estimateSize: () => rowHeight,
    overscan: OVERSCAN_ROWS,
  });

  const applySelection = useCallback(
    (next: Set<RecordId>) => {
      // Push to the host; the subscription above mirrors it back into local state.
      host.selection.set([...next]);
    },
    [host],
  );

  function changeTileSize(size: TileSize) {
    setTileSize(size);
    host.settings.set({ tileSize: size });
  }

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
    event.preventDefault();
    const id = ids[index]!;
    // Right-clicking outside the current selection targets just that record.
    const targetIds = selected.has(id) ? [...selected] : [id];
    if (!selected.has(id)) {
      anchorIndex.current = index;
      applySelection(new Set([id]));
    }
    setMenu({ x: event.clientX, y: event.clientY, ids: targetIds });
  }

  // Close the context menu on any outside interaction.
  useEffect(() => {
    if (!menu) return;
    const close = () => setMenu(null);
    window.addEventListener('click', close);
    window.addEventListener('scroll', close, true);
    return () => {
      window.removeEventListener('click', close);
      window.removeEventListener('scroll', close, true);
    };
  }, [menu]);

  const virtualRows = virtualizer.getVirtualItems();

  return (
    <div className="grid-panel">
      <div className="grid-toolbar">
        <span className="muted">
          {ids.length > 0 ? `${ids.length.toLocaleString()} record${ids.length === 1 ? '' : 's'}` : 'No results'}
          {selected.size > 0 && ` · ${selected.size.toLocaleString()} selected`}
        </span>
        <label className="grid-size">
          Size
          <select value={tileSize} onChange={(e) => changeTileSize(Number(e.target.value) as TileSize)}>
            {TILE_SIZES.map((s) => (
              <option key={s} value={s}>
                {s}px
              </option>
            ))}
          </select>
        </label>
      </div>

      <div ref={scrollRef} className="grid-scroll">
        {ids.length === 0 ? (
          <p className="muted grid-empty">Run a search to populate the grid.</p>
        ) : (
          <div style={{ height: virtualizer.getTotalSize(), position: 'relative' }}>
            {virtualRows.map((row) => {
              const start = row.index * columns;
              const end = Math.min(start + columns, ids.length);
              const cells = [];
              for (let i = start; i < end; i++) {
                const id = ids[i]!;
                cells.push(
                  <button
                    key={id}
                    type="button"
                    className={`grid-tile${selected.has(id) ? ' selected' : ''}`}
                    style={{ width: tileSize, height: tileSize }}
                    onClick={(e) => onTileClick(e, i)}
                    onDoubleClick={() => activate(i)}
                    onContextMenu={(e) => onTileContextMenu(e, i)}
                    title={`#${id}`}
                  >
                    <img
                      src={host.records.thumbnailUrl(id, tileSize)}
                      alt={`#${id}`}
                      loading="lazy"
                      decoding="async"
                      draggable={false}
                    />
                  </button>,
                );
              }
              return (
                <div
                  key={row.key}
                  className="grid-row"
                  style={{
                    position: 'absolute',
                    top: row.start,
                    left: 0,
                    height: rowHeight,
                    gap: GAP,
                  }}
                >
                  {cells}
                </div>
              );
            })}
          </div>
        )}
      </div>

      {menu && (
        <ul className="context-menu" style={{ top: menu.y, left: menu.x }} onClick={(e) => e.stopPropagation()}>
          <li>
            <button type="button" onClick={() => { host.bus.emit(RECORD_ACTIVATE_TOPIC, menu.ids[0]); setMenu(null); }}>
              Open
            </button>
          </li>
          <li>
            <button
              type="button"
              onClick={() => { void navigator.clipboard?.writeText(menu.ids.join(', ')); setMenu(null); }}
            >
              Copy id{menu.ids.length === 1 ? '' : 's'}
            </button>
          </li>
          <li>
            <button type="button" onClick={() => { applySelection(new Set(ids)); setMenu(null); }}>
              Select all
            </button>
          </li>
          <li>
            <button type="button" onClick={() => { applySelection(new Set()); setMenu(null); }}>
              Clear selection
            </button>
          </li>
        </ul>
      )}
    </div>
  );
}

export const gridPanel = {
  type: 'grid',
  title: 'Thumbnail Grid',
  description: 'Virtualized grid of the current search results; drives the selection.',
  component: GridPanel,
  defaultConfig: DEFAULT_CONFIG,
  configVersion: 1,
} as const;
