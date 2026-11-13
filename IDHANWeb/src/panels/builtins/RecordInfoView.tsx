import type { ReactNode } from 'react';
import type {RecordMetadata} from '../../api/types';

export type RecordInfo = RecordMetadata;

export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
    const units = ['KiB', 'MiB', 'GiB', 'TiB', 'PiB', 'EiB'];
  let value = bytes / 1024;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
    return `${value.toLocaleString(undefined, {maximumFractionDigits: 2})} ${units[unit]}`;
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

function formatFileTimestamp(microseconds: number): string {
    const date = new Date(microseconds / 1000);
    if (!Number.isFinite(date.getTime())) return '-';

    return date.toLocaleString(undefined, {
        year: 'numeric',
        month: 'short',
        day: 'numeric',
        hour: 'numeric',
        minute: '2-digit',
        second: '2-digit',
        timeZoneName: 'short',
    });
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

const KNOWN = new Set([
    'record_id',
    'hashes',
    'tags',
    'urls',
    'size',
    'file_mtime',
    'file_ctime',
    'mime',
    'extension',
    'parsed',
    'simple_type',
    'extra',
    'width',
    'height',
    'channels',
    'has_exif',
    'has_gps',
    'has_xmp',
    'has_iptc',
    'has_icc_profile',
    'lossless',
    'quality',
    'chroma_subsampling',
    'progressive',
    'comment',
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

export const EMBEDDED_BLOCKS = [
    {key: 'has_exif', label: 'EXIF'},
    {key: 'has_gps', label: 'GPS'},
    {key: 'has_xmp', label: 'XMP'},
    {key: 'has_iptc', label: 'IPTC'},
    {key: 'has_icc_profile', label: 'ICC profile'},
] as const;

function Row({ label, children }: { label: string; children: ReactNode }) {
  return (
    <>
      <dt>{label}</dt>
      <dd>{children}</dd>
    </>
  );
}

const num = (value: unknown): number | undefined => (typeof value === 'number' && Number.isFinite(value) ? value : undefined);
const bool = (value: unknown): boolean | undefined => (typeof value === 'boolean' ? value : undefined);

export function RecordInfoView({ info }: { info: RecordInfo }) {
    const sha256 = info.hashes?.sha256;
    const size = num(info.size);
    const fileModified = num(info.file_mtime);
    const fileCreated = num(info.file_ctime);
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
    const lossless = bool(info.lossless);
    const quality = num(info.quality);
    const progressive = bool(info.progressive);
    const embedded = EMBEDDED_BLOCKS.filter(({key}) => info[key] === true).map(({label}) => label);
    // Every block is reported together, so one known flag means the image was parsed for all of them.
    const embeddedKnown = EMBEDDED_BLOCKS.some(({key}) => typeof info[key] === 'boolean');
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

    // Grouped so a record tagged in several domains reads as one row per domain rather than one
    // undifferentiated list.
    const tagDomains = new Map<string, string[]>();
    for (const tag of info.tags ?? []) {
        const texts = tagDomains.get(tag.domain);
        if (texts) texts.push(tag.text);
        else tagDomains.set(tag.domain, [tag.text]);
    }

    const urls = info.urls ?? [];

    const encoding = [
        lossless === undefined ? null : lossless ? 'Lossless' : 'Lossy',
        quality === undefined ? null : `q${quality}`,
        typeof info.chroma_subsampling === 'string' ? info.chroma_subsampling : null,
        progressive === undefined ? null : progressive ? 'Progressive' : 'Baseline',
    ].filter(Boolean);
    const category = typeof info.simple_type === 'string' ? SIMPLE_TYPE_LABELS[info.simple_type] ?? info.simple_type : null;

    return (
        <div className="record-info-view">
            <div className="record-info-overview">
                <strong>#{info.record_id}</strong>
                {(info.mime || category) && <span className="muted">{info.mime || category}</span>}
                {info.mime && (info.simple_type === 'animation' || info.simple_type === 'image_project') &&
                    <span className="muted">{category}</span>}
                {size !== undefined && <span title={`${size.toLocaleString()} bytes`}>{formatBytes(size)}</span>}
                {width !== undefined && height !== undefined && (
                    <span aria-label="Dimensions">{width.toLocaleString()} × {height.toLocaleString()}</span>
                )}
                {duration !== undefined && (
                    <span aria-label="Duration" title={`${duration} seconds`}>{formatDuration(duration)}</span>
                )}
                {framerate !== undefined && <span>{Number(framerate.toFixed(2))} fps</span>}
            </div>
            {info.parsed === false &&
                <p className="record-info-pending muted" role="status">Metadata not yet parsed</p>}

            <dl className="info-list">
                {encoding.length > 0 && <Row label="Encoding">{encoding.join(' · ')}</Row>}
                {embeddedKnown && (
                    <Row label="Embedded">{embedded.length > 0 ? embedded.join(', ') :
                        <span className="muted">None</span>}</Row>
                )}
                {channels !== undefined && <Row label="Channels">{channels}</Row>}
                {layers !== undefined && <Row label="Layers">{layers}</Row>}
                {frameCount !== undefined && <Row label="Frames">{frameCount.toLocaleString()}</Row>}
                {loops !== undefined && <Row label="Loops">{loops ? 'Yes' : 'No'}</Row>}
                {bitrate !== undefined && <Row label="Bitrate">{formatBitrate(bitrate)}</Row>}
                {sampleRate !== undefined && <Row label="Sample rate">{sampleRate.toLocaleString()} Hz</Row>}
                {hasAudio !== undefined && <Row label="Audio">{hasAudio ? 'Yes' : 'No'}</Row>}
                {archiveId !== undefined && <Row label="Archive">#{archiveId}</Row>}
                {fileCount !== undefined && <Row label="Contains">{fileCount.toLocaleString()} files</Row>}
                {encrypted !== undefined && <Row label="Encrypted">{encrypted ? 'Yes' : 'No'}</Row>}

                {[...tagDomains].map(([domain, texts]) => (
                    <Row key={`tags:${domain}`} label={`Tags (${domain})`}>
                        <ul className="info-tags" aria-label={`Tags in ${domain}`}>
                            {texts.map((text, index) => <li key={`${text}:${index}`}>{text}</li>)}
                        </ul>
                    </Row>
                ))}
                {urls.length > 0 && (
                    <Row label={urls.length === 1 ? 'Source' : `Sources (${urls.length})`}>
                        <ul className="info-sources">
                            {urls.map((url, index) => <li key={`${url}:${index}`}><SourceUrl url={url}/></li>)}
                        </ul>
                    </Row>
                )}
                {typeof info.extension === 'string' && info.extension !== '' && <Row
                    label="Extension">{info.extension.startsWith('.') ? info.extension : `.${info.extension}`}</Row>}
                {size !== undefined &&
                    <Row label="Size"><span title={`${size.toLocaleString()} bytes`}>{formatBytes(size)}</span></Row>}
                {fileModified !== undefined && <Row label="File modified">{formatFileTimestamp(fileModified)}</Row>}
                {fileCreated !== undefined && <Row label="File created">{formatFileTimestamp(fileCreated)}</Row>}
                {sha256 && <Row label="SHA-256"><span className="mono info-hash">{sha256}</span></Row>}
                {typeof info.comment === 'string' && info.comment !== '' && <Row label="Comment">{info.comment}</Row>}
                {extraEntries.map(([key, value]) => <Row key={`extra:${key}`} label={key}>{String(value)}</Row>)}
                {unknownEntries.map(([key, value]) => <Row key={key} label={key}>{String(value)}</Row>)}
            </dl>
        </div>
    );
}

function SourceUrl({url}: { url: string }) {
    let source: URL;
    try {
        source = new URL(url);
    } catch {
        return <span className="info-source">{url}</span>;
    }
    if (source.protocol !== 'http:' && source.protocol !== 'https:') {
        return <span className="info-source">{url}</span>;
    }
    return <a className="info-source" href={url} title={url} target="_blank" rel="noreferrer noopener">
        {source.host}{source.pathname}{source.search}{source.hash}
    </a>;
}
