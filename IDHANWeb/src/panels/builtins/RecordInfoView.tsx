/**
 * Presentational record-info list, shared by the Record Info and Import panels. Takes an
 * already-fetched info object and renders it, with no host access and no fetching of its own.
 */

import type { ReactNode } from 'react';
import type {RecordMetadata} from '../../api/types';

export type RecordInfo = RecordMetadata;

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

export function formatDuration(seconds: number): string {
    if (!Number.isFinite(seconds) || seconds < 0) return '-';
    const whole = Math.floor(seconds);
    const hours = Math.floor(whole / 3600);
    const minutes = Math.floor((whole % 3600) / 60);
    const secs = whole % 60;
    const mmss = `${String(minutes).padStart(hours > 0 ? 2 : 1, '0')}:${String(secs).padStart(2, '0')}`;
    return hours > 0 ? `${hours}:${mmss}` : mmss;
}

function formatBitrate(bitsPerSecond: number): string {
    if (bitsPerSecond >= 1_000_000) return `${(bitsPerSecond / 1_000_000).toFixed(2)} Mbps`;
    if (bitsPerSecond >= 1000) return `${Math.round(bitsPerSecond / 1000)} kbps`;
    return `${bitsPerSecond} bps`;
}

const SIMPLE_TYPE_LABELS: Record<string, string> = {
    none: 'Unknown',
    image: 'Image',
    video: 'Video',
    animation: 'Animation',
    audio: 'Audio',
    archive: 'Archive',
    image_project: 'Image project',
};

/**
 * Fields the list renders itself. Anything else scalar on the object is appended generically, so a
 * new server field shows up without a WebUI change.
 */
const KNOWN = new Set([
    'record_id',
    'hashes',
    'size',
    'mime',
    'extension',
    'parsed',
    'simple_type',
    'extra',
    'width',
    'height',
    'channels',
    'layers',
    'duration',
    'bitrate',
    'framerate',
    'has_audio',
    'sample_rate',
    'frame_count',
    'loops',
    'archive_id',
    'encrypted',
    'file_count',
]);

function Row({ label, children }: { label: string; children: ReactNode }) {
  return (
    <>
      <dt>{label}</dt>
      <dd>{children}</dd>
    </>
  );
}

const num = (value: unknown): number | undefined => (typeof value === 'number' ? value : undefined);
const bool = (value: unknown): boolean | undefined => (typeof value === 'boolean' ? value : undefined);

export function RecordInfoView({ info }: { info: RecordInfo }) {
    const sha256 = info.hashes?.sha256;
    const size = num(info.size);
    const width = num(info.width);
    const height = num(info.height);
    const channels = num(info.channels);
    const layers = num(info.layers);
    const duration = num(info.duration);
    const bitrate = num(info.bitrate);
    const framerate = num(info.framerate);
    const sampleRate = num(info.sample_rate);
    const frameCount = num(info.frame_count);
    const archiveId = num(info.archive_id);
    const fileCount = num(info.file_count);
    const hasAudio = bool(info.has_audio);
    const loops = bool(info.loops);
    const encrypted = bool(info.encrypted);

    // `extra` is a nested object rather than a scalar, so it is flattened one level and prefixed.
    const extraEntries = Object.entries(info.extra ?? {}).filter(
        ([, value]) => typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean',
    );

    const unknownEntries = Object.entries(info).filter(
    ([key, value]) =>
      !KNOWN.has(key) && (typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean'),
  );

  return (
    <dl className="info-list">
      <Row label="Record">#{info.record_id}</Row>
      {typeof info.mime === 'string' && <Row label="Type">{info.mime}</Row>}
        {typeof info.simple_type === 'string' && (
            <Row label="Category">{SIMPLE_TYPE_LABELS[info.simple_type] ?? info.simple_type}</Row>
        )}
      {typeof info.extension === 'string' && <Row label="Extension">.{info.extension}</Row>}
      {size !== undefined && (
        <Row label="Size">
          {formatBytes(size)} <span className="muted">({size.toLocaleString()} B)</span>
        </Row>
      )}

        {width !== undefined && height !== undefined && (
            <Row label="Dimensions">
                {width} x {height}
            </Row>
        )}
        {channels !== undefined && <Row label="Channels">{channels}</Row>}
        {layers !== undefined && <Row label="Layers">{layers}</Row>}

        {duration !== undefined && (
            <Row label="Duration">
                {formatDuration(duration)} <span className="muted">({duration.toFixed(2)} s)</span>
            </Row>
        )}
        {framerate !== undefined && <Row label="Framerate">{framerate.toFixed(2)} fps</Row>}
        {frameCount !== undefined && <Row label="Frames">{frameCount.toLocaleString()}</Row>}
        {loops !== undefined && <Row label="Loops">{loops ? 'Yes' : 'No'}</Row>}
        {bitrate !== undefined && <Row label="Bitrate">{formatBitrate(bitrate)}</Row>}
        {sampleRate !== undefined && <Row label="Sample rate">{sampleRate.toLocaleString()} Hz</Row>}
        {hasAudio !== undefined && <Row label="Audio">{hasAudio ? 'Yes' : 'No'}</Row>}

        {archiveId !== undefined && <Row label="Archive">#{archiveId}</Row>}
        {fileCount !== undefined && <Row label="Contains">{fileCount.toLocaleString()} files</Row>}
        {encrypted !== undefined && <Row label="Encrypted">{encrypted ? 'Yes' : 'No'}</Row>}

      {sha256 && (
        <Row label="SHA-256">
          <span className="mono">{sha256}</span>
        </Row>
      )}
        {info.parsed === false && (
            <Row label="Metadata">
                <span className="muted">Not yet parsed</span>
            </Row>
        )}

        {extraEntries.map(([key, value]) => (
            <Row key={`extra:${key}`} label={key}>
                {String(value)}
            </Row>
        ))}
        {unknownEntries.map(([key, value]) => (
        <Row key={key} label={key}>
          {String(value)}
        </Row>
      ))}
    </dl>
  );
}
