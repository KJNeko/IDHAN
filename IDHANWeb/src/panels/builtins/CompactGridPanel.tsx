import {useEffect, useLayoutEffect, useMemo, useRef, useState} from 'react';
import type {PanelProps} from '../../host/types';
import {compactGridLayout, imageRatio} from './compactGridLayout';
import {useGridSelection} from './useGridSelection';

const SIZES = [64, 128, 192, 256, 512];
const DEFAULT_CONFIG = {maxWidth: 128};
const BATCH_SIZE = 200;

function CompactGridPanel({host}: PanelProps) {
    const [results, setResults] = useState(() => host.results.get());
    useEffect(() => host.results.subscribe(setResults), [host]);
    return <CompactResults host={host} ids={results.ids}/>;
}

function CompactResults({host, ids}: PanelProps & { ids: Int32Array }) {
    const [maxWidth, setMaxWidth] = useState(() => {
        const value = host.settings.get().maxWidth;
        return typeof value === 'number' && SIZES.includes(value) ? value : DEFAULT_CONFIG.maxWidth;
    });
    const [dimensions, setDimensions] = useState<{ ids: Int32Array; ratios: number[] }>({ids, ratios: []});
    const ratios = dimensions.ids === ids ? dimensions.ratios : [];
    const [error, setError] = useState(false);
    const [retry, setRetry] = useState(0);
    const scrollRef = useRef<HTMLDivElement>(null);
    const [viewport, setViewport] = useState({width: 1, height: 0, top: 0});
    const {selected, onTileClick, activate, onTileContextMenu, recordMenu} = useGridSelection(host, ids);
    const layout = useMemo(() => compactGridLayout(ratios, viewport.width, maxWidth), [ratios, viewport.width, maxWidth]);
    const needsMore = ratios.length < ids.length && layout.height < viewport.top + viewport.height * 3;

    useLayoutEffect(() => {
        const element = scrollRef.current;
        if (!element) return;
        const measure = () => setViewport({
            width: element.clientWidth,
            height: element.clientHeight,
            top: element.scrollTop
        });
        const observer = new ResizeObserver(measure);
        observer.observe(element);
        measure();
        return () => observer.disconnect();
    }, []);

    useEffect(() => {
        scrollRef.current?.scrollTo({top: 0});
        setViewport((current) => ({...current, top: 0}));
        setError(false);
    }, [ids]);

    useEffect(() => {
        if (!needsMore) return;
        let cancelled = false;
        const start = ratios.length;
        const batch = Array.from(ids.slice(start, start + BATCH_SIZE));
        void host.records.getMetadata(batch).then((response) => {
            if (cancelled) return;
            const byId = new Map(response.records.map((record) => [record.record_id, record]));
            const next = batch.map((id) => {
                const record = byId.get(id);
                return imageRatio(record?.width, record?.height);
            });
            setDimensions((current) => ({ids, ratios: [...(current.ids === ids ? current.ratios : []), ...next]}));
            setError(false);
        }).catch(() => {
            if (!cancelled) setError(true);
        });
        return () => {
            cancelled = true;
        };
    }, [host, ids, ratios.length, needsMore, retry]);

    const overscan = viewport.height;
    const visible = layout.tiles.filter((tile) => tile.y + tile.height >= viewport.top - overscan && tile.y <= viewport.top + viewport.height + overscan);

    return (
        <div className="grid-panel compact-grid-panel">
            <div className="grid-toolbar">
        <span className="muted">
          {ids.length.toLocaleString()} records
            {selected.size > 0 && ` · ${selected.size.toLocaleString()} selected`}
        </span>
                <label className="grid-size">
                    Max width
                    <select value={maxWidth} onChange={(event) => {
                        const size = Number(event.target.value);
                        setMaxWidth(size);
                        host.settings.set({maxWidth: size});
                    }}>
                        {SIZES.map((size) => <option key={size} value={size}>{size}px</option>)}
                    </select>
                </label>
            </div>
            <div className="compact-grid-scroll" ref={scrollRef} onScroll={(event) => {
                const top = event.currentTarget.scrollTop;
                setViewport((current) => ({...current, top}));
            }}>
                {ids.length === 0 && <p className="muted grid-empty">Run a search to populate the grid.</p>}
                <div style={{height: layout.height, position: 'relative'}}>
                    {visible.map((tile) => {
                        const id = ids[tile.index]!;
                        return (
                            <button
                                key={id}
                                type="button"
                                className={`grid-tile compact-grid-tile${selected.has(id) ? ' selected' : ''}`}
                                style={{left: tile.x, top: tile.y, width: tile.width, height: tile.height}}
                                aria-pressed={selected.has(id)}
                                title={`#${id}`}
                                onClick={(event) => onTileClick(event, tile.index)}
                                onDoubleClick={() => activate(tile.index)}
                                onContextMenu={(event) => onTileContextMenu(event, tile.index)}
                            >
                                <img src={host.records.thumbnailUrl(id, maxWidth)} alt={`#${id}`} loading="lazy"
                                     decoding="async" draggable={false}/>
                            </button>
                        );
                    })}
                </div>
                {error ?
                    <p className="error grid-empty">Could not load image dimensions. <button className="toolbar-button"
                                                                                             onClick={() => setRetry((value) => value + 1)}>Retry</button>
                    </p>
                    : ratios.length < ids.length && <p className="muted grid-empty"
                                                       role="status">{needsMore ? 'Loading dimensions…' : `${ratios.length.toLocaleString()} loaded · scroll for more`}</p>}
            </div>
            {recordMenu}
        </div>
    );
}

export const compactGridPanel = {
    type: 'compact-grid',
    title: 'Compact Grid',
    description: 'Equal-width thumbnails in masonry columns with 5px gaps.',
    component: CompactGridPanel,
    defaultConfig: DEFAULT_CONFIG,
    configVersion: 1,
} as const;
