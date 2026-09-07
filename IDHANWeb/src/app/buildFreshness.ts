/**
 * Detect build changes by comparing the content-hashed entry module path in the served
 * index.html with the loaded document.
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
        const response = await fetch(new URL('index.html', document.baseURI), { cache: 'no-store' });
        if (!response.ok) return;
        const served = entryFromHtml(await response.text());
        if (!cancelled && served !== null && served !== running) setStale(true);
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
