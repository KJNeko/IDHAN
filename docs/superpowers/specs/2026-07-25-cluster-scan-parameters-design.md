# Cluster Manager scan parameters + Fast-scan preset

**Date:** 2026-07-25
**Status:** Approved
**Scope:** Frontend-only (`IDHANWeb/`). No backend changes.

## Problem

The `/clusters/{cluster_id}/scan` endpoint already accepts a full set of scan
parameters (`scan_mime`, `rescan_mime`, `scan_metadata`, `rescan_metadata`,
`verify_hash`, `adopt_orphans`, `fix_extensions`, `stop_on_fail`,
`remove_missing_files`, and `readonly` force-override), parsed from the **query
string** in `IDHANServer/src/api/cluster/scan.cpp` (`extractScanParams`). But the
Cluster Manager WebUI panel fires a bare `POST /clusters/{id}/scan` with no
params, so none of that is reachable from the UI. There is also no quick way to
run a lightweight "just detect the file exists by hash" scan.

## Goal

1. Let the user set scan parameters from the Cluster Manager UI.
2. Provide a **Fast scan** option that detects only the basics of a file (hash),
   ignoring mime and metadata.

## Non-goals

- No new backend scan "mode". Fast scan is purely a **UI preset** over the
  existing parameters.
- No changes to `scan.cpp` or any server code.
- No changes to the scan pipeline's step ordering or behavior.

## Backend facts relied upon (unchanged)

- `extractScanParams` reads each param via
  `request->getOptionalParameter<bool>("name")`, which reads query/form params.
- Drogon's `fromString<bool>` accepts case-insensitive `"true"`/`"false"` and
  digit strings; `getOptionalParameter` catches parse errors and returns
  `nullopt`, so an unrecognized value falls back to the `.value_or(default)`.
- Coupling inside `extractScanParams`:
  ```cpp
  p.scan_metadata |= p.adopt_orphans; // orphans need metadata
  p.scan_mime     |= p.scan_metadata; // metadata needs mime
  ```
  Therefore a hash-only preset must set `adopt_orphans=false` in addition to
  `scan_mime=false` and `scan_metadata=false`; otherwise mime+metadata get
  silently re-enabled.
- Defaults in `ScanParams`: `scan_mime=true`, `scan_metadata=true`, everything
  else `false`. The UI form defaults mirror these.

## Design

### Component changes — `ClusterManagerPanel.tsx`

Add a scan-options **modal dialog** scoped to a single cluster.

New panel state:
- `scanTarget: Cluster | null` — the cluster whose Scan button was clicked; the
  modal is open iff this is non-null.
- `scanParams: ScanParams` — the form state (a flat object of booleans), reset to
  defaults each time the modal opens.

Type (frontend-local):
```ts
interface ScanParams {
  scan_mime: boolean;        // default true
  rescan_mime: boolean;      // default false
  scan_metadata: boolean;    // default true
  rescan_metadata: boolean;  // default false
  verify_hash: boolean;      // default false
  adopt_orphans: boolean;    // default false
  fix_extensions: boolean;   // default false
  stop_on_fail: boolean;     // default false
  remove_missing_files: boolean; // default false
  readonly: boolean;         // default false (force read-only override)
}
```

Flow:
- The card's **Scan** button now calls `openScan(c)` → sets `scanTarget = c` and
  `scanParams = DEFAULT_SCAN_PARAMS`.
- The modal renders:
  - Title: `Scan cluster "<name>"`.
  - A prominent **Fast scan (hash only)** button. Clicking it applies the
    preset to the form: `{ ...current, scan_mime: false, scan_metadata: false,
    adopt_orphans: false }`. It does **not** auto-start — the user sees the
    resulting checkbox state and confirms with Start scan.
  - A grid of checkboxes bound to each `scanParams` field. Primary group
    (matching the approved mockup): Scan mime, Rescan mime, Scan metadata,
    Rescan metadata, Verify hash, Adopt orphans, Fix extensions, Stop on fail.
    An "Advanced" row adds: Remove missing files, Force read-only.
  - **Cancel** (closes, no action) and **Start scan** (runs, then closes).
- **Start scan** builds a query string from every param
  (`scan_mime=true&scan_metadata=false&…`) and calls
  `POST /clusters/{id}/scan?<qs>`, reusing the existing job-id toast flow and
  `busy` state. On success: toast `Scan started for "<name>".`, close modal.
- Closing: Cancel, overlay click, or Escape key all set `scanTarget = null`.

The existing standalone `scan(c)` function is replaced by `openScan(c)` +
`startScan()`.

### Styling — `global.css`

Add classes near the existing `.cluster-*` block (~line 1079):
- `.scan-modal-overlay` — fixed full-viewport dimmed backdrop, centers content,
  high `z-index`.
- `.scan-modal` — centered card (max-width ~420px), padding, border/radius
  consistent with existing panels.
- `.scan-modal-title`, `.scan-modal-grid` (2-col grid of `.log-check`),
  `.scan-modal-preset` (the Fast scan button), `.scan-modal-actions`
  (right-aligned button row), `.scan-modal-advanced` (subtle separator + label).

Reuse existing `.toolbar-button`, `.toolbar-button.danger`, `.log-check`.

## Data flow

```
Scan button ──▶ openScan(c) ──▶ modal (scanTarget=c, scanParams=defaults)
                                   │
                Fast scan ─────────┤ mutate scanParams → hash-only preset
                                   │
                Start scan ────────▶ qs = encode(scanParams)
                                     POST /clusters/{id}/scan?qs
                                     toast job started; close modal
```

## Error handling

- Same as today: non-OK response → error toast; network throw → error toast;
  `busy` guards against double-submit; `finally` clears `busy`.
- Modal cannot be opened for a second cluster while `busy` (buttons disabled).

## Testing / verification

The WebUI has no panel-level test harness. Verification is:
1. `pnpm --dir IDHANWeb build` (or the CMake `BUILD_IDHAN_WEB` path) compiles
   with no TypeScript errors.
2. Manual UI check: Scan opens modal; Fast scan unchecks mime/metadata/adopt;
   Start scan issues the request with the expected query string (observable in
   devtools network tab) and shows the job-started toast.

Per the maintainer's standing preference, the build/manual steps are run by the
user, not by the assistant.
