# Debug panel: sort profiler

## Purpose

The server recently grew a batch of new search sort types (mime, hash, random,
duration, framerate, has_audio, width, height, ratio, num_pixels, num_tags,
plus the pre-existing ones) along with backing indexes. There's no quick way
to sanity-check the relative query cost of each sort type from the WebUI. This
adds a new **Debug** panel — the first of what will grow into a general
developer-diagnostics panel — whose first tool is a sort profiler: run a blank
search (no tags) once per sort type/direction, one after another, and show
timing for each.

## Scope (v1)

- New panel type `debug`, title "Debug", singleton (matches `ServerStatusPanel`
  / `DatabaseStatsPanel` convention — one instance makes sense for a
  diagnostics panel).
- Single section for now: "Sort Profiler". No collapsible-section
  infrastructure yet — add it only when a second tool actually needs it
  (YAGNI); this keeps the section as a plain block.
- `SORT_OPTIONS` (currently a private const in `SearchPanel.tsx`) is exported
  and reused here instead of duplicated, so the two panels can't drift.

## Behavior

- Manual trigger only: a **Run all** button. No auto-run on mount — this
  hits the DB up to 30 times per click, so it shouldn't fire just from
  opening the panel.
- For each of the 15 sort types × 2 directions (`asc`, `desc`) = 30 runs,
  sequentially (never concurrently — the point is clean, uncontended timing
  per query):
  - POST `/search` with only `{ sort: { by, order } }` — no tags, no
    tag_ids/domains, no limit/offset (full ordered id set, matching the
    grid's default blank search).
  - Issued via `host.http.fetch` directly (not `host.search.run` /
    `api.search`) so the panel can wrap its own `performance.now()` timer
    around the fetch and inspect the raw response text — `host.search.run`'s
    shared client only returns the parsed `SearchResponse`, hiding the
    transfer-time and payload-size signals we want here.
  - Per row, capture:
    - `queryMs` — server-reported `SearchResponse.query_ms` (the actual sort
      query cost — the thing being profiled).
    - `roundTripMs` — client wall time from just before `fetch` to just after
      `response.text()` resolves (network + transfer + read).
    - `overheadMs` = `roundTripMs - queryMs` — everything outside the SQL
      query itself: network + response transfer. The timer stops at
      `response.text()`; JSON.parse happens afterward and isn't included, so
      this is transfer overhead specifically, not parse cost.
    - `bytes` — decoded response body size (`TextEncoder`-measured length of
      the text), i.e. "is this slow because the payload is just huge."
    - `count` / `truncated` — from the parsed body, for context.
  - Progress indicator: "`<sort label> · <order>` (`n`/30)" while running.
  - Each completed row is appended to a live list; a **Stop** button aborts
    the in-flight request (via `AbortController`) and halts the queue — rows
    already captured stay on screen.
  - Unmounting mid-run aborts the same way (cleanup effect).
- Rendering: rows are kept in run order internally but displayed sorted by
  `queryMs` descending (slowest first) — recomputed on every render via a
  plain sort, not re-inserted into state, so it can re-settle live as more
  rows land without fighting React state.
- No persistence — results reset every time the panel remounts (confirmed
  with the user: this is an actively-run debug tool, not a saved dashboard).
- Errors: a failed request (network/5xx) surfaces via `host.ui.toast` and
  stops the run (partial results stay visible); an aborted request
  (Stop / unmount) is swallowed silently.

## Non-goals for v1

- No per-row re-run button.
- No repeated runs / min-avg-max per sort type (single run per sort ×
  direction, per the "run one after another" instruction taken literally).
- No charting — a plain table is enough to eyeball outliers; the `dataviz`
  skill's chart guidance isn't warranted for 30 rows of numbers.
- No wiring into any other panel category/grouping mechanism — the panel
  registry is a flat list today (`LayoutToolbar`'s panel `<select>`), so
  "Debug" just becomes another entry in it.

## UI sketch

```
[ Run all ]  [ Stop ]      Running: num_pixels · asc (14/30)

Sort          Order   Query ms   Round trip   Overhead   Size     Count
num_tags      desc    842        861.2        19.2       1.2 MB   40213
hash          asc     790        805.4        15.4       1.2 MB   40213
...
```

## Files touched

- `IDHANWeb/src/panels/builtins/SearchPanel.tsx` — export `SORT_OPTIONS`.
- `IDHANWeb/src/panels/builtins/DebugPanel.tsx` — new panel.
- `IDHANWeb/src/panels/builtins/index.ts` — register it.
- `IDHANWeb/src/theme/global.css` — new `.debug-*` rules, following the
  `.dbstat-legend-row` grid-row pattern (no new charting/table library).
- A `DebugPanel.test.ts` for the pure helper(s) — at minimum the
  slowest-first sort — mirroring `DatabaseStatsPanel.test.ts`'s convention of
  testing pure logic extracted from the component.
