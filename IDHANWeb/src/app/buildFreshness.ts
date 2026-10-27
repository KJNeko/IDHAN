/**
 * Detect build changes by conditionally checking the loaded document's Last-Modified time.
 */

import { useEffect, useState } from 'react';

const CHECK_INTERVAL_MS = 10 * 60 * 1000;

/** Return the entry module path, or null if none is present. */
export function entryScript(doc: Document): string | null {
  const source = doc.querySelector('script[type="module"][src]')?.getAttribute('src');
  if (!source) return null;
  return new URL(source, doc.baseURI).pathname;
}

export function entryFromHtml(html: string): string | null {
  return entryScript(new DOMParser().parseFromString(html, 'text/html'));
}

export function useStaleBuild(): boolean {
  const [stale, setStale] = useState(false);

  useEffect(() => {
    // Development entries are unhashed. Stop checking once a build change is detected.
    if (import.meta.env.DEV || stale) return;

    const running = entryScript(document);
    if (running === null) return;

    let cancelled = false;
    let checking = false;

    const check = async () => {
      if (cancelled || checking) return;
      checking = true;
      try {
          const lastModified = new Date(document.lastModified);
          if (Number.isNaN(lastModified.getTime())) return;

          const response = await fetch(new URL('index.html', document.baseURI), {
              method: 'HEAD',
              headers: {'If-Modified-Since': lastModified.toUTCString()},
              cache: 'no-store',
          });
          if (!cancelled && response.status === 200) setStale(true);
      } catch {
        // Retry transient fetch failures on the next check.
      } finally {
        checking = false;
      }
    };

    const onVisible = () => {
      if (document.visibilityState === 'visible') void check();
    };

    const timer = window.setInterval(() => void check(), CHECK_INTERVAL_MS);
    document.addEventListener('visibilitychange', onVisible);
    window.addEventListener('focus', onVisible);

    return () => {
      cancelled = true;
      window.clearInterval(timer);
      document.removeEventListener('visibilitychange', onVisible);
      window.removeEventListener('focus', onVisible);
    };
  }, [stale]);

  return stale;
}
