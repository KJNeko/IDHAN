/**
 * Media Viewer: shows the currently active record full-size. "Active" is whatever the grid last
 * activated through the bus, or failing that the most recently selected record. Images render as
 * <img> and video/audio through native players, which seek using the server's range support.
 * Everything else offers a download link.
 *
 * The immediate neighbours in the ordered result set are prefetched on change, and the arrow buttons
 * and keys page through that set.
 */

import { useCallback, useEffect, useRef, useState } from 'react';
import type { PanelProps, RecordId, SearchResultSet } from '../../host/types';
import { RECORD_ACTIVATE_TOPIC } from './GridPanel';

interface Basic {
  mime?: string;
  extension?: string;
  size?: number;
}

function kindOf(mime: string | undefined): 'image' | 'video' | 'audio' | 'other' {
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
  const [meta, setMeta] = useState<Basic | null>(null);
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
    let cancelled = false;
    host.records
      .getMetadata([activeId])
      .then((res) => {
        if (cancelled) return;
        const record = res.records[0] as (Basic & { record_id: RecordId }) | undefined;
        setMeta(record ?? {});
      })
      .catch(() => {
        if (!cancelled) setMeta({});
      });
    return () => {
      cancelled = true;
    };
  }, [host, activeId]);

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
        const record = res.records[0] as Basic | undefined;
        if (kindOf(record?.mime) !== 'image') return;
        const url = host.records.fileUrl(id);
        if (prefetched.current.has(url)) return;
        prefetched.current.add(url);
        const img = new Image();
        img.src = url;
      });
    }
  }, [host, activeIndex, ids]);

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

  const kind = kindOf(meta?.mime);
  const fileUrl = host.records.fileUrl(activeId);

  return (
    <div className="viewer-panel" tabIndex={0} onKeyDown={onKeyDown}>
      <div className="viewer-stage">
        {kind === 'image' && <img className="viewer-media" src={fileUrl} alt={`#${activeId}`} draggable={false} />}
        {kind === 'video' && <video className="viewer-media" src={fileUrl} controls autoPlay={false} />}
        {kind === 'audio' && <audio className="viewer-audio" src={fileUrl} controls />}
        {kind === 'other' && (
          <div className="viewer-fallback">
            <p className="muted">No inline preview for {meta?.mime ?? 'this type'}.</p>
            <a className="viewer-download" href={host.records.fileUrl(activeId, { download: true })}>
              Download
            </a>
          </div>
        )}
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
