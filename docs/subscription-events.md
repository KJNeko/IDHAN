# Subscription events

The authenticated `/events?filter=subscriptions` stream shares the existing SSE
connection with `downloader` and `job`. Events use `event: subscriptions` and a
JSON `type` field. `session_ids` filters download sessions only.

Every connection receives a complete baseline:

```json
{"type":"snapshot","groups":[...],"items":[...]}
```

Subsequent messages contain only changed fields and affected entities:

```json
{"type":"patch","items":[{"id":42,"imported_count":18,"last_error":null}]}
```

Merge objects by ID. Omitted fields retain their previous values; explicit nulls
clear nullable fields. A map such as `parameters` replaces the entire field.
Counters are absolute values, making repeated patches harmless. New entities
include all their fields. Empty `groups` and `items` sections are omitted.

Deletions contain IDs only, including subscription IDs removed with a group:

```json
{"type":"remove","groups":[7],"items":[42,43]}
```

Commands publish after their database changes succeed. Persisted imports (including
known files) and URL removals queue count updates independently of the scheduler.
A worker batches affected sessions over 100 ms, refreshes only their groups, and
publishes changes to file counts and total sizes. The final event is flushed even
if no more files arrive; ongoing downloads never postpone the batch deadline.
Scheduler checks reconcile run state, completion, recovery and missed changes at
the existing check interval (five seconds). Unchanged state emits no subscription
messages. Each
reconciliation batches its changes into one patch plus a removal when needed.
Partial patches are neither retained snapshots nor replaceable queued events.

Initialization and publication use the same coroutine mutex. The baseline
replaces queued subscription updates through its checkpoint, preserving newer
updates and other streams. Reconnect replaces local state with a new baseline;
there is no event-history replay. Existing heartbeat and queue limits apply.

The WebUI maintains one subscription store per tab on the shared authenticated
SSE connection. Panels do not poll or refresh lists after commands. Parser
choices load separately on mount.
