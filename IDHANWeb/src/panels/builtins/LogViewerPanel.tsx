/**
 * Log Viewer: the server's in-memory ring buffer, via GET /log?level=<level>. The endpoint returns
 * the whole buffer as plain text filtered to the requested level and above, so the body is replaced
 * on each fetch rather than appended. Logs are process-local, so a restart empties this.
 */

import { useCallback, useEffect, useRef, useState } from 'react';
import type { PanelProps } from '../../host/types';

const POLL_MS = 2000;

const LEVELS = ['trace', 'debug', 'info', 'warning', 'error', 'critical'] as const;
type Level = (typeof LEVELS)[number];

function LogViewerPanel({ host }: PanelProps) {
  const [level, setLevel] = useState<Level>('info');
  const [text, setText] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [auto, setAuto] = useState(true);
  const [wrap, setWrap] = useState(false);
  const pre = useRef<HTMLPreElement>(null);
  const timer = useRef<number | null>(null);

  const refresh = useCallback(async () => {
    try {
      const res = await host.http.fetch(`/log?level=${level}`);
      if (!res.ok) throw new Error(`/log → ${res.status}`);
      const body = await res.text();
      setText(body);
      setError(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }, [host, level]);

  useEffect(() => {
    void refresh();
    if (!auto) return;
    timer.current = window.setInterval(() => void refresh(), POLL_MS);
    return () => {
      if (timer.current !== null) window.clearInterval(timer.current);
    };
  }, [auto, refresh]);

  // Keep the newest lines in view unless the user has scrolled up to read history.
  useEffect(() => {
    const el = pre.current;
    if (!el) return;
    const nearBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 40;
    if (nearBottom) el.scrollTop = el.scrollHeight;
  }, [text]);

  const lineCount = text ? text.replace(/\n$/, '').split('\n').length : 0;

  return (
    <div className="panel-body log-viewer">
      <div className="log-toolbar">
        <label className="log-level">
          Level
          <select value={level} onChange={(e) => setLevel(e.target.value as Level)}>
            {LEVELS.map((l) => (
              <option key={l} value={l}>
                {l}
              </option>
            ))}
          </select>
        </label>
        <button type="button" className="toolbar-button" onClick={() => void refresh()}>
          Refresh
        </button>
        <label className="log-check">
          <input type="checkbox" checked={auto} onChange={(e) => setAuto(e.target.checked)} />
          Auto
        </label>
        <label className="log-check">
          <input type="checkbox" checked={wrap} onChange={(e) => setWrap(e.target.checked)} />
          Wrap
        </label>
        <span className="muted grow">
          {lineCount} line{lineCount === 1 ? '' : 's'}
        </span>
      </div>

      {error && <p className="error">{error}</p>}
      <pre ref={pre} className={`log-body${wrap ? ' wrap' : ''}`}>
        {text || (error ? '' : 'No log output at this level.')}
      </pre>
    </div>
  );
}

export const logViewerPanel = {
  type: 'log-viewer',
  title: 'Log Viewer',
  description: "The server's in-memory log ring buffer, filtered by level.",
  component: LogViewerPanel,
  configVersion: 1,
  singleton: true,
} as const;
