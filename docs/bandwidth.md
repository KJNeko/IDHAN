# Bandwidth accounting

`GET /bandwidth?start=1735689600&end=1735776000` returns every host's totals
for the half-open interval `[start, end)`. Both bounds are optional Unix timestamps
in seconds; omitting both returns all recorded history. Invalid integers and
`start >= end` return HTTP 400. There is no host filter.

Each response node contains `host`, `bytes_received`, and `children`. Counters belong exclusively to the exact hostname.
Neither
parents nor children inherit or aggregate one another's counters. Intermediate
nodes created for display have zero counters. DNS names are nested by suffix,
stopping before the top-level label; this is not a public-suffix classification.
IP addresses remain single nodes. Hosts are normalized and exclude ports, URL
paths, and query strings.

The downloader records each completed transfer attempt, including redirect hops,
retries, HTTP errors, transport errors, cancellations, and shutdown interruptions.
Each log's timestamp is its completion time. Incoming bytes include the encoded
response body and HTTP headers reported by curl. These HTTP transfer measurements
exclude TLS, TCP/IP overhead, and retransmissions.
In-progress requests appear after completion and the next successful flush.

Transfer threads only append to a synchronized in-memory queue. A background job
flushes at five-second intervals using batches of at most 4096 rows. Each batch
resolves distinct hosts and inserts its logs in one transaction. `bandwidth_hosts`
maps each unique hostname to an integer ID. Log rows contain only `host_id`,
`timestamp`, and `bytes_received`; the API joins the host mapping when reading.
Rows for the same host and Unix second are combined by summing received bytes,
including measurements arriving in later flushes. Individual request counts
are not retained.
Failed batches remain queued for retry. If a commit succeeds but its acknowledgement
is lost, retrying can count that batch twice. Normal shutdown stops the downloader
before draining the queue. A
process crash, or a database outage that persists through shutdown, can lose
unflushed measurements. A prolonged database outage grows the in-memory queue.

Migrations 445–448 create the log table, add the host mapping, and convert existing
logs to host IDs and per-second totals through the normal migration process.
