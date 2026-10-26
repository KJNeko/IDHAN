# Unified server event stream

`GET /events` provides one authenticated SSE connection for jobs, download
sessions, downloader diagnostics, and rate limits. Omit `types` to receive all
event types. Otherwise pass a comma-separated list of strings:

```text
/events?types=job.status,downloader.rate_limit
```

`downloader.*` matches every type beginning with `downloader.`. Current event
types are `job.status`, `downloader.session`, `downloader.rate_limit`, and
`downloader.debug`. Type names are strings and may grow as new producers are
added.

## Frame format

`event:` is the standard SSE event name and describes the frame lifecycle:
`snapshot`, `update`, or `shutdown`. Every application frame also has a custom
`type:` field naming its category and a custom `timestamp:` field containing
Unix seconds. `data:` contains JSON text. Standard `EventSource` ignores the
custom fields; IDHAN's fetch based client reads `type:` to route and filter
events.

```text
event: update
timestamp: 1793030402
type: job.status
data: {"job_id":42,"status":"running","completed":false}

```

An opening subscription sends the selected types' current state, then a
`stream.snapshot.complete` snapshot frame with `{}`. Clients can use that
marker to apply a complete snapshot before processing later updates. A
reconnection sends fresh snapshots; there is no `id:` field, durable event
history, or `Last-Event-ID` replay.

Current snapshot payloads are:

- `job.status`: one complete JobStatus object per job. Updates replace that
  job; `{job_id, removed: true}` removes a retained job.
- `downloader.session`: `{id}` for each existing or changed session. The client
  fetches its session and URL data through REST; `{id, removed: true}` removes it.
- `downloader.rate_limit`: one complete lane state per scheduling key.
- `downloader.debug`: the complete diagnostic snapshot, including session
  event history, lanes, and module activity.

The stream begins with `retry: 3000`. `: keepalive` comments keep idle streams
open about every ten seconds. Shutdown sends an empty `shutdown` event and ends
the response. The server negotiates gzip or zstd, flushing after each event.

## Coalescing and history

Pending state updates coalesce by type and entity. If a client is slow and a job
changes several times before the queue drains, it receives the newest job
status; intermediate states may be skipped. The same applies to session and
rate-limit state. This keeps queued data bounded around current state rather
than accumulating obsolete copies.

Downloader debug uses coalesced invalidations. When one is read, the server
renders the latest diagnostic snapshot and retained session event rings, so
coalescing invalidations does not discard each history entry. The event ring
reports any entries it evicted before the snapshot was read.

The WebUI combines all active watcher type filters and maintains one `/events`
connection. When the set of requested types changes, it reconnects once with
the new union and replaces local state from the fresh snapshots.
