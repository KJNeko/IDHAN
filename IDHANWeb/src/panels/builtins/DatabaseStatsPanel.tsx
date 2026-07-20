/**
 * Database Stats — an at-a-glance view of the IDHAN database: aggregate counts, PostgreSQL storage
 * per table, on-disk clusters, and record counts by mime type.
 *
 * Replaces the old two-ring Sunburst chart, whose outer ring exploded every table into one sliver
 * per index — lots of noise for little signal. Storage and content-by-type are now capped donut
 * charts (≤5 segments + a folded "Other") with a legend/table twin; storage adds a second, inner
 * ring for whichever table is currently selected, showing only that table's heap/index split —
 * never every table's indexes at once, which is what made the old sunburst noisy.
 *
 * The panel `type` is still the legacy `sunburst-stats` slug: it is an internal id embedded in saved
 * layout documents, so keeping it means every existing layout keeps rendering this panel instead of
 * tombstoning. Only the user-facing title/description changed.
 */

import { useCallback, useEffect, useState, type KeyboardEvent as ReactKeyboardEvent, type ReactNode } from 'react';
import type { PanelProps } from '../../host/types';
import type { DatabaseStats, MimeCount, StorageNode, VersionInfo } from '../../api/types';
import { formatBytes } from './RecordInfoView';

/** One on-disk cluster, matching the GET /clusters/list shape (see ClusterManagerPanel). */
interface Cluster {
  cluster_id: number;
  name: string;
  path: string;
  readonly: boolean;
  file_count: number;
  size: { used: number; limit: number; available: number };
  /** Mime breakdown of the files stored in this specific cluster. */
  by_mime: MimeCount[];
}

/** A table's storage footprint: its heap plus every index on it. */
export interface TableFootprint {
  name: string;
  heap: number;
  indexes: StorageNode[];
  footprint: number;
}

export interface StorageBreakdown {
  /** Tables large enough to show on their own, largest first. */
  major: TableFootprint[];
  /** Tables below the threshold, folded into a single "Other" row. */
  minor: TableFootprint[];
  /** Sum of every table's footprint. */
  grand: number;
}

/**
 * Turn the raw storage tree into a sorted breakdown, folding tables below `minFraction` of the grand
 * total into `minor`. Pure and exported so the folding is unit-testable.
 */
export function buildStorageBreakdown(root: StorageNode | null, minFraction = 0.01): StorageBreakdown {
  const empty: StorageBreakdown = { major: [], minor: [], grand: 0 };
  if (!root?.children) return empty;

  const tables: TableFootprint[] = root.children
    .map((t) => {
      const indexes = (t.children ?? []).filter((i) => (i.value ?? 0) > 0).sort((a, b) => (b.value ?? 0) - (a.value ?? 0));
      const heap = t.value ?? 0;
      const footprint = heap + indexes.reduce((sum, i) => sum + (i.value ?? 0), 0);
      return { name: t.name, heap, indexes, footprint };
    })
    .filter((t) => t.footprint > 0)
    .sort((a, b) => b.footprint - a.footprint);

  const grand = tables.reduce((sum, t) => sum + t.footprint, 0);
  if (grand === 0) return empty;

  const major: TableFootprint[] = [];
  const minor: TableFootprint[] = [];
  for (const t of tables) (t.footprint / grand >= minFraction ? major : minor).push(t);
  return { major, minor, grand };
}

function formatCount(n: number): string {
  return n.toLocaleString();
}

function pct(value: number, total: number): number {
  return total > 0 ? (value / total) * 100 : 0;
}

// Validated dark-mode categorical order (see the `dataviz` skill) — checked against IDHAN's actual
// panel surface (#16181d) for CVD separation and contrast. Assigned to segments in this fixed order
// by rank; never cycled or re-assigned on refresh.
const DONUT_PALETTE = ['#3987e5', '#008300', '#d55181', '#c98500', '#199e70', '#d95926'] as const;
// The "Other" fold bucket is not a categorical slot — it reads as neutral/de-emphasized, not identity.
const OTHER_COLOR = '#9aa0aa';

export interface DonutSegment {
  label: string;
  value: number;
  color: string;
  /** Optional secondary line shown under the label in DonutLegend (e.g. capacity, a status flag). */
  sub?: string;
}

export interface DonutRing {
  segments: DonutSegment[];
  otherValue: number;
  otherCount: number;
  /** The individual items folded into "Other", sorted descending — what the legend expands to show. */
  otherItems: DonutSegment[];
  total: number;
}

/**
 * Rank the given items by value, keep the top `maxSegments` with a fixed-order palette color each,
 * and fold everything past that into a single "Other" bucket — regardless of how large an individual
 * folded item is. This bounds segment count (unlike a percentage threshold), which is what keeps a
 * donut chart readable. The folded items themselves are kept (as `otherItems`, all sharing the neutral
 * "Other" color) so a legend can offer them up on request rather than losing them entirely. Pure and
 * exported so the capping/folding is unit-testable.
 */
export function buildDonutRing(items: { name: string; value: number; sub?: string }[], maxSegments = 5): DonutRing {
  const sorted = items.filter((i) => i.value > 0).sort((a, b) => b.value - a.value);
  const top = sorted.slice(0, maxSegments);
  const rest = sorted.slice(maxSegments);
  const segments = top.map((t, i) => ({ label: t.name, value: t.value, color: DONUT_PALETTE[i] ?? OTHER_COLOR, sub: t.sub }));
  const otherItems = rest.map((t) => ({ label: t.name, value: t.value, color: OTHER_COLOR, sub: t.sub }));
  const otherValue = rest.reduce((sum, t) => sum + t.value, 0);
  const total = segments.reduce((sum, s) => sum + s.value, 0) + otherValue;
  return { segments, otherValue, otherCount: rest.length, otherItems, total };
}

/**
 * Sums each mime's bytes across every cluster's own `by_mime` breakdown, folding the null-mime bucket
 * into one "unknown" label. Pure and exported so the aggregation is unit-testable.
 */
export function aggregateMimeBytes(clusters: { by_mime: MimeCount[] }[]): { name: string; value: number }[] {
  const totals = new Map<string, number>();
  for (const c of clusters) {
    for (const m of c.by_mime) {
      const key = m.mime ?? 'unknown (not obtained)';
      totals.set(key, (totals.get(key) ?? 0) + m.bytes);
    }
  }
  return Array.from(totals, ([name, value]) => ({ name, value }));
}

/** All segments of a ring, including the synthetic "Other" one when non-empty. */
function ringItems(ring: DonutRing): DonutSegment[] {
  return ring.otherValue > 0
    ? [...ring.segments, { label: `Other (${ring.otherCount})`, value: ring.otherValue, color: OTHER_COLOR }]
    : ring.segments;
}

interface Arc extends DonutSegment {
  dasharray: string;
  dashoffset: number;
}

/** Lay a ring's segments end-to-end around a circle of the given radius, in SVG user units. */
function ringArcs(ring: DonutRing, radius: number): Arc[] {
  const circumference = 2 * Math.PI * radius;
  let offset = 0;
  return ringItems(ring).map((seg) => {
    const frac = ring.total > 0 ? seg.value / ring.total : 0;
    const len = frac * circumference;
    const arc: Arc = { ...seg, dasharray: `${len} ${Math.max(circumference - len, 0)}`, dashoffset: -offset };
    offset += len;
    return arc;
  });
}

interface HoverInfo {
  label: string;
  value: number;
  total: number;
}

/**
 * A donut chart: an outer ring, and optionally an inner ring (e.g. the drill-down detail for
 * whichever outer segment is selected). Hand-rolled SVG — concentric stroked circles via
 * stroke-dasharray/stroke-dashoffset — rather than a charting library, consistent with why the old
 * d3-based sunburst.html was dropped rather than kept.
 */
function Donut({
  outer,
  inner,
  selectedLabel,
  onSelect,
  onHover,
}: {
  outer: DonutRing;
  inner?: DonutRing | null;
  selectedLabel?: string | null;
  onSelect?: (label: string) => void;
  onHover: (info: HoverInfo | null) => void;
}) {
  const outerArcs = ringArcs(outer, 40);
  const innerArcs = inner ? ringArcs(inner, 24) : null;

  const segProps = (a: Arc, radius: number, strokeWidth: number, total: number, selectable: boolean) => ({
    r: radius,
    cx: 50,
    cy: 50,
    fill: 'none',
    stroke: a.color,
    strokeWidth,
    strokeDasharray: a.dasharray,
    strokeDashoffset: a.dashoffset,
    tabIndex: 0,
    role: selectable ? 'button' : undefined,
    'aria-label': a.label,
    className: `dbstat-donut-seg${selectedLabel === a.label ? ' is-selected' : ''}`,
    onMouseEnter: () => onHover({ label: a.label, value: a.value, total }),
    onFocus: () => onHover({ label: a.label, value: a.value, total }),
    onMouseLeave: () => onHover(null),
    onBlur: () => onHover(null),
    onClick: selectable && onSelect ? () => onSelect(a.label) : undefined,
    onKeyDown:
      selectable && onSelect
        ? (e: ReactKeyboardEvent) => {
            if (e.key === 'Enter' || e.key === ' ') {
              e.preventDefault();
              onSelect(a.label);
            }
          }
        : undefined,
  });

  return (
    <svg className="dbstat-donut" viewBox="0 0 100 100" role="img" aria-label="Storage breakdown">
      <g transform="rotate(-90 50 50)">
        {outerArcs.map((a) => (
          <circle key={a.label} {...segProps(a, 40, 14, outer.total, Boolean(onSelect))}>
            <title>{a.label}</title>
          </circle>
        ))}
        {innerArcs?.map((a) => (
          <circle key={a.label} {...segProps(a, 24, 12, inner?.total ?? 0, false)}>
            <title>{a.label}</title>
          </circle>
        ))}
      </g>
    </svg>
  );
}

/** One row of a DonutLegend: swatch + label(+sub) + formatted value + share. Shared by top-N segment
 * rows, the "Other" row, and its expanded sub-rows so the three only differ in the props they pass. */
function LegendRow({
  color,
  label,
  sub,
  caret,
  valueText,
  pctText,
  selected,
  onClick,
  onHover,
  hoverInfo,
}: {
  color: string;
  label: string;
  sub?: string;
  caret?: string;
  valueText: string;
  pctText: string;
  selected?: boolean;
  onClick?: () => void;
  onHover: (info: HoverInfo | null) => void;
  hoverInfo: HoverInfo;
}) {
  return (
    <div
      className={`dbstat-legend-row${selected ? ' is-selected' : ''}`}
      tabIndex={0}
      role={onClick ? 'button' : undefined}
      onClick={onClick}
      onKeyDown={
        onClick
          ? (e) => {
              if (e.key === 'Enter' || e.key === ' ') {
                e.preventDefault();
                onClick();
              }
            }
          : undefined
      }
      onMouseEnter={() => onHover(hoverInfo)}
      onFocus={() => onHover(hoverInfo)}
      onMouseLeave={() => onHover(null)}
      onBlur={() => onHover(null)}
    >
      <span className="dbstat-legend-swatch" style={{ background: color }} />
      <span className="dbstat-legend-label-group">
        <span className="dbstat-legend-label" title={label}>
          {caret && <span className="dbstat-legend-caret">{caret}</span>}
          {label}
        </span>
        {sub && <span className="dbstat-legend-sub muted">{sub}</span>}
      </span>
      <span className="dbstat-legend-value muted">{valueText}</span>
      <span className="dbstat-legend-pct muted">{pctText}</span>
    </div>
  );
}

/** The legend for a donut ring: swatch + label + formatted value + share, doubling as the table-view
 * twin (every value stays readable without hovering the chart). The folded "Other" row is itself
 * expandable — clicking it reveals the individual items that got folded in, since a user comparing
 * clusters or content types may specifically want to know what "Other" is hiding. */
function DonutLegend({
  ring,
  format,
  selectedLabel,
  onSelect,
  onHover,
}: {
  ring: DonutRing;
  format: (n: number) => string;
  selectedLabel?: string | null;
  onSelect?: (label: string) => void;
  onHover: (info: HoverInfo | null) => void;
}) {
  const [otherExpanded, setOtherExpanded] = useState(false);
  const pctText = (value: number) => (ring.total > 0 ? `${pct(value, ring.total).toFixed(1)}%` : '—');

  return (
    <ul className="dbstat-legend">
      {ring.segments.map((seg) => (
        <li key={seg.label}>
          <LegendRow
            color={seg.color}
            label={seg.label}
            sub={seg.sub}
            valueText={format(seg.value)}
            pctText={pctText(seg.value)}
            selected={selectedLabel === seg.label}
            onClick={onSelect ? () => onSelect(seg.label) : undefined}
            onHover={onHover}
            hoverInfo={{ label: seg.label, value: seg.value, total: ring.total }}
          />
        </li>
      ))}
      {ring.otherValue > 0 && (
        <li>
          <LegendRow
            color={OTHER_COLOR}
            label={`Other (${ring.otherCount})`}
            caret={otherExpanded ? '▾' : '▸'}
            valueText={format(ring.otherValue)}
            pctText={pctText(ring.otherValue)}
            onClick={() => setOtherExpanded((v) => !v)}
            onHover={onHover}
            hoverInfo={{ label: `Other (${ring.otherCount})`, value: ring.otherValue, total: ring.total }}
          />
          {otherExpanded && (
            <ul className="dbstat-legend-sublist">
              {ring.otherItems.map((item) => (
                <li key={item.label}>
                  <LegendRow
                    color={item.color}
                    label={item.label}
                    sub={item.sub}
                    valueText={format(item.value)}
                    pctText={pctText(item.value)}
                    onHover={onHover}
                    hoverInfo={{ label: item.label, value: item.value, total: ring.total }}
                  />
                </li>
              ))}
            </ul>
          )}
        </li>
      )}
    </ul>
  );
}

function readoutText(hover: HoverInfo | null, format: (n: number) => string, fallback: string): string {
  if (!hover) return fallback;
  const share = hover.total > 0 ? ` (${pct(hover.value, hover.total).toFixed(1)}%)` : '';
  return `${hover.label} — ${format(hover.value)}${share}`;
}

/** A collapsible section with a caret header and an optional right-aligned summary. */
function Section({
  title,
  summary,
  open,
  onToggle,
  children,
}: {
  title: string;
  summary?: string;
  open: boolean;
  onToggle: () => void;
  children: ReactNode;
}) {
  return (
    <div className="dbstat-section">
      <button type="button" className="dbstat-section-head" onClick={onToggle}>
        <span className="dbstat-caret">{open ? '▾' : '▸'}</span>
        <span className="dbstat-section-title">{title}</span>
        {summary && <span className="muted dbstat-section-summary">{summary}</span>}
      </button>
      {open && <div className="dbstat-section-body">{children}</div>}
    </div>
  );
}

const ALL_SECTIONS = ['overview', 'storage', 'clusters'] as const;
type SectionKey = (typeof ALL_SECTIONS)[number];

function DatabaseStatsPanel({ host }: PanelProps) {
  const [db, setDb] = useState<DatabaseStats | null>(null);
  const [storage, setStorage] = useState<StorageNode | null>(null);
  const [clusters, setClusters] = useState<Cluster[] | null>(null);
  const [version, setVersion] = useState<VersionInfo | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);

  const [openSections, setOpenSections] = useState<Set<SectionKey>>(new Set(ALL_SECTIONS));
  const [selectedTable, setSelectedTable] = useState<string | null>(null);
  const [storageHover, setStorageHover] = useState<HoverInfo | null>(null);
  const [clusterHover, setClusterHover] = useState<HoverInfo | null>(null);

  const refresh = useCallback(
    async (signal?: AbortSignal) => {
      setLoading(true);
      setError(null);
      // Fetch every source independently so one failing endpoint doesn't blank the whole panel.
      const [dbRes, storageRes, clustersRes, versionRes] = await Promise.allSettled([
        host.stats.database(signal),
        host.stats.storage(signal),
        host.http.fetch('/clusters/list').then((r) => (r.ok ? (r.json() as Promise<Cluster[]>) : Promise.reject(new Error(`/clusters/list → ${r.status}`)))),
        host.http.fetch('/version').then((r) => (r.ok ? (r.json() as Promise<VersionInfo>) : Promise.reject(new Error(`/version → ${r.status}`)))),
      ]);
      if (signal?.aborted) return;

      if (dbRes.status === 'fulfilled') setDb(dbRes.value);
      if (storageRes.status === 'fulfilled') setStorage(storageRes.value);
      if (clustersRes.status === 'fulfilled') setClusters(clustersRes.value);
      if (versionRes.status === 'fulfilled') setVersion(versionRes.value);

      const failed = [dbRes, storageRes, clustersRes, versionRes].find((r) => r.status === 'rejected');
      if (failed && failed.status === 'rejected') {
        setError(failed.reason instanceof Error ? failed.reason.message : String(failed.reason));
      }
      setLoading(false);
    },
    [host],
  );

  useEffect(() => {
    const controller = new AbortController();
    void refresh(controller.signal);
    return () => controller.abort();
  }, [refresh]);

  const toggleSection = (key: SectionKey) =>
    setOpenSections((prev) => {
      const next = new Set(prev);
      if (next.has(key)) next.delete(key);
      else next.add(key);
      return next;
    });

  const { major, minor, grand } = buildStorageBreakdown(storage);
  const allTables = [...major, ...minor];
  const storageRing = buildDonutRing(
    allTables.map((t) => ({ name: t.name, value: t.footprint })),
    5,
  );
  const selectedTableObj = allTables.find((t) => t.name === selectedTable) ?? null;
  const storageInnerRing = selectedTableObj
    ? buildDonutRing(
        [{ name: 'heap', value: selectedTableObj.heap }, ...selectedTableObj.indexes.map((i) => ({ name: i.name, value: i.value ?? 0 }))],
        3,
      )
    : null;
  const selectTable = (label: string) => setSelectedTable((prev) => (prev === label ? null : label));

  const diskUsed = clusters?.reduce((sum, c) => sum + c.size.used, 0) ?? 0;
  const diskFiles = clusters?.reduce((sum, c) => sum + c.file_count, 0) ?? 0;
  const avgFileSize = diskFiles > 0 ? diskUsed / diskFiles : 0;

  // Outer ring: on-disk usage per cluster. `sub` carries the read-only flag and capacity limit, since
  // that detail no longer has its own bar-list to live in.
  const clustersRing = clusters
    ? buildDonutRing(
        clusters.map((c) => {
          const capped = c.size.limit > 0;
          const sub =
            [c.readonly ? 'read-only' : null, capped ? `limit ${formatBytes(c.size.limit)}` : null].filter(Boolean).join(' · ') ||
            undefined;
          return { name: c.name || c.path, value: c.size.used, sub };
        }),
        5,
      )
    : null;
  // Inner ring: on-disk bytes by mime type, summed from each cluster's own by_mime breakdown — always
  // shown alongside the outer ring rather than gated behind selecting a cluster segment.
  const mimeBytesRing = clusters ? buildDonutRing(aggregateMimeBytes(clusters), 5) : null;

  return (
    <div className="panel-body dbstats">
      <div className="dbstat-toolbar">
        <button type="button" className="toolbar-button" onClick={() => void refresh()} disabled={loading}>
          {loading ? 'Refreshing…' : 'Refresh'}
        </button>
        {version && (
          <span className="muted grow dbstat-build">
            IDHAN {version.idhan_server_version.string} · {version.branch}@{version.commit.slice(0, 8)} · {version.build}
          </span>
        )}
      </div>

      {error && <p className="error">{error}</p>}

      <Section
        title="Overview"
        open={openSections.has('overview')}
        onToggle={() => toggleSection('overview')}
      >
        <ul className="dbstat-overview">
          <StatRow label="Records" value={db ? formatCount(db.records) : '—'} />
          <StatRow label="Tags" value={db ? formatCount(db.tags) : '—'} />
          <StatRow
            label="Mappings"
            value={db ? `${db.mappings_estimated ? '≈ ' : ''}${formatCount(db.mappings)}` : '—'}
            sub={db?.mappings_estimated ? 'estimated' : undefined}
          />
          <StatRow label="DB size" value={grand > 0 ? formatBytes(grand) : '—'} />
          <StatRow label="On disk" value={formatBytes(diskUsed)} sub={`${formatCount(diskFiles)} files`} />
          <StatRow label="Avg file size" value={diskFiles > 0 ? formatBytes(Math.round(avgFileSize)) : '—'} />
          <StatRow label="Clusters" value={db ? formatCount(db.clusters) : String(clusters?.length ?? '—')} />
        </ul>
      </Section>

      <Section
        title="Storage by table"
        summary={grand > 0 ? `${formatBytes(grand)} · ${major.length + minor.length} tables` : undefined}
        open={openSections.has('storage')}
        onToggle={() => toggleSection('storage')}
      >
        {grand === 0 ? (
          <p className="muted">No table storage reported.</p>
        ) : (
          <div className="dbstat-donut-panel">
            <Donut outer={storageRing} inner={storageInnerRing} selectedLabel={selectedTable} onSelect={selectTable} onHover={setStorageHover} />
            <div className="dbstat-donut-side">
              <p className="dbstat-donut-readout muted">
                {readoutText(
                  storageHover,
                  formatBytes,
                  selectedTableObj ? `${selectedTableObj.name} selected — click its segment again to close.` : 'Click a segment for its heap / index breakdown.',
                )}
              </p>
              <DonutLegend ring={storageRing} format={formatBytes} selectedLabel={selectedTable} onSelect={selectTable} onHover={setStorageHover} />
              {storageInnerRing && (
                <>
                  <p className="dbstat-legend-heading muted">{selectedTableObj?.name} breakdown</p>
                  <DonutLegend ring={storageInnerRing} format={formatBytes} onHover={setStorageHover} />
                </>
              )}
            </div>
          </div>
        )}
      </Section>

      <Section
        title="Clusters"
        summary={clusters ? `${clusters.length} · ${formatBytes(diskUsed)}` : undefined}
        open={openSections.has('clusters')}
        onToggle={() => toggleSection('clusters')}
      >
        {clusters === null ? (
          <p className="muted">Loading…</p>
        ) : clusters.length === 0 ? (
          <p className="muted">No clusters configured.</p>
        ) : !clustersRing || clustersRing.total === 0 ? (
          <p className="muted">No files on disk yet.</p>
        ) : (
          <div className="dbstat-donut-panel">
            <Donut outer={clustersRing} inner={mimeBytesRing} onHover={setClusterHover} />
            <div className="dbstat-donut-side">
              <p className="dbstat-donut-readout muted">
                {readoutText(clusterHover, formatBytes, 'Hover or focus a segment for details.')}
              </p>
              <DonutLegend ring={clustersRing} format={formatBytes} onHover={setClusterHover} />
              {mimeBytesRing && mimeBytesRing.total > 0 && (
                <>
                  <p className="dbstat-legend-heading muted">By mime type</p>
                  <DonutLegend ring={mimeBytesRing} format={formatBytes} onHover={setClusterHover} />
                </>
              )}
            </div>
          </div>
        )}
      </Section>
    </div>
  );
}

/** One row of the Overview list: label left, value (+ optional inline sub) right. */
function StatRow({ label, value, sub }: { label: string; value: string; sub?: string }) {
  return (
    <li className="dbstat-overview-row">
      <span className="dbstat-overview-label muted">{label}</span>
      <span className="dbstat-overview-value">
        {value}
        {sub && <span className="dbstat-overview-sub muted"> · {sub}</span>}
      </span>
    </li>
  );
}

export const databaseStatsPanel = {
  // Legacy slug retained so existing saved layouts keep resolving to this panel (see file header).
  type: 'sunburst-stats',
  title: 'Database Stats',
  description: 'Database counts, PostgreSQL storage by table, clusters, and content by type.',
  component: DatabaseStatsPanel,
  configVersion: 1,
  singleton: true,
} as const;
