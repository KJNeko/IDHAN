/**
 * Render the active record, falling back to the latest selection. Prefetch adjacent records for
 * navigation. Display the thumbnail behind the full image while it loads.
 */

import { useCallback, useEffect, useRef, useState } from 'react';
import type {RecordMetadata} from '../../api/types';
import type { PanelProps, RecordId, SearchResultSet } from '../../host/types';
import {RECORD_ACTIVATE_TOPIC, useRecordMenu} from './recordActions';

/**
 * The server's own category is authoritative where it has one; the mime prefix is the fallback for
 * records nothing has parsed yet. Animations render in an <img>, which is what the browser wants for
 * gif/apng/webp, and an image project has no inline preview.
 */
function kindOf(meta: RecordMetadata | null): 'image' | 'video' | 'audio' | 'other' {
    switch (meta?.simple_type) {
        case 'image':
        case 'animation':
            return 'image';
        case 'video':
            return 'video';
        case 'audio':
            return 'audio';
        case 'archive':
        case 'image_project':
            return 'other';
        default:
            break;
    }

    const mime = meta?.mime;
  if (!mime) return 'other';
  if (mime.startsWith('image/')) return 'image';
  if (mime.startsWith('video/')) return 'video';
  if (mime.startsWith('audio/')) return 'audio';
  return 'other';
}

function MediaViewerPanel({ host }: PanelProps) {
  const [results, setResults] = useState<SearchResultSet>(() => host.results.get());
  const [activeId, setActiveId] = useState<RecordId | null>(() => {
    const sel = host.selection.get();
    return sel.length > 0 ? sel[sel.length - 1]! : null;
  });
    const [meta, setMeta] = useState<{ id: RecordId; record: RecordMetadata | null } | null>(null);
  const [loadedId, setLoadedId] = useState<RecordId | null>(null);
  const prefetched = useRef(new Set<string>());

  useEffect(() => host.results.subscribe(setResults), [host]);
  useEffect(() => host.bus.on(RECORD_ACTIVATE_TOPIC, (payload) => {
    if (typeof payload === 'number') setActiveId(payload);
  }), [host]);
  useEffect(() => host.selection.subscribe((ids) => {
    if (ids.length > 0) setActiveId(ids[ids.length - 1]!);
  }), [host]);

    // Resolve the active record's kind from the shared metadata cache.
  useEffect(() => {
    if (activeId === null) {
      setMeta(null);
      return;
    }
    const id = activeId;
    let cancelled = false;
    host.records
      .getMetadata([id])
      .then((res) => {
        if (cancelled) return;
          setMeta({ id, record: res.records[0] ?? null });
      })
      .catch(() => {
          if (!cancelled) setMeta({ id, record: null });
      });
    return () => {
      cancelled = true;
    };
  }, [host, activeId]);

  const record = meta !== null && meta.id === activeId ? meta.record : null;
  const metaPending = meta === null || meta.id !== activeId;
  const kind = metaPending ? null : kindOf(record);
  // Images become ready after decoding; streaming media becomes ready when metadata is
  // available.
  const ready = kind !== null && (kind !== 'image' || loadedId === activeId);

  const ids = results.ids;
  const activeIndex = activeId === null ? -1 : ids.indexOf(activeId);

  const step = useCallback(
    (delta: number) => {
      if (activeIndex < 0) return;
      const next = activeIndex + delta;
      if (next < 0 || next >= ids.length) return;
      setActiveId(ids[next]!);
    },
    [activeIndex, ids],
  );

  // Prefetch the immediate neighbours' full files (images only; video/audio stream on demand).
  useEffect(() => {
    if (activeIndex < 0) return;
    for (const neighbour of [activeIndex - 1, activeIndex + 1]) {
      if (neighbour < 0 || neighbour >= ids.length) continue;
      const id = ids[neighbour]!;
      void host.records.getMetadata([id]).then((res) => {
          if (kindOf(res.records[0] ?? null) !== 'image') return;
        const url = host.records.fileUrl(id);
        if (prefetched.current.has(url)) return;
        prefetched.current.add(url);
        const img = new Image();
        img.src = url;
      });
    }
  }, [host, activeIndex, ids]);

    const {openRecordMenu, recordMenu} = useRecordMenu(host);

  function onKeyDown(event: React.KeyboardEvent) {
    if (event.key === 'ArrowLeft') {
      event.preventDefault();
      step(-1);
    } else if (event.key === 'ArrowRight') {
      event.preventDefault();
      step(1);
    }
  }

  if (activeId === null) {
    return (
      <div className="panel-body viewer-empty">
        <p className="muted">Select a record to view it.</p>
      </div>
    );
  }

  const fileUrl = host.records.fileUrl(activeId);
  // Reserve the image aspect ratio so the thumbnail backdrop is visible before the full image
  // loads.
  const dimensions =
    record?.width && record?.height ? { width: record.width, aspectRatio: `${record.width} / ${record.height}` } : null;

  return (
    <div className="viewer-panel" tabIndex={0} onKeyDown={onKeyDown}>
      <div className="viewer-stage">
          {/* Only the image takes the record menu; video and audio keep the browser's own controls menu. */}
          {kind === 'image' && (
              <img
                  key={activeId}
                  className="viewer-media"
                  style={{
                      ...dimensions,
                      // Remove the flattened thumbnail after loading to preserve image
                      // transparency.
                      ...(ready ? null : { backgroundImage: `url("${host.records.thumbnailUrl(activeId)}")` }),
                  }}
                  src={fileUrl}
                  alt={`#${activeId}`}
                  draggable={false}
                  onLoad={() => setLoadedId(activeId)}
                  onError={() => setLoadedId(activeId)}
                  onContextMenu={(event) => openRecordMenu(event, [activeId])}
              />
          )}
        {kind === 'video' && <video className="viewer-media" src={fileUrl} controls autoPlay={false} />}
        {kind === 'audio' && <audio className="viewer-audio" src={fileUrl} controls />}
        {kind === 'other' && (
          <div className="viewer-fallback">
            <p className="muted">No inline preview for {record?.mime ?? 'this type'}.</p>
            <a className="viewer-download" href={host.records.fileUrl(activeId, { download: true })}>
              Download
            </a>
          </div>
        )}
        {!ready && <p className="viewer-loading">Loading...</p>}
      </div>
      <div className="viewer-bar">
        <button type="button" onClick={() => step(-1)} disabled={activeIndex <= 0} title="Previous (←)">
          ←
        </button>
        <span className="muted viewer-pos">
          #{activeId}
          {activeIndex >= 0 && ` · ${(activeIndex + 1).toLocaleString()} / ${ids.length.toLocaleString()}`}
        </span>
        <button
          type="button"
          onClick={() => step(1)}
          disabled={activeIndex < 0 || activeIndex >= ids.length - 1}
          title="Next (→)"
        >
          →
        </button>
      </div>
        {recordMenu}
    </div>
  );
}

export const mediaViewerPanel = {
  type: 'media-viewer',
  title: 'Media Viewer',
  description: 'Full-size view of the active record, with neighbour prefetch and paging.',
  component: MediaViewerPanel,
  configVersion: 1,
} as const;
