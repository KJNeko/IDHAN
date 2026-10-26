/**
 * Virtualized thumbnail grid backed by host.results. Selection updates host.selection;
 * activation uses the bus. Prefetch visible rows before neighboring viewports.
 */

import {useEffect, useLayoutEffect, useRef, useState} from 'react';
import { useVirtualizer } from '@tanstack/react-virtual';
import type {PanelProps, SearchResultSet} from '../../host/types';
import {useGridSelection} from './useGridSelection';

// Convenience presets offered in the size dropdown; the server accepts any positive edge length.
const TILE_SIZES: readonly number[] = [128, 256, 512];
const GAP = 6;
const OVERSCAN_ROWS = 2;
// Bound thumbnail prefetch concurrency to reserve capacity for visible tiles.
const MAX_INFLIGHT_PREFETCH = 6;
const PREFETCH_MEMORY = 4096;

/** Replace pending prefetches on each submission; preserve requests already in progress. */
function createPrefetchQueue() {
  const requested = new Set<string>();
  let queue: string[] = [];
  let inflight = 0;

  const pump = () => {
    while (inflight < MAX_INFLIGHT_PREFETCH && queue.length > 0) {
      const url = queue.shift()!;
      if (requested.has(url)) continue;
      if (requested.size >= PREFETCH_MEMORY) requested.clear();
      requested.add(url);
      inflight += 1;
      const img = new Image();
      const finish = () => {
        inflight -= 1;
        pump();
      };
      img.onload = finish;
      img.onerror = finish;
      img.decoding = 'async';
      img.src = url;
    }
  };

  return (urls: readonly string[]) => {
    queue = urls.filter((url) => !requested.has(url));
    pump();
  };
}

type Config = { tileSize: number };
const DEFAULT_CONFIG: Config = { tileSize: 256 };

function readTileSize(raw: Partial<Config>): number {
  const size = raw.tileSize;
  return typeof size === 'number' && Number.isFinite(size) && size > 0 ? Math.round(size) : DEFAULT_CONFIG.tileSize;
}

/** Tracks the scroll container's content width, to pack a whole number of fixed-width tiles per row. */
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

function GridPanel({ host }: PanelProps) {
  const [tileSize, setTileSize] = useState<number>(() => readTileSize(host.settings.get() as Partial<Config>));
  const [results, setResults] = useState<SearchResultSet>(() => host.results.get());

  const scrollRef = useRef<HTMLDivElement>(null);
  const [prefetch] = useState(createPrefetchQueue);

  useEffect(() => host.results.subscribe(setResults), [host]);

  const ids = results.ids;
    const {selected, onTileClick, activate, onTileContextMenu, recordMenu} = useGridSelection(host, ids);
  const columns = useColumns(scrollRef, tileSize);
  const rowCount = Math.ceil(ids.length / columns);
  const rowHeight = tileSize + GAP;

  const virtualizer = useVirtualizer({
    count: rowCount,
    getScrollElement: () => scrollRef.current,
    estimateSize: () => rowHeight,
    overscan: OVERSCAN_ROWS,
  });

  function changeTileSize(size: number) {
    setTileSize(size);
    host.settings.set({ tileSize: size });
  }

  const virtualRows = virtualizer.getVirtualItems();
  const firstRow = virtualRows[0]?.index ?? -1;
  const lastRow = virtualRows[virtualRows.length - 1]?.index ?? -1;

  // Warm the rendered rows first, then walk outwards a viewport in each direction.
  useEffect(() => {
    if (firstRow < 0) return;
    const viewportRows = Math.max(1, Math.ceil((scrollRef.current?.clientHeight ?? 0) / rowHeight));
    const top = Math.max(0, firstRow - viewportRows);
    const bottom = Math.min(rowCount - 1, lastRow + viewportRows);

    const rows: number[] = [];
    for (let row = firstRow; row <= lastRow; row++) rows.push(row);
    for (let offset = 1; ; offset++) {
      const below = lastRow + offset;
      const above = firstRow - offset;
      if (below > bottom && above < top) break;
      if (below <= bottom) rows.push(below);
      if (above >= top) rows.push(above);
    }

    const urls: string[] = [];
    for (const row of rows) {
      const start = row * columns;
      const end = Math.min(start + columns, ids.length);
      for (let i = start; i < end; i++) urls.push(host.records.thumbnailUrl(ids[i]!, tileSize));
    }
    prefetch(urls);
  }, [host, prefetch, ids, tileSize, columns, rowCount, rowHeight, firstRow, lastRow]);

  return (
    <div className="grid-panel">
      <div className="grid-toolbar">
        <span className="muted">
          {ids.length > 0 ? `${ids.length.toLocaleString()} record${ids.length === 1 ? '' : 's'}` : 'No results'}
          {selected.size > 0 && ` · ${selected.size.toLocaleString()} selected`}
        </span>
        <label className="grid-size">
          Size
          <select value={tileSize} onChange={(e) => changeTileSize(Number(e.target.value))}>
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

        {recordMenu}
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
