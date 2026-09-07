import { describe, expect, it } from 'vitest';
import { entryFromHtml } from './buildFreshness';

const built = (entry: string) => `<!doctype html>
<html lang="en">
  <head>
    <script type="importmap">{ "imports": { "react": "/idhan-runtime/react.mjs" } }</script>
    <link rel="modulepreload" href="/assets/vendor-9f8e7d6c.js" />
  </head>
  <body>
    <div id="root"></div>
    <script type="module" crossorigin src="${entry}"></script>
  </body>
</html>`;

describe('entryFromHtml', () => {
  it('finds the hashed entry module', () => {
    expect(entryFromHtml(built('/assets/index-a1b2c3d4.js'))).toBe('/assets/index-a1b2c3d4.js');
  });

  it('reports a different path once the build changes', () => {
    const before = entryFromHtml(built('/assets/index-a1b2c3d4.js'));
    const after = entryFromHtml(built('/assets/index-e5f6a7b8.js'));
    expect(after).not.toBe(before);
  });

  it('ignores the import map and preloads, which are not the entry', () => {
    expect(entryFromHtml(built('/assets/index-a1b2c3d4.js'))).not.toContain('vendor');
  });

  it('resolves a relative entry to a path', () => {
    expect(entryFromHtml(built('assets/index-a1b2c3d4.js'))).toBe('/assets/index-a1b2c3d4.js');
  });

  it('returns null for markup with no module entry', () => {
    expect(entryFromHtml('<!doctype html><html><body></body></html>')).toBeNull();
  });
});
