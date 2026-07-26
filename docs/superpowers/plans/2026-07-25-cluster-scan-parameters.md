# Cluster Manager scan parameters + Fast-scan preset — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the Cluster Manager WebUI panel set scan parameters via a modal dialog, including a one-click "Fast scan (hash only)" preset — with no server changes.

**Architecture:** Frontend-only. Extract the scan-param data model, the fast-scan preset transform, and the query-string encoder as **pure exported functions** from `ClusterManagerPanel.tsx` (mirroring how `DebugPanel.tsx` exports `buildSortJobs`), unit-test them with vitest, then wire a modal into the panel that POSTs to the existing `/clusters/{id}/scan?<query>` endpoint. Drogon already parses every param from the query string.

**Tech Stack:** React 19 + TypeScript, Vite, Vitest (jsdom), plain CSS in `src/theme/global.css`.

## Global Constraints

- **No backend changes.** The `/clusters/{cluster_id}/scan` handler in `IDHANServer/src/api/cluster/scan.cpp` already parses all params from the query string. Do not touch server code.
- **Booleans are sent as the strings `"true"`/`"false"`** — Drogon's `fromString<bool>` accepts these (case-insensitive); unrecognized values fall back to the server-side default.
- **Fast scan is a UI preset only** — it sets the default-true params to false. It is NOT a new server mode.
- **Fast-scan preset must also set `adopt_orphans=false`.** The server does `scan_metadata |= adopt_orphans; scan_mime |= scan_metadata;`, so leaving `adopt_orphans` on would silently re-enable mime + metadata.
- **Param field names must exactly match the server query keys:** `scan_mime`, `rescan_mime`, `scan_metadata`, `rescan_metadata`, `verify_hash`, `adopt_orphans`, `fix_extensions`, `stop_on_fail`, `remove_missing_files`, `readonly` (the last is the server's force-read-only override, keyed `readonly`).
- **Commits:** The repo maintainer requires that commits happen only when explicitly requested. Treat every "Commit" step below as *staged and ready*, but do not run it unless the user has asked you to commit. Commit each task separately (per-item granularity).
- Run all commands from the `IDHANWeb/` directory.

---

### Task 1: Pure scan-param logic (type, defaults, preset, query encoder)

**Files:**
- Modify: `IDHANWeb/src/panels/builtins/ClusterManagerPanel.tsx` (add exports near the top, after the imports / before the component)
- Test: `IDHANWeb/src/panels/builtins/ClusterManagerPanel.test.ts` (create)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `interface ScanParams` — 10 boolean fields listed in Global Constraints.
  - `const DEFAULT_SCAN_PARAMS: ScanParams` — `scan_mime` and `scan_metadata` true, all others false.
  - `function fastScanPreset(params: ScanParams): ScanParams` — returns a copy with `scan_mime`, `scan_metadata`, `adopt_orphans` forced false, all other fields preserved.
  - `function buildScanQuery(params: ScanParams): string` — returns a `URLSearchParams` string with every field encoded as `"true"`/`"false"`.

- [ ] **Step 1: Write the failing tests**

Create `IDHANWeb/src/panels/builtins/ClusterManagerPanel.test.ts`:

```ts
import { describe, expect, it } from 'vitest';
import {
  DEFAULT_SCAN_PARAMS,
  fastScanPreset,
  buildScanQuery,
  type ScanParams,
} from './ClusterManagerPanel';

describe('DEFAULT_SCAN_PARAMS', () => {
  it('enables mime + metadata scanning and nothing else', () => {
    expect(DEFAULT_SCAN_PARAMS).toEqual({
      scan_mime: true,
      rescan_mime: false,
      scan_metadata: true,
      rescan_metadata: false,
      verify_hash: false,
      adopt_orphans: false,
      fix_extensions: false,
      stop_on_fail: false,
      remove_missing_files: false,
      readonly: false,
    });
  });
});

describe('fastScanPreset', () => {
  it('disables mime, metadata, and adopt_orphans (hash-only)', () => {
    const out = fastScanPreset({ ...DEFAULT_SCAN_PARAMS, adopt_orphans: true });
    expect(out.scan_mime).toBe(false);
    expect(out.scan_metadata).toBe(false);
    expect(out.adopt_orphans).toBe(false);
  });

  it('preserves unrelated params and does not mutate the input', () => {
    const input: ScanParams = { ...DEFAULT_SCAN_PARAMS, verify_hash: true, stop_on_fail: true };
    const out = fastScanPreset(input);
    expect(out.verify_hash).toBe(true);
    expect(out.stop_on_fail).toBe(true);
    expect(input.scan_mime).toBe(true); // input untouched
  });
});

describe('buildScanQuery', () => {
  it('encodes every param as a true/false string', () => {
    const qs = buildScanQuery(DEFAULT_SCAN_PARAMS);
    const parsed = new URLSearchParams(qs);
    expect(parsed.get('scan_mime')).toBe('true');
    expect(parsed.get('scan_metadata')).toBe('true');
    expect(parsed.get('verify_hash')).toBe('false');
    expect(parsed.get('readonly')).toBe('false');
  });

  it('reflects a fast-scan preset in the query string', () => {
    const qs = buildScanQuery(fastScanPreset(DEFAULT_SCAN_PARAMS));
    const parsed = new URLSearchParams(qs);
    expect(parsed.get('scan_mime')).toBe('false');
    expect(parsed.get('scan_metadata')).toBe('false');
  });
});
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pnpm --dir IDHANWeb test -- ClusterManagerPanel`
Expected: FAIL — `DEFAULT_SCAN_PARAMS`, `fastScanPreset`, `buildScanQuery` are not exported (import/resolve error).

- [ ] **Step 3: Add the pure logic to the panel module**

In `IDHANWeb/src/panels/builtins/ClusterManagerPanel.tsx`, after the existing imports and before the `interface Cluster` declaration, add:

```ts
export interface ScanParams {
  scan_mime: boolean;
  rescan_mime: boolean;
  scan_metadata: boolean;
  rescan_metadata: boolean;
  verify_hash: boolean;
  adopt_orphans: boolean;
  fix_extensions: boolean;
  stop_on_fail: boolean;
  remove_missing_files: boolean;
  readonly: boolean;
}

export const DEFAULT_SCAN_PARAMS: ScanParams = {
  scan_mime: true,
  rescan_mime: false,
  scan_metadata: true,
  rescan_metadata: false,
  verify_hash: false,
  adopt_orphans: false,
  fix_extensions: false,
  stop_on_fail: false,
  remove_missing_files: false,
  readonly: false,
};

// Hash-only preset. The server couples params as
//   scan_metadata |= adopt_orphans; scan_mime |= scan_metadata;
// so adopt_orphans MUST be cleared too, else mime + metadata come back on.
export function fastScanPreset(params: ScanParams): ScanParams {
  return { ...params, scan_mime: false, scan_metadata: false, adopt_orphans: false };
}

// Encode as ?scan_mime=true&... — Drogon's fromString<bool> reads "true"/"false".
export function buildScanQuery(params: ScanParams): string {
  const qs = new URLSearchParams();
  for (const [key, value] of Object.entries(params)) {
    qs.set(key, value ? 'true' : 'false');
  }
  return qs.toString();
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `pnpm --dir IDHANWeb test -- ClusterManagerPanel`
Expected: PASS (5 tests).

- [ ] **Step 5: Typecheck**

Run: `pnpm --dir IDHANWeb typecheck`
Expected: no errors.

- [ ] **Step 6: Commit** *(only if the user has asked you to commit — see Global Constraints)*

```bash
git add IDHANWeb/src/panels/builtins/ClusterManagerPanel.tsx IDHANWeb/src/panels/builtins/ClusterManagerPanel.test.ts
git commit -m "feat(webui): add scan-param model, fast-scan preset, and query encoder for Cluster Manager"
```

---

### Task 2: Scan options modal (UI wiring + styles)

**Files:**
- Modify: `IDHANWeb/src/panels/builtins/ClusterManagerPanel.tsx` (component body + the Scan button)
- Modify: `IDHANWeb/src/theme/global.css` (add `.scan-modal-*` classes after the `.cluster-actions` block, ~line 1138)

**Interfaces:**
- Consumes from Task 1: `ScanParams`, `DEFAULT_SCAN_PARAMS`, `fastScanPreset`, `buildScanQuery`.
- Produces: no exports; replaces the old `scan(c)` with `openScan(c)` + `startScan()` + modal render.

- [ ] **Step 1: Add modal state and handlers to the component**

In `ClusterManagerPanel.tsx`, inside `function ClusterManagerPanel({ host })`, add two state hooks next to the existing `useState` calls (after `const [newReadonly, setNewReadonly] = useState(true);`):

```ts
  const [scanTarget, setScanTarget] = useState<Cluster | null>(null);
  const [scanParams, setScanParams] = useState<ScanParams>(DEFAULT_SCAN_PARAMS);
```

Delete the existing `async function scan(c: Cluster) { … }` block entirely and replace it with:

```ts
  function openScan(c: Cluster) {
    setScanParams(DEFAULT_SCAN_PARAMS);
    setScanTarget(c);
  }

  function closeScan() {
    setScanTarget(null);
  }

  async function startScan() {
    const c = scanTarget;
    if (!c) return;
    setBusy(true);
    try {
      const qs = buildScanQuery(scanParams);
      const res = await host.http.fetch(`/clusters/${c.cluster_id}/scan?${qs}`, { method: 'POST' });
      if (!res.ok) throw new Error(`scan → ${res.status}`);
      host.ui.toast(`Scan started for "${c.name}".`, { kind: 'info' });
      setScanTarget(null);
    } catch (err) {
      host.ui.toast(`Scan failed: ${err instanceof Error ? err.message : String(err)}`, { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }
```

- [ ] **Step 2: Close the modal on Escape**

Add this effect right after the existing `useEffect(() => { void refresh(); }, [refresh]);` block:

```ts
  useEffect(() => {
    if (!scanTarget) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setScanTarget(null);
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [scanTarget]);
```

- [ ] **Step 3: Point the card's Scan button at the modal**

In the `.cluster-actions` block of the JSX, change the Scan button's handler from the old `void scan(c)` to `openScan(c)`:

```tsx
                  <button type="button" className="toolbar-button" disabled={busy} onClick={() => openScan(c)}>
                    Scan
                  </button>
```

- [ ] **Step 4: Render the modal**

Immediately before the final closing `</div>` of the `return ( <div className="panel-body cluster-manager"> … )`, add the modal. The `scanCheck` helper is declared inline just above the `return` statement (so it closes over `scanParams`/`setScanParams`):

Declare the helper just above `return (`:

```tsx
  const scanCheck = (field: keyof ScanParams, label: string) => (
    <label className="log-check">
      <input
        type="checkbox"
        checked={scanParams[field]}
        onChange={(e) => setScanParams((p) => ({ ...p, [field]: e.target.checked }))}
      />
      {label}
    </label>
  );
```

Add the modal markup before the closing `</div>`:

```tsx
      {scanTarget && (
        <div className="scan-modal-overlay" onClick={closeScan}>
          <div className="scan-modal" onClick={(e) => e.stopPropagation()}>
            <div className="scan-modal-title">Scan cluster “{scanTarget.name}”</div>
            <button
              type="button"
              className="toolbar-button scan-modal-preset"
              disabled={busy}
              onClick={() => setScanParams((p) => fastScanPreset(p))}
            >
              Fast scan (hash only)
            </button>
            <div className="scan-modal-grid">
              {scanCheck('scan_mime', 'Scan mime')}
              {scanCheck('rescan_mime', 'Rescan mime')}
              {scanCheck('scan_metadata', 'Scan metadata')}
              {scanCheck('rescan_metadata', 'Rescan metadata')}
              {scanCheck('verify_hash', 'Verify hash')}
              {scanCheck('adopt_orphans', 'Adopt orphans')}
              {scanCheck('fix_extensions', 'Fix extensions')}
              {scanCheck('stop_on_fail', 'Stop on fail')}
            </div>
            <div className="scan-modal-advanced">
              <span className="muted">Advanced</span>
              <div className="scan-modal-grid">
                {scanCheck('remove_missing_files', 'Remove missing files')}
                {scanCheck('readonly', 'Force read-only')}
              </div>
            </div>
            <div className="scan-modal-actions">
              <button type="button" className="toolbar-button" disabled={busy} onClick={closeScan}>
                Cancel
              </button>
              <button type="button" className="toolbar-button" disabled={busy} onClick={() => void startScan()}>
                Start scan
              </button>
            </div>
          </div>
        </div>
      )}
```

- [ ] **Step 5: Add the modal styles**

In `IDHANWeb/src/theme/global.css`, after the `.cluster-actions { … }` rule (~line 1138), add:

```css
.scan-modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.55);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}
.scan-modal {
  width: min(440px, 92vw);
  background: #1b1e24;
  border: 1px solid #2a2e37;
  border-radius: 8px;
  padding: 1rem 1.25rem;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.5);
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}
.scan-modal-title {
  font-weight: 600;
  font-size: 1.05rem;
}
.scan-modal-preset {
  align-self: flex-start;
}
.scan-modal-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 0.4rem 1rem;
}
.scan-modal-advanced {
  border-top: 1px solid #2a2e37;
  padding-top: 0.6rem;
  display: flex;
  flex-direction: column;
  gap: 0.4rem;
}
.scan-modal-actions {
  display: flex;
  justify-content: flex-end;
  gap: 0.5rem;
  margin-top: 0.25rem;
}
```

- [ ] **Step 6: Typecheck and re-run the unit tests**

Run: `pnpm --dir IDHANWeb typecheck && pnpm --dir IDHANWeb test -- ClusterManagerPanel`
Expected: no type errors; the Task 1 tests still PASS.

- [ ] **Step 7: Manual verification** *(ask the maintainer to run — do not self-run the dev server)*

Ask the user to:
1. Run `pnpm --dir IDHANWeb dev` and open the Cluster Manager panel.
2. Click **Scan** on a cluster → modal opens with mime + metadata checked, everything else unchecked.
3. Click **Fast scan (hash only)** → Scan mime, Scan metadata, and Adopt orphans all become unchecked.
4. Click **Start scan** → devtools Network shows `POST /clusters/{id}/scan?scan_mime=false&…&scan_metadata=false&…`, and a "Scan started" toast appears; modal closes.
5. Escape / overlay click / Cancel all close the modal without firing a request.

- [ ] **Step 8: Commit** *(only if the user has asked you to commit — see Global Constraints)*

```bash
git add IDHANWeb/src/panels/builtins/ClusterManagerPanel.tsx IDHANWeb/src/theme/global.css
git commit -m "feat(webui): scan options modal with fast-scan preset in Cluster Manager"
```

---

## Self-Review

**Spec coverage:**
- "Let the user set scan parameters from the UI" → Task 2 modal with a checkbox per param.
- "Fast scan = hash-only, UI preset only, no backend mode" → Task 1 `fastScanPreset` (clears mime/metadata/adopt_orphans); Task 2 preset button.
- "Keep query-param transport, no backend changes" → Task 1 `buildScanQuery` + Task 2 `POST …?<qs>`; no server files touched.
- "adopt_orphans coupling" → encoded in `fastScanPreset` + tested in Task 1 Step 1.
- "Modal dialog UI shape" → Task 2 overlay + centered card, Escape/overlay/Cancel close.
- "Advanced row for remove_missing_files + force read-only" → Task 2 Step 4.
- "Verification is build + manual, run by user" → Task 2 Step 7.

**Placeholder scan:** No TBD/TODO; all code blocks are complete; no "similar to Task N" references.

**Type consistency:** `ScanParams` field names are identical across the type, `DEFAULT_SCAN_PARAMS`, `fastScanPreset`, `buildScanQuery`, tests, and the `scanCheck('field', …)` calls, and each matches a server query key. `openScan`/`closeScan`/`startScan`/`scanCheck` names are used consistently between Task 2 steps.
