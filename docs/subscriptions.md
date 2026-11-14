# Subscriptions

A subscription stores generator parameters, whether it is enabled, its next check time,
and its last outcome. A group supplies the generator, origin and check interval.
Each subscription has one reusable downloader session, created on its first check.

The scheduler runs on a 30-second timer. On each tick it:

1. Records reported outcomes for active subscription sessions and schedules their next checks.
2. Visits every enabled group. A group with an active item is skipped.
3. Selects one enabled item whose next check time has passed, ordered by explicit run
   requests, next check time, then ID. It joins the saved downloader session ID.
4. Activates the item and retries its saved downloader session.

Different groups can run concurrently. Each group admits at most one item per tick
and has at most one active item. Run-now makes an item due for the next timer tick;
it obeys the same group admission rule. Pausing prevents new checks and lets active
work finish. Both success and failure schedule the next check using the group interval.

The subscription code never reads URL trees. Downloader sessions own request creation,
retrying, URL states, import accounting and completion. A retry refreshes the saved
request tree; the session's imported-record membership remains available. Legacy
subscription sessions with several historical roots reuse their latest root.

Every downloader session reports its result through its existing observer completion
callback after parser/import work has drained. Results are stored in
`download_session_results` and exposed by `GET /download_sessions/{session_id}/result`.
Before a result is available this endpoint returns JSON null. Results include:

- `imported_count`: distinct records newly imported in the current request tree.
- `known_count`: distinct records already present, excluding any counted as newly imported.
- `error`: a downloader failure message, or null on success.
- `finished_at`: completion time in Unix seconds.

Regular downloader sessions also publish a `session_result` event. The whole session
can be retried through `POST /download_sessions/{session_id}/retry`. New work clears
the previous completed result. Subscriptions use the same downloader result and retry
operations; they do not calculate file statistics themselves.

On restart, completed results remain available for reconciliation. Interrupted checks
are marked interrupted and made due again, using the same saved session. Configuration
and imported-record membership are preserved. Dead-subscription detection, source-date
policy and subscription-level file/size statistics are outside this core system.

Migration 471 creates the results table through the normal migration process. Existing
subscription configuration and downloader sessions require no destructive conversion.

Session import counts
---------------------

Every session exposes `total_imported_count` and `last_scan_imported_count` in
its detail/list responses and SSE snapshot. The total advances as new files are
imported, once per distinct record in that session. Already-present files do not
increase it. It survives retries and deletion of URL history. The last-scan count
is replaced when a scan finishes, including partial imports from failed scans;
it remains visible while the next scan runs and is null before the first finish.
Completion results also include the total. Live `session_import_counts` events
update the total during a scan. The Downloads panel displays both values.

Migrations 472 and 473 add the import marker and counters. Existing sessions are
backfilled from retained URL import rows and completion results. Imports whose
URL history was already discarded cannot be reconstructed.
