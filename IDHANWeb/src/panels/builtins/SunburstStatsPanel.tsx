/**
 * Sunburst Stats — PostgreSQL storage broken down by table and index (GET /db/stats/sunburst).
 * Retires the old static sunburst.html, which pulled d3 + sunburst-chart off a CDN; the SPA's CSP
 * blocks external scripts, so this draws the rings itself with plain SVG — no dependencies.
 *
 * Inner ring: one arc per table, sized by its total footprint (heap + its indexes). Outer ring: within
 * each table's wedge, the heap itself followed by each index. Hover any arc for its name and size.
 */

import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { PanelProps } from '../../host/types';
import { formatBytes } from './RecordInfoView';

interface RawNode {
  name: string;
  value: number;
  children?: RawNode[];
}

interface Arc {
  path: string;
  color: string;
  name: string;
  value: number;
  ring: 'table' | 'part';
}

const SIZE = 320;
const CENTER = SIZE / 2;
const R_INNER = 60;
const R_MID = 110;
const R_OUTER = 155;
const TAU = Math.PI * 2;

function polar(r: number, angle: number): [number, number] {
  return [CENTER + r * Math.sin(angle), CENTER - r * Math.cos(angle)];
}

function donutArc(rInner: number, rOuter: number, a0: number, a1: number): string {
  // A full-circle segment has a coincident start/end point that collapses the arc; clamp just under.
  const end = a1 - a0 >= TAU ? a0 + TAU - 0.0001 : a1;
  const largeArc = end - a0 > Math.PI ? 1 : 0;
  const [x0o, y0o] = polar(rOuter, a0);
  const [x1o, y1o] = polar(rOuter, end);
  const [x1i, y1i] = polar(rInner, end);
  const [x0i, y0i] = polar(rInner, a0);
  return `M ${x0o} ${y0o} A ${rOuter} ${rOuter} 0 ${largeArc} 1 ${x1o} ${y1o} L ${x1i} ${y1i} A ${rInner} ${rInner} 0 ${largeArc} 0 ${x0i} ${y0i} Z`;
}

interface TableFootprint {
  name: string;
  heap: number;
  indexes: RawNode[];
  footprint: number;
}

function SunburstStatsPanel({ host }: PanelProps) {
  const [root, setRoot] = useState<RawNode | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [hover, setHover] = useState<{ name: string; value: number } | null>(null);
  const mounted = useRef(true);

  useEffect(() => {
    mounted.current = true;
    return () => {
      mounted.current = false;
    };
  }, []);

  const refresh = useCallback(async () => {
    try {
      const res = await host.http.fetch('/db/stats/sunburst');
      if (!res.ok) throw new Error(`/db/stats/sunburst → ${res.status}`);
      if (mounted.current) {
        setRoot((await res.json()) as RawNode);
        setError(null);
      }
    } catch (err) {
      if (mounted.current) setError(err instanceof Error ? err.message : String(err));
    }
  }, [host]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const { arcs, total, tables } = useMemo(() => {
    const empty = { arcs: [] as Arc[], total: 0, tables: [] as TableFootprint[] };
    if (!root?.children) return empty;

    const tableList: TableFootprint[] = root.children
      .map((t) => {
        const indexes = t.children ?? [];
        const heap = t.value ?? 0;
        const footprint = heap + indexes.reduce((sum, i) => sum + (i.value ?? 0), 0);
        return { name: t.name, heap, indexes, footprint };
      })
      .filter((t) => t.footprint > 0)
      .sort((a, b) => b.footprint - a.footprint);

    const grand = tableList.reduce((sum, t) => sum + t.footprint, 0);
    if (grand === 0) return empty;

    const out: Arc[] = [];
    let angle = 0;
    tableList.forEach((t, i) => {
      const hue = Math.round((i * 360) / tableList.length);
      const span = (t.footprint / grand) * TAU;
      const a0 = angle;
      const a1 = angle + span;
      angle = a1;

      out.push({
        path: donutArc(R_INNER, R_MID, a0, a1),
        color: `hsl(${hue} 55% 52%)`,
        name: t.name,
        value: t.footprint,
        ring: 'table',
      });

      // Outer ring: heap first, then each index, filling the same wedge proportionally.
      const parts: RawNode[] = [{ name: `${t.name} (heap)`, value: t.heap }, ...t.indexes];
      let inner = a0;
      parts.forEach((p, j) => {
        const pv = p.value ?? 0;
        if (pv <= 0) return;
        const pSpan = (pv / t.footprint) * span;
        out.push({
          path: donutArc(R_MID, R_OUTER, inner, inner + pSpan),
          color: `hsl(${hue} ${j === 0 ? 40 : 32}% ${58 + ((j % 3) * 8)}%)`,
          name: p.name,
          value: pv,
          ring: 'part',
        });
        inner += pSpan;
      });
    });

    return { arcs: out, total: grand, tables: tableList };
  }, [root]);

  return (
    <div className="panel-body sunburst-stats">
      <div className="sunburst-toolbar">
        <button type="button" className="toolbar-button" onClick={() => void refresh()}>
          Refresh
        </button>
        <span className="muted grow">{total > 0 ? `${formatBytes(total)} across ${tables.length} tables` : ''}</span>
      </div>

      {error && <p className="error">{error}</p>}
      {!error && root === null && <p className="muted">Loading…</p>}
      {!error && root !== null && total === 0 && <p className="muted">No table storage reported.</p>}

      {total > 0 && (
        <div className="sunburst-wrap">
          <svg viewBox={`0 0 ${SIZE} ${SIZE}`} className="sunburst-svg" onMouseLeave={() => setHover(null)}>
            {arcs.map((a, i) => (
              <path
                key={i}
                d={a.path}
                fill={a.color}
                className={`sunburst-arc${hover?.name === a.name ? ' hot' : ''}`}
                onMouseEnter={() => setHover({ name: a.name, value: a.value })}
              />
            ))}
            <text x={CENTER} y={CENTER - 6} textAnchor="middle" className="sunburst-center-name">
              {hover ? hover.name : 'Total'}
            </text>
            <text x={CENTER} y={CENTER + 14} textAnchor="middle" className="sunburst-center-value">
              {formatBytes(hover ? hover.value : total)}
              {hover ? ` · ${((hover.value / total) * 100).toFixed(1)}%` : ''}
            </text>
          </svg>

          <ul className="sunburst-legend">
            {tables.map((t, i) => (
              <li key={t.name} className="sunburst-legend-row" onMouseEnter={() => setHover({ name: t.name, value: t.footprint })}>
                <span className="sunburst-swatch" style={{ background: `hsl(${Math.round((i * 360) / tables.length)} 55% 52%)` }} />
                <span className="grow" title={t.name}>
                  {t.name}
                </span>
                <span className="muted">{formatBytes(t.footprint)}</span>
              </li>
            ))}
          </ul>
        </div>
      )}
    </div>
  );
}

export const sunburstStatsPanel = {
  type: 'sunburst-stats',
  title: 'Sunburst Stats',
  description: 'PostgreSQL storage by table and index, as a sunburst.',
  component: SunburstStatsPanel,
  configVersion: 1,
  singleton: true,
} as const;
