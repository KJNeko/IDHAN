/**
 * Presentational record-info list, shared by the Record Info and Import panels. Takes an
 * already-fetched info object and renders it, with no host access and no fetching of its own.
 */

import type { ReactNode } from 'react';
import type { RecordId } from '../../host/types';

export type RecordInfo = Record<string, unknown> & { record_id: RecordId };

export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  const units = ['KB', 'MB', 'GB', 'TB'];
  let value = bytes / 1024;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return `${value.toFixed(value < 10 ? 1 : 0)} ${units[unit]}`;
}

/** Fields rendered explicitly, in order; everything else scalar is appended generically. */
const KNOWN = new Set(['record_id', 'hashes', 'size', 'mime', 'extension', 'width', 'height', 'duration']);

function Row({ label, children }: { label: string; children: ReactNode }) {
  return (
    <>
      <dt>{label}</dt>
      <dd>{children}</dd>
    </>
  );
}

export function RecordInfoView({ info }: { info: RecordInfo }) {
  const sha256 = (info.hashes as { sha256?: string } | undefined)?.sha256;
  const size = typeof info.size === 'number' ? info.size : undefined;
  const width = typeof info.width === 'number' ? info.width : undefined;
  const height = typeof info.height === 'number' ? info.height : undefined;
  const duration = typeof info.duration === 'number' ? info.duration : undefined;

  const extras = Object.entries(info).filter(
    ([key, value]) =>
      !KNOWN.has(key) && (typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean'),
  );

  return (
    <dl className="info-list">
      <Row label="Record">#{info.record_id}</Row>
      {typeof info.mime === 'string' && <Row label="Type">{info.mime}</Row>}
      {typeof info.extension === 'string' && <Row label="Extension">.{info.extension}</Row>}
      {size !== undefined && (
        <Row label="Size">
          {formatBytes(size)} <span className="muted">({size.toLocaleString()} B)</span>
        </Row>
      )}
      {width !== undefined && height !== undefined && <Row label="Dimensions">{width} × {height}</Row>}
      {duration !== undefined && <Row label="Duration">{duration.toFixed(2)} s</Row>}
      {sha256 && (
        <Row label="SHA-256">
          <span className="mono">{sha256}</span>
        </Row>
      )}
      {extras.map(([key, value]) => (
        <Row key={key} label={key}>
          {String(value)}
        </Row>
      ))}
    </dl>
  );
}
