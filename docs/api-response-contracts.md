# Response fields and job results

Response schemas describe the current server's emitted fields. A required field
must be present even if its value is empty or zero. An optional field may be
omitted; this does not imply that `null` is valid. Additional response properties
remain allowed for forward compatibility.

## Field presence

| Response                  | Always present                                                                                                                      | Conditional fields                                                                                                                                                                                                                                          |
|---------------------------|-------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Tag creation item         | `tag_id`                                                                                                                            | None                                                                                                                                                                                                                                                        |
| Extended tag information  | `tag_id`, `namespace`, `subtag`, `storage_count`, `display_count`, `domain_counts`; `namespace.id`, `namespace.text`, `subtag.text` | Each selected domain has a count row, including zero usage. Top-level counts are independent maxima over that selection; an empty namespace is an empty string                                                                                              |
| Embedding model list item | `model_id`, `model_name`, `dimensions`, `available`, `supports_text`                                                                | `embedding_estimate` only when PostgreSQL supplies a nonnegative table estimate; unavailable models still include both capability flags                                                                                                                     |
| Saved layout list item    | `id`, `name`, `schema`, `created_at`, `updated_at`                                                                                  | None; timestamps are Unix seconds                                                                                                                                                                                                                           |
| POST search result        | `count`, `truncated`, `query_ms`                                                                                                    | `record_ids` when IDs are requested, `hashes` when hashes are requested; at least one is present, and both may be present. `stats` depends on debug mode and available diagnostics; a diagnostic's `micros` is omitted when no positive timing was recorded |
| Job envelope              | `job_id`, `status`                                                                                                                  | Known jobs additionally require `job_name`, `location`, `completed`; failed jobs require `error`. `start_time` and `response` remain optional                                                                                                               |

Empty list responses have no item objects to validate. A search with no matches
still includes the requested result arrays and its three summary fields.
The default search response includes IDs; requesting only hashes need not emit
`record_ids`. Optional estimates and diagnostics are not converted to nulls.

Known job states are `running`, `completed`, and `failed`. `completed` is a
completion flag, so it is also true for failed jobs. A queued job can be reported
as running before its execution start time exists. Once execution begins,
`start_time` is an approximate Unix-second timestamp calculated from the job's
monotonic start time and the current wall clock.

The serializer also supports a minimal `not_found` placeholder containing only
`job_id` and `status`. The single-job HTTP endpoint currently returns a plain-text
404 for an unknown ID. Its successful responses concern known jobs. The shared
schema preserves the placeholder without requiring known-job fields in it.
JSON Schema conditionals express these state-dependent requirements; clients
whose schema tooling does not interpret conditionals should apply these rules
when consuming a job envelope.

## Generic job results

`JobStatus.response` deliberately accepts any JSON value: object, array, string,
number, boolean, or null. It is omitted when the job has not supplied a result.
Explicit null and omission are different. A result can be published before
completion, so clients must inspect `status` or `completed` separately.

There is no stable job-kind discriminator. `job_name` is descriptive text and
must not be used to infer a result type. The generic schema therefore does not
claim that every result matches one of the known producer schemas below.
Consumers initiating a known operation can use that operation's result contract
while retaining generic handling for other jobs.

| Producer                           | Result contract                                               | Interpretation                                                                                                                                                           |
|------------------------------------|---------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `POST /clusters/{cluster_id}/scan` | [ClusterScanResult](api/schemas/ClusterScanResult.yaml)       | Scan totals, cluster ID, and up to 1000 failure details. `failed_count` can include missing files and enumeration errors; scanned totals are not database usage counters |
| `POST /jobs/metadata/rescan`       | [RescanMetadataResult](api/schemas/RescanMetadataResult.yaml) | Successfully parsed record count, failed record count, and per-record failure details                                                                                    |

For example, a completed metadata rescan can report an individual record failure:

```json
{
  "job_id": 42,
  "job_name": "Rescanning record metadata",
  "location": "IDHANServer/src/httpapi/jobs.cpp:250:20",
  "status": "completed",
  "completed": true,
  "start_time": 1792454400,
  "response": {
    "scanned_count": 0,
    "failed_count": 1,
    "failures": [{"record_id": 7, "error": "No parser found for mime id 1"}]
  }
}
```

The job completed its loop even though one record could not be parsed. A job
with an arbitrary result such as `"response": [1, "custom", null]` also conforms
to the generic envelope, without conforming to either known producer schema.
An operation failing before it publishes a result need not have `response` at
all. Source location strings and job display names are diagnostic values, not
stable identifiers.

Native WebUI and Qt clients currently use handwritten API types; they are not
generated from these schemas. Third-party generators may represent arbitrary
results as unknown or a general JSON value. They should not narrow the result
to a union of the two documented producer shapes. Required-field guarantees
describe the current server and are not a claim about older server versions.
