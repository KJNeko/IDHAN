# How to use

Start the testing framework using docker-compose-test.yml

Run the python. Needs httpx

## Tag benchmarks

Install `tests/api/requirements.txt` into your configured Python interpreter.
The `pytest-benchmark` plugin measures everyday tag workflows using existing
functional assertions: reads, creation, duplicate creation, exact search,
normalization, batches, concurrent creation, autocomplete, and parent/alias
creation and removal. Invalid inputs, rollback, cycle rejection, domain isolation,
and disabled endpoints are covered by functional tests instead of benchmarks. Normal API test runs skip benchmarks.

In CLion, run **Tests / Tag Benchmarks**. Its before-launch step starts Compose
and waits for healthy services, just like **API Tests**.

From the command line, with the test server running:

```sh
python -m pytest tests/api/benchmarks --run-benchmarks --benchmark-autosave
python -m pytest tests/api/benchmarks --run-benchmarks --benchmark-compare
```

There is one warm-up round followed by 10 measured rounds per scenario. Use
`--api-benchmark-rounds=50` for more samples, or `-k autocomplete` to select cases.
Results show latency distributions for each workflow. Saved results live in
`.benchmarks/` and are ignored by Git. Comparisons report changes; they do not
fail tests on a timing threshold by default.

Each round receives fresh fixture data. Fixture tag/domain creation and domain
cleanup happen outside the timer. The measured workflow includes HTTP client
work, server/database time, response decoding, assertions, and multiple requests
where the functional test uses them. The concurrency scenario also includes
thread-pool startup and synchronization. OPS therefore means workflows per
second, not individual requests per second or concurrent server throughput.

Tags have no deletion API, so rounds leave unused tags until the disposable
stack is restarted. Database size and caches can affect later results. Compare
runs using the same rounds, server build, hardware, and initial dataset; restart
the entire stack before a fresh baseline. These scenarios use small datasets and
do not yet characterize performance at production scale. Compose currently
builds Debug: use a Release server for representative performance measurements.

Benchmark times are displayed in milliseconds by default. Override with
`--benchmark-time-unit=s` to display seconds.

Batch creation benchmarks cover **1, 10, 100, 1,000, and 10,000 new tags per
request**. Run only these with:

```sh
python -m pytest tests/api/benchmarks --run-benchmarks -k create_tag_batches --benchmark-autosave
```

Each round generates a fresh namespace and payload outside the timer. Only the
HTTP request (including JSON encoding and response transfer) is timed. Response
status, result count, and unique positive IDs are checked outside the timer.
The grouped table shows milliseconds per batch; divide the batch size by mean
seconds for tags/second. Batch size is also stored in benchmark result metadata.
At the default rounds this adds 122,221 tags including warm-up, so reset the
whole disposable stack before comparing fresh runs.

## API-wide coverage

Run all functional tests with `python -m pytest tests/api`. Add `--run-benchmarks`
to include performance cases, or run **Tests / API Benchmarks** in CLion to
benchmark every supported area. **Tag Benchmarks** selects the tag benchmark file.
`--api-benchmark-rounds=10` sets rounds for all benchmarks; the earlier
`--tag-benchmark-rounds` spelling remains an alias.

| Area                 | Current functional coverage                                                                                                                                   |
|----------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------|
| System               | Health, authentication verification in the auth-disabled test stack, bandwidth, database stats, MIME registry, job listing/status, metadata-rescan completion |
| Contract             | OpenAPI local references, operation IDs, response declarations, required path parameters, invalid IDs and malformed JSON                                      |
| Records              | Single/batch creation, duplicate hashes, hash lookup, invalid hashes, notes, URLs                                                                             |
| Files                | Unique generated PNG import, duplicate import, exact download bytes/SHA256, metadata/info, WebP thumbnails, fixture file deletion                             |
| Tags                 | Creation/normalization/batches, lookup, autocomplete, concurrency, parent/alias relationships and cycle rejection                                             |
| Record tags          | Add/remove, stored versus resolved tags, inheritance/aliases, domain filtering, eventual usage counts                                                         |
| Search               | GET/POST tag/domain filters, IDs/hashes, offset, negative pagination validation                                                                               |
| Record relationships | Duplicate, alternative and unrelated creation, reciprocal lookup and clearing                                                                                 |
| Clusters             | Writable disposable cluster creation, listing, info, name/ratio modification, fixture removal                                                                 |
| Hydrus               | Version/instance identity, services, client info, pages, popups, options                                                                                      |
| Downloader           | Configuration/registry reads, empty session creation/list/detail/errors/records/deletion                                                                      |
| Subscriptions        | Disabled group/subscription creation, listing, updates and deletion                                                                                           |

The tests need a disposable server: they create records and tags and register a
writable cluster beneath `/media`. Hash-only records/tags cannot be deleted via
the API and remain until a full stack restart. Imported files, temporary domains,
subscription groups/subscriptions, and sessions are cleaned up by fixtures.
File tests generate a tiny valid PNG with fresh pixels using Python's standard
library; no external sample assets or network downloads are needed.
Tag usage counts are accumulated by a background job every 60 seconds. The count
test can take around two minutes, polling for each expected update with a
75-second deadline. Benchmark timings exclude these background-maintenance waits.

The additional benchmark groups measure read endpoints, record batch creation (1/10/100/1,000 records), reads of
imported records, GET/POST search, and creation
of empty download sessions and disabled subscription groups. Request generation
and validation are outside their timers. These are small-fixture HTTP benchmarks;
large search corpora, large file transfers, and sustained concurrent load still
need dedicated datasets.

Coverage is expanding; this is not complete behavioral coverage of every route.
Still needed: auth-enabled permissions, full Hydrus compatibility, job failure
and cancellation, streamed events, rate limiting under load, parser upload,
actual downloader runs against a local HTTP fixture, embedding models/search,
thumbnail variants, maintenance workflows, and broader metadata/media formats.
The OpenAPI checks inspect the entire native contract but do not replace those
endpoint-specific tests.

## Local services only

API tests and benchmarks must target the disposable local test stack. `--api-url`
and `IDHAN_TEST_API_URL` accept only HTTP loopback origins (`127.0.0.1`, `::1`,
or `localhost`), without credentials, paths, queries, or fragments. The session
network guard also blocks non-loopback DNS, TCP connections, and UDP sends from
Python, including attempts made by clients other than the shared fixture.
The API client ignores proxy environment variables and does not follow redirects.

The Compose runtime network is `internal`, preventing the server and downloader
from accessing external services. Fixed-destination TCP relays publish only the
local API and PostgreSQL ports; the application containers have no external
network attachment. Image builds and initial image pulls may download dependencies.
Subscriptions are created disabled; URL-association tests store loopback URLs and
do not fetch them. Future downloader tests must run local fake upstream services
on the isolated Compose network, rather than using real websites. These controls
do not identify other applications listening on loopback: use the dedicated test
stack, not a development or production server exposed locally.
