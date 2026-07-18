# WebUI Plugins

The IDHAN WebUI panel catalog is extensible. A plugin is a JavaScript ES-module bundle that registers
one or more panels through the same `@idhan/host` surface the built-in panels use. Plugins run in the
browser — they are **not** the C++ `ModuleLoader`/`dlopen` system, and they never execute on the server.

> Plugins are **trusted code sharing the page's JS realm**. `@idhan/host` is a convention boundary that
> keeps panels decoupled from app internals — it is *not* a sandbox. Only install plugins you trust.

## How loading works

1. Bundles live under the server's static root at `<static>/plugins/<dir>/`, each with a `manifest.json`.
   The scan directory defaults to `<static>/plugins` and is overridable via `[plugins] path` (an
   override must still be reachable under the `/plugins` URL for the browser to fetch the bundle).
2. `GET /plugins` scans that directory, validates each manifest, and returns the index with a resolved
   `entry` URL. The result is cached; `GET /plugins?rescan=true` forces a fresh scan.
3. After login the WebUI fetches the index and, for each plugin whose declared `hostApi` range is
   compatible with the host, dynamic-imports the bundle. The bundle self-registers its panels.
4. Incompatible or failing plugins are logged to the console and skipped — they never block the app.

### Shared React via the import map

Two copies of React on one page break hooks, so plugins must **not** bundle React. They mark
`react`, `react-dom`, `react/jsx-runtime`, and `@idhan/host` as **external** and emit bare specifiers.
The host page ships a static import map (`index.html`) that resolves those specifiers to small shim
modules under `/idhan-runtime/`, which re-export the host's own instances. This works identically in
dev and production — no per-build import-map generation needed.

## manifest.json

```jsonc
{
  "id": "com.example.myplugin",   // unique, reverse-DNS recommended
  "name": "My Plugin",            // shown to users
  "version": "1.0.0",             // the plugin's own version
  "hostApi": "^1.0.0",            // host API range this plugin targets (caret / exact / "*")
  "entry": "index.js",            // bundle filename, relative to this dir (no "/" or "..")
  "description": "What it does.", // optional
  "panels": [                     // optional, for display before the bundle loads
    { "type": "myplugin.thing", "title": "Thing" }
  ]
}
```

The current host API version is `1.0.0`. If `hostApi` is not satisfied, the plugin is refused with a
console warning rather than loaded against a mismatched surface.

## Writing a panel

A plugin's entry module imports `registerPanel` from `@idhan/host` and registers a `PanelDefinition`
whose `component` receives `{ host }`. See `IDHANWeb/public/plugins/hello/` for a complete, dependency-
free example (hand-written plain JS, no build step). A typical TSX plugin instead looks like:

```tsx
import { useState } from 'react';
import { registerPanel, type PanelProps } from '@idhan/host';

function MyPanel({ host }: PanelProps) {
  const [ids, setIds] = useState<readonly number[]>(host.selection.get());
  // host.search, host.records.getMetadata, host.tags.autocomplete, host.results, host.bus, …
  return <div className="panel-body">Selected: {ids.length}</div>;
}

registerPanel({ type: 'myplugin.mine', title: 'Mine', component: MyPanel });
```

### Building a real plugin

Build to a single ES module with the shared deps external. With Vite library mode:

```ts
// vite.config.ts (plugin)
export default {
  build: {
    lib: { entry: 'src/index.tsx', formats: ['es'], fileName: () => 'index.js' },
    rollupOptions: { external: ['react', 'react-dom', 'react/jsx-runtime', '@idhan/host'] },
  },
};
```

Ship `index.js` alongside `manifest.json` in a directory under the server's `<static>/plugins/`, then
`GET /plugins?rescan=true` (or restart) to pick it up.

## Dev-mode note

In `pnpm dev`, `GET /plugins` is proxied to the running IDHANServer, so the index reflects the
**server's** static directory, not Vite's `public/`. To test a plugin against the dev server, place it
under the server's `<static>/plugins/` (or point the server's static path at the built `dist/`). The
import-map / shared-React mechanism itself works the same in dev and production.
