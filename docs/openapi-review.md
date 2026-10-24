# OpenAPI contract review — 2026-10-18

Reviewed the native API contract in the current working tree, including the existing staged and unstaged documentation
changes. Finding IDs are retained from the original review so existing references remain stable; removed findings are
outside the current scope.

## Result

The original review identified 21 native API contract findings in addition to F01 and F02. After the approved fixes
through 2026-10-20, none of those findings remain open; F01 remains deferred, F02 and twenty approved findings are
resolved, and F12 is closed as intended behavior. Two observations about the upstream-provided Pawchive specification
are recorded as outside our change scope (E01–E02). Five smaller follow-up gaps are expanded under G01–G05 (all
resolved), and three verification issues are recorded under V01–V03. P1 means advertised functionality is unavailable or
a documented request cannot perform its intended operation; P2 means another material contract mismatch or omission; P3
means a smaller descriptive inconsistency. These identify disagreements with current code, not a requirement to preserve
incorrect implementation behavior. Some should be resolved by fixing the implementation instead of weakening the
contract.

## Validation and scope

- `python3 tools/openapi/validate.py` passed: OpenAPI 3.2.1 official schema, local references, examples, and **101
  operations** validated. Coverage comprises **91 paths and 35 shared schemas**, including two explicitly documented
  HEAD operations. No missing or unregistered native API method/path pairs were reported.
- Compared route registrations, authentication, parameter/body parsing, serializers, error branches, shared file/stream
  responses, search fetches, and relevant relationship/database helpers and migrations. Reviewed all functional groups
  listed in the inventory below.
- Initial JSON Schema witnesses showed that actual layout integer timestamps failed the original schema, while
  unsupported numeric tag subtags passed. F05 now corrects the timestamp mismatch, and F06 now rejects unsupported
  numeric subtags in the schema. Also checked the opposing empty-array constraints for tag creation and bulk tagging.
- The initial audit was static. Subsequent F02 verification rebuilt the server and exercised API requests against a
  disposable PostgreSQL database: the four new storage-search cases passed 68 assertions; the focused storage,
  embedded-metadata, and mapping-resolution regression set passed 98 assertions in six cases. A broader run exposed the
  separate perceptual-hash operator issue recorded under V01 below. No browser or external-service behavior was tested.
- Approved-fix verification on 2026-10-19: the changed server translation units compiled and linked into a separate
  verification binary, reusing existing dependency artifacts without regenerating CLion's build directory. The focused
  API set passed **196 assertions in 11 cases**, including five new review cases; purge filesystem tests passed **48
  assertions in three cases**. Valid 9 MiB search, metadata, and layout bodies succeeded; malformed oversized JSON
  returned 400. The known V01 perceptual-hash case was excluded from this focused run.
- F06 verification on 2026-10-19: 43 schema assertions passed across TagSet, TagInput, and the complete-ID/string
  alternatives; the corrected examples and all 101 operations pass the OpenAPI validator. Runtime parsing is unchanged.
- F09 verification on 2026-10-19: 120 autocomplete API assertions, the broader focused API set (316 assertions in 15
  cases), nine WebUI request/cache tests, TypeScript typechecking, Qt autocomplete compilation, and server
  compilation/linking passed. Domain-scoped counts remain estimates, as requested.
- V01 verification on 2026-10-19: the complete focused API set passes **343 assertions in 18 cases**, including
  perceptual-hash searches, similarity requests, and a server restart. Database initialization passes **62 assertions in
  nine cases** under the revised shared-database policy. A fresh server with no schema setting creates application
  tables in `idhan` using administrator-provisioned extensions; a non-superuser application role successfully runs the
  full migration sequence while extension identities/namespaces/versions and unrelated data remain unchanged. Builds and
  tests use separate verification artifacts without regenerating CLion's build directory.
- The separate Pawchive file is supplied by Pawchive and used as input to generate our interface client. The user
  confirmed on 2026-10-20 that this upstream specification is outside our change scope. Its local structure was reviewed
  only as an upstream-input observation; this report does not certify its live endpoints, host, authentication, or
  payload behavior. UI entrypoints and static assets are outside the native API contract.

## Additional approved work: endpoint parameter declarations

Native endpoint registrations now carry explicit query and JSON-field allowlists
from `IDHANServer/src/httpapi/parameterDeclarations.cpp`. Unknown names warn and
remain accepted; the reusable policy also supports a future 400 response. Nested
objects and arrays are checked where declared, while free-form maps remain open.
Authentication query keys and path captures are recognized. See
[the implementation guide](api-parameters.md) for maintenance and policy details.

Verification: rebuilt and linked the native HTTP/server sources separately from
CLion's build directory; 38 parameter-rule assertions in six cases and 693 API
assertions in 25 cases passed. All 101 OpenAPI operations still validate.
This parameter-declaration work does not resolve or change the proposals for F24–F27. F24 and F25 were subsequently
resolved as recorded below.

## Findings

Each open finding has a proposed solution immediately after its evidence. Open proposals remain unimplemented. Resolved
findings record the approved implementation and its verification; their earlier proposals are retained where useful and
explicitly labeled. They distinguish corrections supported by current behavior from changes that need a product
decision. **Questions for later** records the latter without choosing an answer; independent work on the same finding
can proceed. F01 remains deferred and F02 remains resolved.

### F01 [Deferred] Sibling system is intentionally stubbed out

Sibling creation, removal, and resolution are intentionally stubbed out pending a full rewrite and redesign. The
authenticated creation and removal endpoints return plain-text 501 and ignore their request bodies. This is accepted
current behavior and requires no implementation work in this review. The API documentation now identifies these stubs
explicitly.

### F02 [Resolved] Storage-mode search uses directly applied mappings

The selected mode now reaches tag, wildcard, namespace, count, and pagination queries. Storage mode reads
`tag_mappings`; display mode retains the resolved mapping view. Exact and negated terms preserve the selected layer,
domain filters remain in place, and `num_tags` sorting and tag-count predicates use the same mode. Counts remain
distinct-tag counts across all domains, consistent with the existing sort behavior.

Regression coverage in `tests/api/searchStorage.cpp` passes all 68 assertions in four API test cases against the rebuilt
server. This includes aliases, inherited parents, default display mode, domain filters, duplicate suppression, hashes,
and pagination through both materialized results and database fallback. Changes are grouped in the
`F02 - Storage-mode search` changelist.

### F05 [Resolved] Layout list timestamps have the wrong JSON type

`GET /layouts` defines `created_at` and `updated_at` as strings. SQL extracts epoch values as bigint and the serializer
emits JSON integers. Specify integer/int64 Unix seconds. A representative payload with the actual integer types was
rejected by the documented response schema.

**Resolution — 2026-10-19:** Changed layout-list created_at/updated_at to integer/int64 epoch seconds. The new API case
verifies both serialized fields are integral.

Evidence: [docs/api/paths/layouts.yaml:22](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/layouts.yaml:22); [IDHANServer/src/httpapi/layouts.cpp:95](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/layouts.cpp:95); [IDHANServer/src/httpapi/layouts.cpp:117](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/layouts.cpp:117).

**Approved proposal and rationale**

Change both list-response timestamp properties to `type: integer`, `format: int64`, with descriptions explicitly stating
Unix epoch seconds in UTC. Replace string examples with integer examples. The database already extracts epoch values and
the serializer already emits integers, so correcting the schema preserves the existing wire representation and gives
generated clients the right type. A date-time string format would describe a different representation and should not be
attached to these integer fields.

Trace the create, update, and detail responses before reusing a shared layout schema: the list response is a summary and
should not accidentally acquire the full layout document or require fields that another route omits. If the same
timestamp representation occurs elsewhere, factor out that property definition only where its units and nullability
actually match. Do not globally normalize timestamps, because other API groups deliberately use microseconds.

Validate a captured list response containing two integer timestamps against the corrected schema, and verify that the
previous string witness is rejected. Cover an empty list and a layout updated after creation so the examples do not
suggest the two values must always match. Audit whether these fields are always emitted before marking them required as
part of G04. No runtime or database migration is needed for this correction.

### F06 [Resolved] Tag objects require a namespace and textual subtag

Resolved on 2026-10-19. The original TagSet schema accepted numeric subtags and omitted fields even though parseTagPair
rejects them. The corrected contract matches the existing parser; no runtime behavior was changed.

Evidence: [docs/api/schemas/TagSet.yaml:1](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/schemas/TagSet.yaml:1); [docs/api/paths/records/tags/add.yaml:47](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/tags/add.yaml:47); [docs/api/paths/records/tags/addgroup.yaml:66](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/tags/addgroup.yaml:66); [IDHANServer/src/httpapi/records.cpp:174](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/records.cpp:174).

**Implemented solution and rationale**

TagSet now requires both namespace and subtag. The namespace remains text or an existing positive int32 namespace ID;
the subtag is always a string. An empty namespace string still represents an unnamespaced tag. A numeric-looking subtag
such as "5" remains literal text and is distinct from an integer ID. Complete tag IDs and complete tag strings remain
separate alternatives in TagInput.

TagComponent is now described specifically as the namespace component, and its integer alternative defines namespace
bounds directly rather than referencing TagID. This avoids teaching clients that a namespace ID is a complete tag ID.
Only TagSet's namespace uses this component; the subtag has its own string schema. Tag creation has its own text-only
pair contract and was left unchanged. No new restrictions were introduced for empty subtag strings or additional object
properties that the parser currently accepts.

Single-record and per-record bulk examples now use textual subtags. Their descriptions distinguish existing numeric
namespace IDs from complete tag IDs, and the examples include an explicitly unnamespaced numeric-looking textual tag.
This corrects requests that previously passed documentation validation but returned 400 when copied into the API.

Verification checked 20 input shapes against both TagSet and TagInput: valid text/numeric/empty namespaces, literal
numeric strings, missing members, null and boolean values, numeric subtags, namespace bounds, and retained
empty-string/additional-property acceptance. Three complete-ID/string alternatives were also checked, for 43 schema
assertions. These shapes were compared with the current parser implementation; no new end-to-end HTTP run was needed for
this documentation-only correction. The official OpenAPI validator passed all local references, examples, and 101
operations. Changes are grouped in the F06 changelist; this shared review report remains in API documentation.

### F08 [Resolved] Tag relationship alias descriptions point in the opposite direction

For an alias row `aliased_id=10, alias_id=20`, requesting relationships for tag 10 yields `aliases:[20]`, while tag 20
yields `aliased:[10]`. The documentation describes `aliases` as incoming and `aliased` as outgoing, the reverse of these
queries and of the documented alias-create semantics. Swap these descriptions and update the relationship example
accordingly.

**Resolution — 2026-10-19:** Corrected outgoing aliases and incoming aliased descriptions, preserving the wire
properties, and changed the example to tag 10 resolving toward tag 20.

Evidence: [docs/api/paths/tags/relationships/get.yaml:52](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/relationships/get.yaml:52); [IDHANServer/src/httpapi/tags.cpp:154](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/tags.cpp:154); [docs/api/paths/tags/relationships/alias/create.yaml:6](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/relationships/alias/create.yaml:6).

**Approved proposal and rationale**

Correct the property descriptions around a concrete directed relationship: creating `aliased_id=10, alias_id=20` makes
10 resolve toward 20. Fetching relationships for 10 reports 20 in `aliases`; fetching for 20 reports 10 in `aliased`.
Put that example beside the response definitions and make the create operation use the same vocabulary. This anchors the
meaning in an observable request/response pair instead of relying on the ambiguous word “alias.”

Preserve the response property names and database direction. Renaming or swapping the arrays would change existing
clients' behavior and is unnecessary to correct a prose inversion. Clarify whether the arrays contain directly stored
relationships or a transitive result by following the actual relationship queries; do not infer transitive closure from
the search resolution views. State the domain scope so an edge in one domain does not appear to apply everywhere.

Verify with two tags and one edge, fetching both endpoints in the same domain and in an unrelated domain. Also cover a
tag that has incoming and outgoing edges so the arrays cannot appear interchangeable. Check that the examples agree with
the create body, deletion body, and returned arrays. Existing database triggers remain responsible for resolution and
cycle handling; no application-side relationship resolver is needed. The acceptance criterion is a documentation-only
correction with unchanged serialized field names and values.

### F09 [Resolved] Autocomplete uses mode-specific estimated maxima over selected domains

Resolved on 2026-10-19. Storage/display selection is functional, display is the default, and counts use the largest
corresponding per-domain estimate in the requested domain set. No sum, distinct-record union, or new exact-count
infrastructure is used.

Evidence: [docs/api/paths/tags/autocomplete.yaml:38](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/autocomplete.yaml:38); [IDHANServer/src/httpapi/tags.cpp:308](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/tags.cpp:308); [IDHANServer/src/httpapi/tags.cpp:216](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/tags.cpp:216). [IDHANMigration/src/table_tag_counts/035-create.sql:1](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANMigration/src/table_tag_counts/035-create.sql:1); [IDHANWeb/src/api/client.ts:297](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANWeb/src/api/client.ts:297); [IDHANWeb/src/host/autocompleteCache.ts:29](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANWeb/src/host/autocompleteCache.ts:29).

**Implemented solution and rationale**

The existing tag_counts table already stores counters by (tag_id, tag_domain_id), so no migration or counter redesign
was needed. The autocomplete helper now receives the selected mapping mode and domain IDs. Its left join restricts
counter rows to that set before taking MAX of the selected storage_count or display_count. The resulting estimate drives
the returned count, popularity weighting, and include_unused=false filtering consistently. Missing counters become zero;
exact text matches retain first priority, followed by count-weighted similarity, similarity, and a stable tag-ID tie
break.

The canonical tag_domains query parameter uses repeated positive smallint IDs. Domains are deduplicated and checked for
existence: malformed or nonpositive values return 400, and nonexistent requested domains return 404. Omitting selection
preserves the previous all-domain scope, with the same maximum rule. An explicitly empty parameter value is malformed;
clients with an empty domain array omit the parameter. The previous WebUI tag_domain spelling remains a deprecated
single-domain alias, used only when tag_domains is absent. If A has count 120, B 80, and unrequested C 500, selecting A
and B returns 120, while B alone returns 80. Neither duplicates nor unrequested domains inflate the result.

Display is the default to preserve the previously effective mapping mode and align with search. Storage uses directly
applied mapping estimates, while display uses the existing alias/parent-resolved counters. These counters remain
estimates and can include multiple relationship contributions from one record. The documentation explicitly
distinguishes suggestion weights from exact search-result totals. include_unused accepts the HTTP library's boolean
forms, and unused tags retain zero counts when included.

The WebUI client accepts a domain set, mode, and includeUnused option, while retaining its single-domain convenience
option. Cache keys normalize domain sets and include mode, used-only setting, limit, and query so domain/mode changes
cannot reuse another scope's counts. Existing complete-prefix reuse stays within that same scope. The tag editor
forwards its selected domain; the existing tag picker single-domain option now reaches the server. The current search
panel omits domains and therefore retains all-domain suggestions. The Qt client keeps its original autocomplete method
and adds an overload for domains/mode/unused selection; obsolete ignored query options were removed.

Verification: the focused autocomplete API suite passed 120 assertions in four cases, using actual mappings and
alias/parent relationships across four domains. It covers mode/default behavior, maxima, exclusion of larger unrequested
counts, repeated/reordered domains, zero-count domains, used-only filtering, exact-match priority, negative input,
legacy alias precedence, and domain validation. Nine WebUI request/cache tests and the full TypeScript typecheck passed;
the Qt autocomplete translation unit compiled successfully. The changed server and its existing helper call sites
compiled and linked in the separate verification build. The broader focused API regression set passed 316 assertions in
15 cases, excluding the known V01 perceptual-hash issue. All 101 OpenAPI operations validate. Changes are grouped in the
F09 changelist; shared source files retain earlier approved edits and the shared review report remains in API
documentation.

### F10 [Resolved] Perceptual similarity defaults to distance 0

Approved on 2026-10-19: omitting `distance` from `GET /relationships/{record_id}/similar` means a distance of `0`. The
handler already uses that fallback; the documented default of `8` was incorrect.

Evidence: [docs/api/paths/relationships/record_id/similar.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/relationships/record_id/similar.yaml); [IDHANServer/src/httpapi/relationships.cpp:119](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/relationships.cpp:119); [IDHANServer/src/httpapi/helpers.cpp:45](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/helpers.cpp:45).

**Implemented solution and rationale**

Corrected the OpenAPI parameter default to `0` and described the threshold as Hamming distance between 64-bit perceptual
hashes, measured in differing bits. Omission selects equal perceptual hashes; callers supply a positive distance for
broader matches. Equal perceptual hashes do not guarantee identical file contents. The supported range remains `0`–`64`,
and explicit distances retain their existing behavior.

Verified the handler's fallback and the shared parameter parser: an absent value returns the supplied fallback `0`,
while explicit `0` parses to the same numeric value. Both paths then use the same query, filtering, ordering, and
response serialization. No runtime change is needed. All 101 native OpenAPI operations validate. The endpoint
documentation is grouped in `F10 - Similarity default distance`; this shared report remains in API documentation.

### F11 [Resolved] Duration sorting excludes records without duration

Approved adjustment implemented on 2026-10-19: sorting by duration must filter out records with no known duration.
Related sort descriptions now distinguish filesystem modification time and the existing fallback behavior of other keys.

Evidence: [docs/api/paths/search/search.yaml:136](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/search/search.yaml:136); [IDHANServer/src/core/search/SortKey.cpp:67](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/core/search/SortKey.cpp:67); [IDHANServer/src/core/search/SortKey.cpp:29](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/core/search/SortKey.cpp:29); [IDHANServer/src/httpapi/search/parseSortType.hpp:15](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/search/parseSortType.hpp:15).

**Implemented solution and rationale**

The duration sort joins video, audio, and animation metadata and evaluates
`COALESCE(vm.duration, am.duration, anm.duration)` without a fabricated zero. Its shared sort specification sets
`exclude_null=true`, so both term-fetch queries and fallback pagination exclude missing values. This expresses the
requested distinction between an absent measurement and a real duration of zero. The existing metadata tables require a
duration on their rows; a record with no applicable metadata is excluded.

If inconsistent data contains several media-specific rows, video takes precedence, then audio, then animation. That is
an explicit SQL precedence rather than summing unrelated measurements or treating audio as durationless. Existing sort
direction and record-ID tie breaking remain in place. Framerate and has-audio continue to use video metadata with
zero/false fallbacks; their documentation now says so. File timestamp and resolution exclusions are described
separately, and `modified_time` is identified as filesystem mtime.

The new API case supplies video, audio, animation, measured-zero, and durationless records. It checks ascending and
descending results, positive-tag fetches, negative-only searches, and paginated fallback queries. Missing-duration
records disappear consistently while the zero-duration audio record remains. This change is shared by callers of the
existing sort specification and requires no schema migration. Perceptual-hash operator behavior remains a separate V01
issue.

### F12 [Closed] Existing cluster assignment behavior is intentional

Closed by the user's 2026-10-19 decision. Preserve the existing live file and its assignment when another cluster
contains the same file. No scan implementation or cluster-scan documentation changes are requested for this finding.

### F14 [Resolved] Several domain-level 404 responses are undocumented

`POST /search` returns 404 for nonexistent text tags, namespaces, and unmatched wildcard patterns. Other missing 404
contracts include `/tags/parents/create`, `/tags/alias/create`, `/records/tags/add`, `/relationships/duplicates/add`,
and `/relationships/alternatives/add`. Their handlers or shared existence validators explicitly produce these responses.
Add text/plain 404 responses and explain the relevant lookup failures.

**Resolution — 2026-10-19:** Added plain-text 404 response contracts to search and affected relationship operations, and
confirmed the existing single-record tag-add 404 for a missing record. Existing runtime statuses are preserved.

Evidence: [docs/api/paths/search/search.yaml:75](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/search/search.yaml:75); [IDHANServer/src/httpapi/search/SearchQuery.cpp:746](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/search/SearchQuery.cpp:746); [IDHANServer/src/httpapi/tagHelpers.cpp:102](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/tagHelpers.cpp:102); [IDHANServer/src/httpapi/helpers.cpp:36](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/helpers.cpp:36).

**Approved proposal and rationale**

Add explicit `404` responses with a `text/plain` string body to each affected operation, describing the particular
missing domain entity. Use a shared response definition for the common representation if useful, but keep
endpoint-specific explanations beside the references. A missing URL route and a missing tag or record share an HTTP
status yet require different caller actions; “not found” alone does not explain which submitted reference must be
corrected.

For search, distinguish successful evaluation with zero matching records from failure to resolve a text tag, namespace,
or wildcard expansion. A known tag with no mapped records can yield an empty result, while an unknown term currently
fails resolution. Document supported negative terms using the same distinction rather than implying that an unresolved
negative term is automatically ignored. Do not change search to return empty results as part of filling this
documentation gap.

Trace shared validators at every call site to build an operation/status matrix; add only statuses reachable from that
route. Verify a missing domain, missing tag, and missing record independently, plus a syntactically invalid ID
returning 400. For batch mutations, inspect transaction boundaries and assert whether validation prevents the whole
operation or whether any documented partial result is possible. Check the actual content type and avoid inventing JSON
error envelopes or guaranteeing the first missing item in an unordered collection. The corrected contract should let
clients distinguish reference failures from malformed requests without requiring exact error-message matching.

### F15 [Resolved] Parent and alias creation omit the cycle-conflict response

`createRelationshipPairs` catches database errors containing "Cycle detected" and returns plain-text 409. Neither parent
creation nor alias creation lists 409. Document this separately from malformed 400 requests and unexpected 500 errors.

**Resolution — 2026-10-19:** Documented the existing plain-text cycle-conflict 409 on parent and alias creation.
Structured database-error translation remains a possible later hardening task; it was not required for the contract
correction.

Evidence: [docs/api/paths/tags/relationships/parent/create.yaml:27](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/relationships/parent/create.yaml:27); [docs/api/paths/tags/relationships/alias/create.yaml:40](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/relationships/alias/create.yaml:40); [IDHANServer/src/httpapi/tags.cpp:588](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/tags.cpp:588).

**Approved proposal and rationale**

Document a plain-text `409` on both creation operations: the requested relationship conflicts with the existing directed
graph because it would introduce a cycle. Keep malformed input under 400, missing references under 404 as in F14, and
unexpected database failures under 500. This distinction tells callers that reformatting an otherwise valid request will
not fix a graph conflict; they must change the relationship or existing graph.

The immediate contract correction requires no new application resolver. Database triggers remain the authority for graph
validity, including concurrent writers. Separately harden error translation if the database layer can expose structured
PostgreSQL diagnostics: identify cycle violations by a dedicated SQLSTATE or stable constraint identity rather than
matching the English phrase “Cycle detected.” Inspect existing trigger definitions first. If a new database error
identity is required, add the next sequential migration and preserve the current 409 representation at the HTTP
boundary. Do not convert every integrity violation into a cycle error.

Verify direct self-cycles and multi-edge cycles for parent and alias graphs, an allowed acyclic insertion, and any
deliberately supported duplicate insertion behavior. For a batch whose later edge closes a cycle, assert the intended
transaction rollback rather than merely checking the status. A concurrent opposing-edge case should demonstrate that the
database rejects an invalid final graph. If structured diagnostic handling is added, also show that an unrelated
database exception still follows its own error path. Update the docs even if the diagnostic hardening is scheduled
separately.

### F16 [Resolved] Request-body memory threshold no longer rejects larger bodies

Approved adjustment implemented on 2026-10-19: the 8 MiB threshold selects memory versus temporary-file storage and is
not an application-level body-size limit. The previous proposal to document a JSON 413 limit is superseded.

Evidence: [IDHANServer/src/httpapi/jsonBody.cpp:19](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/jsonBody.cpp:19); [IDHANHttp/include/idhan/http/HTTPConfig.hpp:34](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANHttp/include/idhan/http/HTTPConfig.hpp:34); [docs/api/paths/embeddings/search.yaml:5](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/embeddings/search.yaml:5).

**Implemented solution and rationale**

The raw-body reader now accepts both an in-memory IOBuf and a completed TemporaryUpload. For a spilled request it
obtains the expected byte length, reads the anonymous upload file with positional reads, and verifies the number of
bytes read before passing the result onward. I/O failures are server errors rather than malformed-JSON errors. Ownership
of the upload remains local, so its descriptor closes on both success and failure.

`readValidatedJsonBody` calls that raw reader and then performs the existing JSON validation, preserving 400 for
malformed JSON. Batch record metadata uses the same reader rather than its former independent oversize 400 branch.
Plain-text note creation uses the raw reader without incorrectly requiring JSON. The obsolete json_body_limit constant
and separate 1 MiB layout body checks were removed. The transport's 8 MiB spill threshold remains, avoiding
unconditional buffering during receipt; no size-based 413 response was added to the native contract.

There is no configured application byte cap. Parsing still needs addressable memory and remains subject to library,
database, filesystem, and available-resource limits; this implementation does not claim mathematically unbounded
processing. The HTTP README and layout descriptions make that distinction. API regressions successfully submit valid 9
MiB search, batch-metadata, and layout documents, confirm layout timestamp types, and verify oversized malformed JSON
yields 400. Existing small-body tests continue to pass.

### F17 [Resolved] Thumbnail route omits shared file response behavior

Cached native thumbnails call FileResponse, which supports Range and conditional requests and can return 206, 304, 412,
and 416. The thumbnail documentation lists none of these statuses or corresponding headers. Apply the relevant file
response contract to this route and distinguish cached-thumbnail responses from uncached 200/no-store generation.

**Resolution — 2026-10-19:** Documented cached thumbnail Range/conditional headers, 206/304/412/416 responses and cache
validators, plus the uncached 200/no-store path. Also corrected the adjacent 400/404 descriptions (G02). Runtime
delivery is unchanged.

Evidence: [docs/api/paths/records/thumbnail.yaml:24](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/thumbnail.yaml:24); [IDHANServer/src/httpapi/thumbnail.cpp:127](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/thumbnail.cpp:127); [IDHANHttp/src/FileResponse.cpp:226](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANHttp/src/FileResponse.cpp:226).

**Approved proposal and rationale**

Describe the two serving paths explicitly. A cached thumbnail served through `FileResponse` supports the range and
conditional-request behavior implemented by that shared helper. A generated response follows its own 200/no-store path.
Add the relevant request headers and `206`, `304`, `412`, and `416` responses with the actual returned headers and body
rules. Do not imply that generation automatically honors validators or ranges merely because cached delivery does.

Reuse the existing original-file response descriptions where their implementation is truly shared, while preserving
thumbnail-specific media types, sizing parameters, cache behavior, and generation failures. Check which successful and
unsuccessful branches emit ETag, Last-Modified, Accept-Ranges, Content-Range, Content-Length, and Cache-Control. In
particular, 304 must not be described as returning an image body, and an unsatisfiable range needs the helper's actual
Content-Range representation. Avoid promising unsupported multipart ranges or adding a HEAD operation solely by copying
another route's YAML.

Verify a cache hit and a cache miss separately, then cover a satisfiable range, an unsatisfiable range, a matching cache
validator, and a failed precondition against the cached path. Inspect precedence when multiple conditional headers are
present by following the helper rather than inventing an endpoint-specific interpretation. Also cover a regenerate
request that bypasses normal cached delivery. G01 separately corrects the 400/404 descriptions, so the final thumbnail
contract should combine those fixes without repeating inconsistent response definitions. No cache-generation algorithm
change is required.

### F18 [Resolved] Thumbnail purge deletes only recognized cache files

Approved adjustment implemented on 2026-10-19: purge must leave unrelated user files alone. Recursive deletion of an
entire matching bucket was too broad for that requirement and has been replaced.

Evidence: [docs/api/paths/purge/thumbnails.yaml:5](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/purge/thumbnails.yaml:5); [IDHANServer/src/filesystem/clearThumbnailCache.cpp:36](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/filesystem/clearThumbnailCache.cpp:36); [IDHANServer/src/filesystem/clearThumbnailCache.cpp:54](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/filesystem/clearThumbnailCache.cpp:54).

**Implemented solution and rationale**

Purge enumerates only immediate lowercase hexadecimal t00–tff buckets and only regular files matching the actual
generated cache layout: 64 lowercase hexadecimal hash characters, a dot, a canonical decimal size from 1 through 4096,
and `.webp`. The hash prefix must match its bucket. Unknown files, hidden temporary writes, nested directories, nonhex
buckets, misplaced hash names, and symlinks remain untouched. Directories are retained without removal/recreation.

Directory descriptors pin the enumerated root and bucket. Buckets are opened with O_NOFOLLOW, entry checks do not follow
symlinks, and unlinkat removes only a matched file name in the opened bucket. Purge does not recurse or follow a
user-provided link to another directory. A file disappearing during cleanup is tolerated; other filesystem failures
return an error. The cache helper accepts a directory path, so it can be verified against disposable trees without
touching a real user cache. Both startup cleanup and the HTTP endpoint use it.

`deleted_count` now counts successful recognized file deletions, not the remove_all entry count. A missing root is a
zero-deletion success. Concurrent thumbnail generation can publish new files during a purge; no lasting-empty guarantee
is implied. Three filesystem cases pass 48 assertions covering valid cache files, invalid sizes and names, nested
content, symlinked files/buckets/root, retained directories, missing roots, and repeated cleanup. The operation
description now states this narrow deletion contract.

### F19 [Resolved] Known file size and timestamps are independent of MIME

Approved on 2026-10-19: known file size and filesystem timestamps are returned even when MIME classification is absent.
The database already stores these values independently; the collector had been withholding them.

Evidence: [IDHANServer/src/httpapi/collectRecordInfo.cpp:147](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/collectRecordInfo.cpp:147); [docs/api/schemas/RecordInfo.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/schemas/RecordInfo.yaml); [docs/api/paths/records/info.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/info.yaml); [docs/api/paths/records/metadata.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/metadata.yaml); [tests/api/recordInfo.cpp](/home/kj16609/Desktop/Projects/cxx/IDHAN/tests/api/recordInfo.cpp).

**Implemented solution and rationale**

The shared record-info collector assigns `size`, `file_mtime`, and `file_ctime` independently of the optional MIME ID.
Its existing query already returns these stored facts, so no database migration or new persistence path is required.
File size and filesystem timestamps describe the file independently of whether its format can be classified. Both the
single-record and batch metadata routes use this collector and therefore expose the same facts.

The copied values remain optional. A missing file-info row contributes no size or timestamps; an individual NULL
timestamp remains absent. Present zero values remain present, including an empty file and Unix epoch timestamps, because
the serializer checks optional presence rather than numeric truthiness. Existing Unix microsecond units are preserved.
MIME, registered extension, `parsed`, and parsed type-specific fields retain their existing classification/metadata
rules; exposing a known size does not mark the record as parsed. The OpenAPI descriptions and a new unknown-MIME example
explain the contract. Audio channels/duration/bitrate and animation dimensions/duration are also included in the
relevant field descriptions.

Verification: two new HTTP cases pass 95 assertions across single-record and batch responses. The cases cover no
file-info row, unclassified files with known facts, a 4 GiB size, zero-byte size, epoch-zero and microsecond timestamps,
individually NULL modification/birth times, a classified but unparsed record, and parsed audio/animation fields.
Complete single-record payloads match corresponding batch entries. The broader focused API set passes 438 assertions in
20 cases; the changed collector compiles and the server links in the separate verification build. All 101 native OpenAPI
operations, including the new example, validate.

Five files are grouped in `F19 - MIME-independent file information`; endpoint/schema files retain earlier review edits
because changelists assign whole files. This shared report remains in API documentation.

### F20 [Resolved] IDs are positive and range-checked before narrowing

Approved on 2026-10-19: zero is an invalid record ID, not an accepted request sentinel, and there are no other supported
sentinels. Invalid values should produce a clear client error.

Evidence: [docs/api/schemas/RecordID.yaml:2](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/schemas/RecordID.yaml:2); [docs/api/schemas/TagID.yaml:2](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/schemas/TagID.yaml:2); [docs/api/schemas/TagDomainID.yaml:2](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/schemas/TagDomainID.yaml:2); [IDHANServer/src/httpapi/recordValidator.cpp:21](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/recordValidator.cpp:21); [IDHANServer/src/httpapi/tagHelpers.cpp:89](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/tagHelpers.cpp:89).

**Implemented solution and rationale**

The shared RecordID and TagID schemas now specify integer/int32 with bounds 1–2147483647. TagDomainID uses integer/int32
with bounds 1–32767, matching the narrower domain type. These are representable domain-ID bounds, not assumptions about
current sequence values. Existing typed path parsers reject invalid representations; record-existence validation also
explicitly rejects nonpositive IDs rather than querying or caching them.

Reusable typed positive-ID helpers validate integrality, positivity, and the destination type's upper bound before
casting. Native search bodies, record/tag mutations, relationship creation/removal, metadata batches, embedding record
inputs, and metadata-rescan requests use these checks at the former narrowing sites. Numeric namespaces are checked
before conversion as well. GET search rejects nonpositive typed tag/domain values. Shared record and relationship
existence validators protect against invalid IDs reaching them through another native caller. The unrelated
compatibility API is outside this change.

An in-range but absent entity still follows its endpoint's existing lookup behavior; this change does not turn all
missing IDs into 400 or fabricate a new sentinel response. API regressions cover zero, negatives, fractions, numeric
strings, null, int32 overflow, values that formerly wrapped to a valid ID, unsigned overflow, and smallint domain
overflow across representative mutation/search/metadata routes. The invalid values return 400 before conversion, and
valid single-record bulk tagging/removal still succeeds. No database migration or ID reassignment is required.

### F21 [Resolved] Empty tag-creation and bulk-record batches are rejected

Approved adjustment implemented on 2026-10-19: empty records arrays return 400 for both bulk addition and bulk removal.
Tag creation also documents its existing nonempty requirement.

Evidence: [docs/api/paths/tags/create.yaml:12](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/create.yaml:12); [IDHANServer/src/httpapi/tagHelpers.cpp:133](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/tagHelpers.cpp:133); [docs/api/paths/records/tags/addgroup.yaml:28](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/tags/addgroup.yaml:28); [IDHANServer/src/httpapi/records.cpp:912](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/records.cpp:912).

**Implemented solution and rationale**

Both bulk handlers check that the parsed records array is nonempty before mapping work. The normal type checks, domain
parsing, positive-ID checks, and record/set cardinality rules remain in place. An empty collection is therefore a
malformed batch rather than a successful no-op. The contract gives both bulk records arrays minItems 1 and explains
their 400 response; tag creation now has minItems 1 on its root array to match the existing No tags to create guard.

This changes the previously permissive empty-add behavior deliberately, following the user's decision. It does not
prohibit an empty per-record removal set or change the existing behavior of a nonempty add batch with neither tags nor
sets. Those are different situations from having no records to operate on. The bulk-removal success description no
longer promises an empty-input `{}` response that the new guard cannot produce.

The API regression checks empty tag creation, empty bulk addition, and empty bulk removal with a valid domain, expecting
400 in each case. It then performs valid one-record addition and removal to verify that the new guard does not interfere
with ordinary work. Existing storage/mapping tests continue to pass. The guards run before mutation, so an empty batch
cannot create tags, delete mappings, or schedule per-record tasks. These checks require no migration and preserve the
successful response shapes of nonempty requests.

### F22 [Resolved] Search leaves supported wildcard and diagnostic contracts undocumented

`POST /search` accepts namespace wildcards such as `character:*`, general tag wildcards, and their supported negations,
but its tags description only mentions literal tags, negation, and system predicates. It also accepts debug:true and
conditionally emits stats steps containing step, rows, inverted, kind, and optional micros; neither the request switch
nor response field is described. Add these supported forms and the 404 behavior for unmatched wildcard expansion. Also
replace "mutually exclusive" tags/tag_ids wording with the actual precedence: presence of tags causes tag_ids to be
ignored.

**Resolution — 2026-10-19:** Documented current literal negation, tag/namespace wildcard forms, supported wildcard
negation, unresolved-term 404, presence-based tags precedence, and debug/stats shapes. Negated namespace wildcards
remain an explicit current 400 restriction. Existing and new API cases verify these forms and diagnostics.

Evidence: [docs/api/paths/search/search.yaml:91](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/search/search.yaml:91); [IDHANServer/src/httpapi/search.cpp:85](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/search.cpp:85); [IDHANServer/src/httpapi/search.cpp:293](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/search.cpp:293); [IDHANServer/src/httpapi/search/SearchQuery.cpp:887](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/search/SearchQuery.cpp:887).

**Approved proposal and rationale**

Expand the tag grammar with concrete accepted examples for literal tags, negative literals, namespace wildcards, general
tag wildcards, and supported negative wildcard forms. Document the actual wildcard matching rules after tracing the
parser and SQL expansion, including any escaping rules; do not label them shell globs or regular expressions without
that correspondence. Explicitly identify unsupported negated namespace wildcards such as `-character:*` as 400 rather
than suggesting every positive form can be prefixed with a minus. Coordinate unmatched-expansion 404 descriptions with
F14.

Add `debug` as an optional boolean and describe `stats` only where the serializer emits it. Model its step objects with
`step`, `rows`, `inverted`, `kind`, and optional `micros`, verifying types and units from the implementation. Explain
what the diagnostics measure without promising stable query plans, timings, or exact step counts. Diagnostics should not
be used as a substitute for the result contract, and examples should show an ordinary response and a debug response.

Replace mutual-exclusion wording with the current presence-based rule: if `tags` is present, `tag_ids` is ignored,
including when `tags` is an empty array. Do not add a schema `oneOf` that rejects a request the handler accepts. Verify
both fields together, each alone, empty tags with nonempty IDs, each wildcard family in both storage/display modes, an
unmatched wildcard, and debug on/off. Use F02's alias/parent fixtures to ensure examples describe the selected mapping
layer. This proposal documents current precedence; changing it to reject mixed input would be a separate compatibility
decision.

### F23 [Resolved] Search accepts documented sorting methods and rejects unknown names

Approved on 2026-10-19: an explicitly supplied unknown sorting method returns 400 rather than silently falling back to
import-time sorting. All previously supported names and aliases remain accepted.

Evidence: [IDHANServer/src/httpapi/search/parseSortType.hpp](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/search/parseSortType.hpp); [IDHANServer/src/httpapi/search.cpp](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/search.cpp); [docs/api/schemas/SearchSortMethod.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/schemas/SearchSortMethod.yaml); [docs/api/paths/search/search.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/search/search.yaml); [tests/api/searchSorting.cpp](/home/kj16609/Desktop/Projects/cxx/IDHAN/tests/api/searchSorting.cpp).

**Implemented solution and rationale**

The shared parser returns `std::optional<SortType>` and recognizes `import_time` explicitly. Unknown strings, including
empty, misspelled, differently capitalized, and whitespace-padded names, have no parsed value. GET `by` and POST
`sort.by` convert that failure to a plain-text 400 response identifying the unknown method. POST also rejects explicitly
supplied non-string method values instead of treating them as omission. This makes client mistakes visible rather than
returning plausible results ordered by another key.

Absence remains distinct from invalid input. GET without `by` keeps file-size sorting and its ascending order default.
POST without `sort` or without `sort.by` keeps import-time sorting and its descending order default. Supplying only an
order retains the endpoint's default key; supplying only a recognized key retains its default direction. The change does
not alter supported keys, alias meanings, duration filtering, or the interpretation of sort direction.

A shared `SearchSortMethod` schema lists all 23 accepted names for both transports, including `size`, record-creation
aliases, filesystem modification/birth aliases, and `filetype`. Its descriptions distinguish record creation from
filesystem timestamps, retain existing key-specific missing-value behavior, and explicitly document rejection of unknown
names. GET and POST attach their own defaults while referencing the same enum. The shared API index registers the new
component; 400 descriptions identify unsupported sorting methods.

Verification: three new HTTP cases pass 239 assertions. They reject unknown and empty names on both transports and
invalid JSON method types; check omission/default behavior; compare every alias against independently expected ordering
on records with distinct size, import, record-creation, modification, birth, and MIME orders; and exercise all supported
names in ascending and descending requests. Random sorting is checked for acceptance without assuming deterministic
ordering. A separate comparison confirms exact parity between the parser's 23 accepted literal names and the shared
enum. The complete focused API set passes 677 assertions in 23 cases. The changed server translation unit compiles and
links in the separate verification build, and all 101 native OpenAPI operations validate.

Five files are grouped in `F23 - Strict search sorting methods`. The shared review report and API index remain in API
documentation. The search handler and endpoint documentation retain earlier approved review edits because changelists
assign whole files.

### F24 [Resolved] SSE event names, payloads, and delivery behavior are documented

The original session-event documentation described generic parsed SSE fields but omitted snapshot
data {"sessions":[...]}, update data {"id":...}, or deletion updates {"id":...,"removed":true}. The original rate-limit
event documentation had no application payload contract: their snapshot is {"limits":[...]} and updates use
scheduling_key and consecutive_limits, unlike REST lane and consecutive_failures, plus bytes_per_second. Both use named
snapshot/update/shutdown events, retry 3000, and heartbeat comments. Document the event names and data shapes so
consumers can subscribe and decode them correctly; do not imply REST and SSE payloads are interchangeable.

Evidence: [docs/api/paths/download_sessions/events.yaml:10](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/download_sessions/events.yaml:10); [docs/api/paths/rate_limits/events.yaml:10](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/rate_limits/events.yaml:10); [IDHANServer/src/downloader/DownloadSessionEventHub.cpp:30](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/downloader/DownloadSessionEventHub.cpp:30); [IDHANServer/src/downloader/RateLimitEventHub.cpp:24](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/downloader/RateLimitEventHub.cpp:24); [IDHANHttp/src/SSEBodySource.cpp:17](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANHttp/src/SSEBodySource.cpp:17).

**Resolution — 2026-10-19:** Both operations now reference explicit parsed-event schemas for `snapshot`, `update`, and
`shutdown`, with `contentMediaType` and `contentSchema` describing JSON text in `data`. Session payloads distinguish
opening IDs, invalidations, and removals. Rate-limit schemas cover all fourteen emitted lane fields, their units and
empty/zero conventions, and their differences from REST. Complete wire examples show the retry preamble, timestamp
lines, heartbeat comments, and empty shutdown data. No server behavior changed.

The [SSE client guide](api-sse.md) explains named listeners, the ignored custom timestamp, coalescing, non-atomic
snapshot/subscription boundaries, reconnecting from a fresh snapshot, absence of event IDs/replay, and subscription
cleanup. Static tracing also found that `notifyRemoved` calls `remove` before publishing its JSON notification; a
concurrent reader can observe an empty update between those calls. The schema and client guidance explicitly allow and
handle that existing edge case. Reconnecting during shutdown may receive shutdown without a snapshot.

Verification compiled a temporary harness against the existing HTTP/session/rate-limit emitter objects without
regenerating CLion's build directory. It exercised filtering, pending-update coalescing, removal, empty-update encoding,
fresh subscription snapshots, inactive lanes, millisecond rounding, keepalive generation, shutdown, and
closed-subscription termination. **70 schema/content/wire checks** passed, explicitly decoding JSON content annotations
and testing missing lane fields, negative counters, REST-name substitution, malformed JSON, and incorrect event shapes.
All **101 operations** and **35 shared schemas** validate against the official OpenAPI schema with local references and
examples checked. Browser framing guidance was checked against the linked HTML standard; no browser or live streaming
HTTP integration test was run. Changes are grouped in `F24 - SSE event contracts`; the shared index, README, and review
report remain in API documentation.

**Approved proposal and rationale**

Document SSE framing separately from the JSON carried in `data`. Provide complete wire examples for `snapshot`,
`update`, and `shutdown`, the initial `retry: 3000` preamble, and heartbeat comments. The encoder also emits a custom
`timestamp` field; explain its units and verify how supported clients expose it before promising that browser consumers
can read it. A shutdown frame has empty data, so consumers must not unconditionally JSON-parse every event. Document
named-event subscription explicitly.

Define session snapshot, session update, and session removal payloads independently: the opening array contains session
IDs, ordinary updates are invalidations identifying a session to refetch, and removed updates carry `removed: true`. For
rate limits, model the actual snapshot wrapper and update object, including `scheduling_key`, `consecutive_limits`, and
`bytes_per_second`. Preserve their current names; reusing the REST schema would conceal wire differences. Trace all
remaining serialized fields for type, units, empty-string conventions, and requiredness.

Describe delivery as state notification rather than a durable audit log. Pending per-entity updates may coalesce, and
the current frames contain no event ID providing a replay cursor. Explain reconnection through a fresh snapshot instead
of promising Last-Event-ID replay. Verify opening order, entity filtering, ordinary updates, deletion, idle heartbeat,
shutdown, disconnect cleanup, and resubscription. Include a browser-compatible client check if browser behavior is part
of acceptance; schema validation alone cannot establish streaming behavior. No field renaming or transport redesign is
necessary to document the existing stream.

### F25 [Resolved] Downloader diagnostic cursors have an explicit name and contract

`GET /downloader/debug` calls since an event sequence cursor without its format. The parser requires a comma-separated
map of session_id:sequence pairs, for example `since=12:100,15:40`; a bare sequence returns 400. Specify this syntax and
example even while allowing diagnostic response fields to evolve.

Evidence: [docs/api/paths/downloader/debug.yaml:37](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/downloader/debug.yaml:37); [IDHANServer/src/httpapi/downloaderDebug.cpp:268](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/downloaderDebug.cpp:268).

**Resolution — 2026-10-19:** Renamed the canonical query parameter to `event_cursors` and the WebUI method argument to
`eventCursors`. Its existing comma-separated `session_id:event_sequence` representation is unchanged; neither value is a
timestamp. The WebUI client now sends the canonical name. The server retains `since` as a deprecated alias, recognizes
both names in the endpoint parameter allowlist, and rejects supplying both names or repeating either parameter with 400.
Malformed-value errors identify the parameter used and the expected session/sequence representation.

The endpoint documentation now describes exclusive sequence filtering, saving returned event_sequence values for the
next poll, omitted-session behavior, 256-event runtime retention, events_dropped, zero and empty cursors, numeric
bounds, duplicate session pairs, trailing-comma tolerance, malformed requests, and the lack of persistent-history or SSE
replay guarantees. The response remains permissive for evolving diagnostics. This changes parameter naming and
compatibility handling without changing sequence generation, filtering, retention, or the response payload.

Verification: the rebuilt server passes **104 assertions in three new API cases**, including both names, malformed text
and overflow, repeated parameters, mutual exclusion, and allowlist recognition. The focused API suite passes **797
assertions in 28 cases**. The new WebUI request tests and existing Debug panel tests pass **31 cases**, and TypeScript
typechecking passes. All **101 OpenAPI operations** validate. CLion's shared build directory was not regenerated.
Changes are grouped in `F25 - Explicit diagnostic event cursors`; shared client.ts remains with F09, shared
parameterDeclarations.cpp remains with API parameter warnings, and the review report remains in API documentation.

**Original proposal and rationale (superseded by the approved parameter rename)**

Describe `since` as a single string containing comma-separated `session_id:sequence` pairs, with `12:100,15:40` as a
complete example. Explain that sequences belong to individual sessions, not to one global event stream, and that
omitting the parameter requests retained history. State that the diagnostic history is bounded by retention; a cursor is
not a guarantee of permanent historical availability. Inspect `debugSnapshots` to describe the exact exclusive/inclusive
sequence boundary before adding that detail.

Match the parser rather than imposing a stricter grammar accidentally. It currently uses a signed session-ID type, an
unsigned sequence type, and last-pair-wins replacement for duplicate session IDs. Empty input is accepted, and a
trailing comma does not create a parsed final pair. Whitespace, malformed numeric text, and overflows should be checked
against `from_chars` behavior. These details can be documented as current tolerance without encouraging invalid logical
IDs in examples. Do not add a restrictive pattern until its accepted language has been compared against the parser.

Verify omission, empty input, one pair, multiple pairs, duplicate sessions, zero sequence, overflow, a bare sequence,
missing halves, and whitespace. Check boundary filtering using retained events with known sequence numbers, including a
cursor older than retention and a cursor ahead of the newest event. Keep permissive diagnostic response fields, but
stabilize the cursor explanation because clients need it to poll correctly. Avoid presenting this cursor as reusable
with the SSE streams in F24, which have a different delivery model.

### F26 [Resolved] Download retry states and reset behavior are documented

The operation summary and description correctly say completed or failed URLs can be retried, but the 200 response says
"failed or cancelled". The handler accepts exactly completed and failed. Correct that response text. The reset deletes
descendants while preserving existing note and record_id fields; document these retained fields if callers interpret the
reset response as a completely new URL job.

Evidence: [docs/api/paths/download_sessions/session_id/urls/url_id/retry.yaml:7](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/download_sessions/session_id/urls/url_id/retry.yaml:7); [IDHANServer/src/httpapi/downloadSessions.cpp:789](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/downloadSessions.cpp:789).

**Resolution — 2026-10-20:** Corrected the success description to completed or failed. The operation now documents the
exact allowed states; reset to pending; clearing of error and finished_at; recursive descendant-job deletion;
preservation of the original job identity, session/parent relationship, URL, creation time, note, and record
association; and preservation of imported media records and files. A complete response example shows the reset job,
retained metadata, and empty children array.

The response descriptions distinguish invalid requests or failed scheduling (400), a missing session or URL within that
session (404), and disallowed or concurrently changed state (409). The documentation explicitly describes the
asynchronous success response and the lack of all-or-nothing behavior: deletion precedes the guarded reset, and
scheduling failure marks the original job failed without restoring descendants. Runtime behavior and accepted states are
unchanged.

Verification traced the current retry handler, scheduling result handling, returned serializer fields, and the parent
URL foreign key's ON DELETE CASCADE behavior. `python3 tools/openapi/validate.py` passes the official schema, local
references, examples (including the new success response), and all **101 operations**. `git diff --check` passes. No
runtime tests or builds were needed for this documentation-only correction. The endpoint file is grouped in
`F26 - Retry endpoint documentation`; the shared review report remains in API documentation.

**Approved proposal and rationale**

Change the success description to “completed or failed” so it matches both the operation description and state guard.
Explain retry as resetting an existing URL job: state becomes pending, finished time and current error are cleared,
descendant URL rows are deleted through the existing deletion/cascade behavior, and the original job identity, creation
time, note, and record association are retained. A successful response describes the reset job; it does not guarantee
that the asynchronous attempt has already completed or will succeed.

Separate precondition conflicts from scheduling failure. A job outside the allowed states returns 409, and the guarded
update can also detect another request restarting the job. Submission can fail after the reset, in which case the
handler marks the job failed and returns its current error response. Trace transaction and cascading-delete behavior
before claiming that every failure leaves descendants or prior state untouched. Retrying a URL should not be documented
as deleting an imported record merely because child jobs are removed.

Verify completed and failed inputs, an active job, a missing job, retained note/record fields, and removal of a
multilevel descendant tree. Include concurrent retries and submission failure if changing the reset implementation. The
immediate prose correction does not need a state-machine rewrite, but these cases define the boundaries any later
rewrite must preserve. Do not add cancelled to the accepted set without an explicit lifecycle decision; the current
guard is unambiguous.

### F27 [Resolved] Import media-type acceptance and force semantics match runtime

`POST /file/import` documents 400 specifically when Content-Type is not application/octet-stream, but neither its route
validation, import handler, nor shared body pipeline checks that header. Empty data and malformed query parameters do
produce 400. Keep octet-stream as the recommended upload media type while describing actual 400 causes, or implement the
documented check. Also describe force_import: it bypasses prior deletion and permits unknown-MIME imports, rather than
leaving this consequential flag unexplained.

Evidence: [docs/api/paths/file/import.yaml:98](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/file/import.yaml:98); [IDHANServer/src/httpapi/importFile.cpp:147](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/importFile.cpp:147); [IDHANHttp/src/HTTPServer.cpp:165](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANHttp/src/HTTPServer.cpp:165); [IDHANServer/src/import/ImportFile.cpp:298](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/import/ImportFile.cpp:298).

**Resolution — 2026-10-20:** Kept import permissive about HTTP Content-Type. The request body now recommends
application/octet-stream and also describes arbitrary media types with the same raw-byte handling; a missing or
different header does not itself cause rejection. MIME identification uses the uploaded bytes and optional filename
hint. The 400 description now covers empty data, malformed/repeated scalar parameters or malformed credentials, and the
existing no-writable-cluster input failure; ordinary internal database/filesystem errors retain their existing HTTP
handling.

The operation and force_import parameter now explain early duplicate/deletion suppression, unknown-MIME acceptance,
forced storage processing, reuse of existing SHA-256 record identity, and continued authentication/body/storage
requirements. They avoid promising unconditional replacement or relocation. Four complete HTTP 200 examples distinguish
an ordinary duplicate, a suppressed previously deleted hash, unknown MIME without force, and successful forced
processing. The documentation emphasizes that statuses 3 and 4 are HTTP 200 import outcomes.

Verification traced route validation, body checks, MIME/deletion/duplicate branches, eligible-cluster errors, storage
rules, record reuse, and response serialization. `python3 tools/openapi/validate.py` passes the official schema, local
references, all examples (including the four new outcomes), and all **101 operations**. `git diff --check` passes. No
runtime build or tests were needed for this documentation-only change. The endpoint file is grouped in
`F27 - Import endpoint documentation`; the shared review report remains in API documentation. Runtime behavior is
unchanged.

**Approved proposal and rationale**

Correct the 400 description to identify empty data and invalid query parameters, and retain `application/octet-stream`
as the advertised binary-upload media type. State that current runtime acceptance does not enforce that header. MIME
classification is performed from the import data and filename; an HTTP Content-Type value is not equivalent to the
classifier's result. The user approved retaining permissive header handling; no enforcement change is part of this fix.

Document `force_import` as bypassing prior-deletion suppression and allowing imports whose MIME remains unknown. Trace
its effect on existing-record/store confirmation paths before describing overwrite or replacement behavior; the name
alone does not establish those guarantees. Explain that forced import still goes through authentication, body
processing, storage, hashing, and ordinary failure paths. It is not a general validation bypass. Include examples
distinguishing normal duplicate handling, a previously deleted hash, and an unknown-MIME payload.

Verify identical binary data with the recommended header, an omitted header, and a different header to establish current
acceptance. Separately check zero-length data, malformed flags, unknown MIME with force off/on, and a previously deleted
record with force off/on. If enforcement is selected, decide whether MIME parameters and alternative binary media types
are accepted, use the established API error convention, and assess existing clients before changing behavior. Do not
relabel storage or filesystem failures as input errors simply because they occur during import.

**Decision — 2026-10-20**

Keep current permissive HTTP Content-Type handling and recommend application/octet-stream. No media-type enforcement or
new rejection status is introduced.

## Upstream-provided downloader specification — outside our change scope

Pawchive supplies `IDHANDownloader/specs/pawchive-api.yaml`; IDHAN consumes it to generate an interface client. The user
confirmed on 2026-10-20 that we must leave that supplied specification untouched. E01 and E02 are retained only as
observations about upstream input, excluded from the remaining work list. The earlier proposals to modify the supplied
file or change generator validation as part of those fixes are withdrawn. No edits to the Pawchive specification were
made during this review.

### E01 [Upstream / out of scope] Pawchive 401 response-reference structure

Five operations use `#/components/schemas/401` in a response context. The target contains `title` and `description`,
whereas OpenAPI expects a Response Object in that location. A structurally valid upstream correction would define a
reusable response under `components.responses`; this is an upstream observation, not an IDHAN implementation task. The
affected operations are GET /account/favorites and POST/DELETE on the favorite-post and favorite-creator paths.

Evidence: [IDHANDownloader/specs/pawchive-api.yaml:1](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANDownloader/specs/pawchive-api.yaml:1),
specifically the five `#/components/schemas/401` response references and their target.

**Disposition — 2026-10-20:** Retain Pawchive's provided input. Any future client-generation compatibility work belongs
in our own generation tooling and requires a concrete generation problem; it does not authorize patching the upstream
specification or guessing the external service's error payload.

### E02 [Upstream / out of scope] Pawchive contact metadata placement

The supplied document places `contact` beside `openapi`, `info`, and `paths`; OpenAPI locates that metadata under
`info.contact`. This is another upstream-input observation. The native validator covers `docs/api`, and the downloader's
external-client generation uses `--skip-validate-spec` as a compatibility accommodation.

Evidence: [IDHANDownloader/specs/pawchive-api.yaml:1](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANDownloader/specs/pawchive-api.yaml:1); [IDHANDownloader/CMakeLists.txt:108](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANDownloader/CMakeLists.txt:108).

**Disposition — 2026-10-20:** Preserve the supplied metadata and current generation behavior. This review does not
schedule a local metadata rewrite, a new external-spec validation requirement, or removal of the compatibility flag.

## Other follow-up gaps

These expand the smaller gaps from the initial review without renumbering the original findings. Native authentication
and its header/query precedence matched the implementation. Health, version, heartbeat cadence, basic notes/URL response
shapes, database statistics, MIME detection/module output, embedding request/response shapes, layout CRUD status codes,
and job polling/cleanup showed no additional material mismatch beyond those recorded here.

### G01 [Resolved] Cluster identity checks cover names and normalized paths

Cluster creation describes 409 as a path conflict, while duplicate cluster names also return that status.

Evidence: [docs/api/paths/clusters/add.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/clusters/add.yaml); [IDHANServer/src/httpapi/clusters.cpp:355](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/clusters.cpp:355); [IDHANServer/src/httpapi/clusters.cpp:404](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/clusters.cpp:404).

**Resolution — 2026-10-20:** Added an explicit name preflight before cluster creation's filesystem work, using a typed
`db::findClusterByName` lookup shared with rename validation. Creation checks both the exact name and normalized
registered path before directory creation or write tests. The path normalizer now converts both submitted and stored
paths to absolute lexical form, including legacy relative registrations; dot segments and trailing separators are
simplified. Symlinks are not resolved, and existing database path strings are not rewritten. Names retain their existing
case-sensitive uniqueness policy.

Existing database unique constraints and INSERT ON CONFLICT handling remain the concurrent-creation backstop. Rename
validation permits the current cluster's own name and rejects another cluster's name. A concurrent rename's specific
cluster-name unique-constraint violation now returns 409; unrelated database failures are propagated. The update
endpoint continues to ignore path changes as documented, without introducing a file relocation feature.

The API documentation now covers both 409 causes, lexical normalization, existing-directory acceptance, conflict timing,
case-sensitive names, and concurrent name conflicts. Complete plain-text name/path conflict examples are included. The
change preserves filesystem content for known conflicts by checking before the write test.

Verification compiled and linked the changed server sources into separate artifacts without regenerating CLion's build
directory. Seven new API cases passed **43 assertions**, covering duplicate-name filesystem side effects, normalized
relative/absolute/dot/trailing path aliases, legacy relative registrations, valid distinct creation, self/duplicate
renames, concurrent duplicate names, concurrent normalized paths, and concurrent renames. The broader focused suite
passed **840 assertions in 35 cases**. All **101 OpenAPI operations** and their examples validate, and
`git diff --check` passes. Changes are grouped in `G01 - Cluster identity validation`; the shared review report remains
in API documentation.

**Approved proposal and rationale**

Expand the 409 description to cover a duplicate configured name or path, preserving the plain-text response schema.
Explain uniqueness in terms of the actual database constraints and path normalization performed by the handler. Do not
assert that two textual paths identifying the same physical location are always recognized as identical unless
canonicalization actually establishes that. The conflict refers to the configured cluster identity, not to an arbitrary
preexisting directory on disk.

Review create and rename/update operations together because callers need the same name-conflict vocabulary across them.
Keep the preflight existence check and the database uniqueness-error handling distinct in the implementation: a precheck
improves diagnostics but cannot prevent another concurrent request from inserting the same value. If translating
database failures is changed, map only the relevant named unique constraints to 409; unrelated failures must not become
misleading conflict responses.

Verify a duplicate name with a different path, a duplicate path with a different name, and a valid new combination.
Include two simultaneous attempts if modifying conflict handling, and check that one succeeds while the other reports
the correct conflict. Existing directories, missing directories, readonly settings, and permission errors belong to
their own documented branches. This is primarily a description correction; it does not require a new uniqueness policy
or changing cluster creation's filesystem side effects.

### G02 [Resolved] Thumbnail bad-request description includes not-found cases

The thumbnail 400 description includes nonexistent records and missing file information, which follow 404 paths.

**Resolution — 2026-10-19:** Corrected with approved F17: 400 describes invalid parameters or unavailable thumbnailers;
absent records/file information are described by 404.

Evidence: [docs/api/paths/records/thumbnail.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/thumbnail.yaml); [IDHANServer/src/httpapi/thumbnail.cpp:139](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/thumbnail.cpp:139); [IDHANServer/src/httpapi/thumbnail.cpp:157](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/thumbnail.cpp:157); [IDHANServer/src/httpapi/thumbnail.cpp:168](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/thumbnail.cpp:168).

**Proposed solution and rationale**

Split the explanations by reachable status. Describe invalid size or regenerate parameters and the existing
unsupported-thumbnailer case under 400. Describe an absent record and an existing record without file information under
404, following the shared record validator and thumbnail handler. Keep both error bodies as the actual plain-text
representation. This lets clients distinguish correcting a request from finding a record that has not acquired file
information yet.

Avoid changing unsupported-media behavior to a different status merely because another status might also be defensible;
this finding concerns inaccurate descriptions of the existing branches. If the actual stored file cannot be opened,
follow that separate error path before adding it to any response description. A file-info row, a physical file, a cached
thumbnail, and an available thumbnailer are different prerequisites and should not be collapsed into one “missing
thumbnail” condition.

Verify a malformed record ID, a valid missing ID, an existing record with no file info, size zero, size over 4096, and a
MIME with no thumbnailer. Check cache hits separately because an existing cached thumbnail can bypass generation
prerequisites. Combine this documentation correction with F17's conditional/range response additions and validate the
resulting single operation. No new production test is needed merely to reword a known error description; targeted
existing fixtures or a response matrix are sufficient unless handler behavior changes.

### G03 [Resolved] Download error list truncation, count scope, and time units are unclear

The error list returns at most the newest 1000 matching entries. Status counts cover the entire session rather than the
filtered or truncated list, and `occurred_at` is epoch seconds.

Evidence: [docs/api/paths/download_sessions/session_id/errors.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/download_sessions/session_id/errors.yaml); [IDHANServer/src/httpapi/downloadSessions.cpp:924](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/downloadSessions.cpp:924); [IDHANServer/src/httpapi/downloadSessions.cpp:942](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/downloadSessions.cpp:942).

**Resolution — 2026-10-20:** Documented independent count/list scopes, filtering before the descending-ID limit,
`maxItems: 1000`, Unix-second timestamps, nullable status and URL-job references, and the `none` filter token. Clarified
that omitted/empty filters include all statuses, counts can differ from the returned list length, and a full-length list
does not prove truncation or completeness. The response provides no pagination cursor or truncation flag. Counts and
rows are separate queries, so no atomic snapshot is promised. Empty, filtered HTTP, and no-HTTP-status examples
illustrate the contract.

The correction was checked against the filter parser, both SQL queries, and the serializer. Official OpenAPI schema,
references, examples, and all **101 operations** validate; `git diff --check` passes. Runtime queries and retention
behavior are unchanged, and no new runtime tests were needed for this documentation correction. The endpoint is grouped
in `G03 - Download error list documentation`; this shared report remains in API documentation.

**Proposed solution and rationale**

Document the response as two related but independently scoped collections. `statuses` summarizes all retained errors for
the session. `errors` applies the requested status filter, orders by descending error ID, and then limits to 1000.
Describe the ordering as ID order rather than asserting strict wall-clock order, and add `maxItems: 1000` to the error
array. State the epoch-second unit on `occurred_at`; retain nullable status and URL references where emitted.

Explain that a null status represents an error with no HTTP status, and document the existing `none` filter token
consistently. Counts need not add up to the returned array length, both because of filtering and because of truncation.
A client receiving exactly 1000 entries cannot determine from length alone whether additional matching history exists;
do not invent a completeness guarantee or pagination cursor absent from the implementation.

Verify no errors, mixed HTTP/no-status errors, a selective filter, and more than 1000 matching entries. Assert counts
independently from row filtering and verify the precise retained IDs. This can be done in a focused database-backed case
if the implementation changes. Full-history pagination would be a separate feature, best designed around a stable
error-ID cursor rather than an offset while new errors are being inserted. The present correction needs no new query or
retention policy.

### G04 [Resolved] Response schemas understate stable fields and overstate job-result validation

Several schemas leave always-emitted fields optional. Separately, the unconstrained `{}` alternative in
`JobStatus.response` means its specialized alternatives do not restrict accepted result values.

Evidence: [docs/api/schemas/JobStatus.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/schemas/JobStatus.yaml); [docs/api/schemas/TagInfoExtended.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/schemas/TagInfoExtended.yaml); [docs/api/paths/embeddings/models.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/embeddings/models.yaml); [docs/api/paths/search/search.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/search/search.yaml); [docs/api/paths/layouts.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/layouts.yaml).

**Resolution — 2026-10-20:** The user approved preserving arbitrary JSON job results and documenting known producer
shapes separately. Replaced the redundant result `anyOf` fallback with an explicitly unrestricted result schema. The
response guide and producer descriptions identify ClusterScanResult and RescanMetadataResult without treating job
display names as discriminators or requiring a result before completion.

Added serializer-backed required fields to tag creation/extended tag schemas, embedding model items, layout items, and
POST search summaries. Search requires at least one requested result array while allowing both, and keeps diagnostics
conditional. Model row estimates remain optional. Job conditionals require name, location, and completion flag for known
states and error text for failures; start time and result remain optional. The minimal not-found serializer placeholder
remains valid, while the single-job endpoint still returns HTTP 404 for an unknown ID. A
new [response contract guide](api-response-contracts.md) records the field-presence matrix, job states, result extension
policy, and client-tooling limits. The WebUI's handwritten TagInfo type no longer requires the nonexistent `subtag.id`;
no consumer used that field.

Verification used a temporary server and isolated migrated PostgreSQL schema: **107 schema witness checks passed across
20 actual HTTP responses**, including empty/populated layouts, namespace-less tag decomposition, unavailable models
before/after table analysis, IDs-only/hashes-only/both search arrays, optional diagnostics, and completed
metadata/cluster-scan results. Negative witnesses reject missing stable fields; synthetic queued/failed/not-found states
and arbitrary JSON values cover branches not reliably observable during short jobs. Forward-compatible extra fields
remain accepted. TypeScript checking passed using the installed compiler directly after the pnpm wrapper failed to open
its external store lock. The official OpenAPI schema, local references, examples, and all **101 operations** validate,
and `git diff --check` passes. Native clients use handwritten types; no external client generator was run. Server
behavior is unchanged.

Changes are grouped in `G04 - Response field and job result contracts`. Whole-file assignments preserve earlier
documentation fixes, F23 search contracts, cluster-marker scan documentation, and F09 WebUI type changes. Shared API
README and this report remain in API documentation. No commits created.

**Proposed solution and rationale**

Build a serializer-to-schema field matrix for each affected response: always emitted, conditionally emitted, nullable,
and dependent on request options or job state. Add `required` only to the first group in the schema where the guarantee
applies. An always-present property whose value can be null should be required and nullable; a genuinely omitted
property should remain optional. Check empty responses and failure states before inferring a guarantee from a populated
example. In search, requested hashes or diagnostic stats are conditional and must not become universally required.

For job results, keep the general result contract separate from known examples under the approved arbitrary-JSON policy.
An `anyOf` containing `{}` accepts every JSON value, so the specialized branches are descriptive examples rather than
enforcement. Merely changing it to `oneOf` would be wrong: a known result could match both its specialized schema and
the unconstrained alternative. Strict typing would require a stable job-kind discriminator and mutually exclusive
state/result variants, or typed operation-specific envelopes; that is a separate future API design. The current
correction explicitly preserves extensible results.

Validate real response witnesses and negative witnesses that omit newly required fields. Check supported client
generation because tighter response guarantees can improve types but may expose historical server-version differences.
Avoid setting `additionalProperties: false` globally; required-field corrections do not require rejecting
forward-compatible additions.

**Decision:** Arbitrary job JSON remains supported. No discriminator or older-server compatibility change is required
for this correction, and no questions remain open for G04.

### G05 [Resolved] Tag information preserves domain-specific storage and display counts

The previous tag-info endpoint summed storage counts across every domain into
`items_count` and exposed no display count or domain breakdown. This discarded
information already present in `tag_counts` and differed from F09's scoped
maximum estimates.

Evidence: [docs/api/schemas/TagInfoExtended.yaml](api/schemas/TagInfoExtended.yaml);
[IDHANServer/src/httpapi/tags.cpp](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/tags.cpp);
[IDHANServer/src/db/tagDomainCounts.cpp](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/db/tagDomainCounts.cpp).

**Resolution — 2026-10-20:** The user requested implementation changes instead of
merely documenting the sum. Both tag-info routes now return `storage_count`,
`display_count`, and `domain_counts`. The previous count field is removed with
no deprecated alias. Both clients and the schema use the new shape. Existing
namespace, subtag, tag-ID validation, and tag lookup behavior are preserved.

**Implemented solution and rationale**

Each existing `tag_counts` row already identifies one tag and domain, with
separate active-storage and resolved-display counts. A typed database helper
reads these values without reimplementing alias or parent resolution. It starts
from selected `tag_domains` and left-joins the tag's counters, returning zeros
for unused domains even when no counter row exists. This distinguishes a valid
unused domain from a nonexistent requested domain. No migration or counter
recalculation is required. Sibling resolution remains stubbed out.

Both path and query routes accept repeated `tag_domains` IDs. Omission includes
all existing domains. Domain selections are normalized by sorting and removing
duplicates; response rows are ordered by domain ID. Invalid, zero, negative,
empty, or overflowing IDs return 400; a nonexistent requested domain returns

404. These names are included in endpoint parameter declarations. Autocomplete
     continues using the same validation and its existing singular-domain fallback;
     tag info introduces only the canonical repeated-domain parameter.

The two top-level estimates are independent maxima across the selected rows.
For example, storage/display pairs `(4,10)` and `(7,8)` produce `(7,10)`. The
maxima need not come from the same domain. This follows the approved F09 estimate
policy and avoids summing overlapping domain mappings. It is not an exact
union count and can undercount distinct records spread across domains. Clients
needing individual values use `domain_counts`. Stored counters retain their
existing freshness and maintenance behavior.

The Qt client accepts domain IDs and decodes both maxima and the per-domain
array into signed 64-bit count fields. This also removes its old mismatched
`item_count` parsing. The WebUI exposes the new response type and a domain-selection
request option, forwarding cancellation. Its tag-text cache still caches only
text, which is independent of the selected count scope. No legacy count field
is sent, parsed, typed, or documented as part of the current API contract.

Verification: the focused tag-info cases passed **170 assertions in four cases**,
covering direct/aliased/inherited counts, independent maxima, overlapping and
excluded domains, normalization, all-domain defaults, zero rows, both routes,
and malformed or missing domains. The broader API regression set passed **1038
assertions in 42 cases**, including autocomplete and prior fixes. WebUI
TypeScript checking and **11 tests across three files** passed. All **27 Qt
client translation units** compiled into separate verification artifacts; the
server compiled and linked there as well. OpenAPI schema, local references,
examples, and all **101 operations** validate. An additional **112 schema
witness checks across 20 live HTTP responses** verify the required new fields,
per-domain rows, arbitrary job JSON, and other tightened G04 contracts. Existing tag-info assertions
about an earlier obsolete query-route bug were updated to the already-supported
scalar parameter behavior. No commits were created.

Changes are grouped in `G05 - Domain-specific tag information counts`.
Whole-file assignments retain earlier tag behavior, F09 client/count changes,
G04 response-field corrections, and parameter declarations. Shared index,
response guide, and this report remain with their existing documentation groups.

**Questions:** None remain for the approved scope. Exact distinct-record union
counts would be a separate API capability, with explicit domain, resolution,
and consistency requirements.

## Verification follow-ups discovered during F02

These are separate implementation/build issues exposed during review verification, not additional claims that the
OpenAPI schema itself is structurally invalid. V01–V03 are resolved.

### V01 [Resolved] Application schema and extension lookup are consistent across connections

The broader regression run failed the existing perceptual-nearby case because PostgreSQL could not resolve
`<~>(bit, bit)`. The test extension was installed in `public`, while runtime connections set their search path to the
configured application schema only. Both nearby search and the similarity endpoint use the operator unqualified. This is
independent of storage-mode query selection.

Evidence: [IDHANDb/src/DBConnection.cpp:342](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANDb/src/DBConnection.cpp:342); [IDHANServer/src/httpapi/search/SearchQuery.cpp:445](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/search/SearchQuery.cpp:445); [IDHANServer/src/httpapi/relationships.cpp:178](/home/kj16609/Desktop/Projects/cxx/IDHAN/IDHANServer/src/httpapi/relationships.cpp:178); [tests/api/searchNearby.cpp](/home/kj16609/Desktop/Projects/cxx/IDHAN/tests/api/searchNearby.cpp).
The broader run passed seven of eight cases; the focused set excluding perceptual-hash cases passed all six cases.

**Implemented solution and rationale**

New installations use `idhan` for application objects and `database.extension_schema` (default `public`) for extensions.
The shared builder in
`IDHANDb/include/idhan/db/SearchPath.hpp` supplies both migration and runtime connections with the configured
application schema followed by the configured extension schema. Both names are quoted as individual identifiers,
preserving spaces, commas, and embedded quotes; matching schema names appear once. Runtime configuration retains bound
`set_config`
values. Migration startup applies the same setting through a bound value instead of interpolating a schema into libpq
connection options. Application objects therefore resolve first and extension operators/types remain visible on every
newly established connection. This also covers embedding queries using `halfvec` and `<=>`, rather than repairing only
the two `<~>` expressions.

Database bootstrap runs transactionally before numbered migrations, including when the migration history is already
current. The database is always treated as shared: startup performs no IDHAN-only ownership inventory and permits
unrelated schemas and tables. It checks the four dependencies already required by the migration history (`pgcrypto`,
`pg_trgm`, `pg_stat_statements`, and `vector`) through PostgreSQL's extension catalog. All missing extensions are
installed in the configured extension schema when the database role has permission; a missing namespace is created
when permitted. The setting supports `IDHAN_DATABASE_EXTENSION_SCHEMA` and `--pg_extension_schema` overrides. Each
attempt uses a savepoint; failures are
collected with PostgreSQL reasons and administrator installation commands. If any dependency fails, bootstrap rolls
back all extensions created in that attempt before application-schema creation or migration changes. Existing
extensions are never moved or updated. Vector must be in the configured extension schema; the other extensions may
remain in
that schema or the configured application schema for compatibility with existing installations. Unsupported extension
namespaces produce administrator instructions. The historical `CREATE EXTENSION IF NOT EXISTS` migration statements
become no-ops after this prerequisite check. A restricted application role still needs no extension-installation
privileges when an administrator has provisioned all dependencies in advance.

Existing objects are never relocated automatically. Startup rejects `public.idhan_info` regardless of the
configured schema or whether the destination already contains migration history. Existing public installations must
be migrated manually, preserving application objects and history together, or users must configure a new database.
Custom application-schema settings remain supported when public migration history is absent. The proposed startup
relocation flags and implementation were removed. Configuration documentation and the example configuration explain
the manual migration requirement.

The revised rejection tests cover default, public, and custom settings with and without destination history, checking
that startup preserves source data, extensions, and the connection search path and does not create a destination schema.
The guard runs before extension creation, so a legacy installation receives the manual migration error even when
required extensions are missing. Tests cover automatic installation and repair when files are available, repeat
initialization, permission-denied aggregation with a usable parent transaction, missing extension files, and complete
rollback after partial installation. The local fixture lacks pgvector, so cases requiring all four extension binaries
skip explicitly. Current focused verification passes 114 assertions in eight cases, with eight cases skipped for missing
pgvector files; the earlier broader verification below predates these bootstrap changes.

Verification: 62 database assertions in nine cases cover defaults, repeat initialization with preinstalled extensions,
an entirely missing extension set, each individually missing dependency, preservation of extension
identities/namespaces/versions, unrelated schemas/tables/views/sequences, preserved public records, custom quoted
identifiers, unsupported extension namespaces, and existing application-schema extension layouts. The complete focused
API set passes 343 assertions in 18 cases, including the formerly failing nearby case, the similarity endpoint, missing
hashes, malformed distances, and a restart that establishes new connections. The restart harness retains its persisted
API key instead of attempting to mint another one. A separate shared-database smoke test starts the full server with no
schema setting under a non-superuser application role, verifies creation of `idhan` application objects, and confirms
that administrator-provisioned extensions and another application's data remain unchanged. Changed
server/database/migration translation units compile and link in the separate verification build.

Changes are grouped in `V01 - Dedicated application schema and extension checks`. The shared review report remains in
API documentation, and `tests/CMakeLists.txt` retains its existing F18 assignment because it contains both changes.

### V02 [Resolved] API tests use libcurl independently of the server runtime

The original API test target linked an unresolved `drogon` library name and used
Drogon/Trantor headers and status types. Earlier review verification needed a
standalone test build supplying that dependency explicitly. The user confirmed
that the framework is being removed completely.

Evidence: [tests/CMakeLists.txt](/home/kj16609/Desktop/Projects/cxx/IDHAN/tests/CMakeLists.txt);
[ApiClient.hpp](/home/kj16609/Desktop/Projects/cxx/IDHAN/tests/api/harness/ApiClient.hpp);
[ApiClient.cpp](/home/kj16609/Desktop/Projects/cxx/IDHAN/tests/api/harness/ApiClient.cpp).

**Resolution — 2026-10-20:** Replaced the harness client with libcurl, which is
already used by the downloader. Tests discover and link `CURL::libcurl`, retaining
its include directories and transitive requirements. API responses use libcurl's
native numeric status representation; all assertions use HTTP status numbers.
The stale submodule declaration and active architecture references were removed.
No framework compatibility namespace, wrapper, headers, event loop, or library
is retained. Historical mentions here explain the finding rather than describe
a current dependency.

**Implemented solution and rationale**

Each request owns its easy handle and header list, avoiding mutable handle state
shared by concurrent test calls. A process-lifetime owner initializes libcurl
before clients and cleans it up after them. The harness connects directly to its
loopback server, ignores environment proxy settings, and leaves redirects and
HTTP error handling visible to tests. It does not invoke a command-line client.

Query names and values are encoded individually, preserving repeated parameters
and empty values. Compact JSON requests carry the JSON media type, while octet
requests use explicit byte lengths, preserving embedded NUL and non-UTF-8 data.
Response bodies retain their original bytes; JsonCpp parses valid JSON without
logging errors for ordinary plain-text error bodies. Final response headers are
normalized to lowercase, with interim response headers discarded. Transfer
callbacks capture exceptions and return failure to the C API before rethrowing
in ordinary C++ control flow.

Ordinary requests retain a 30-second total deadline and raw uploads retain a
60-second deadline. Positive constructor overrides permit fast deadline tests.
Connection and timeout failures throw diagnostics including the requested path;
HTTP 4xx/5xx responses retain their status, headers, and body for assertions.
The [API test guide](../tests/api/README.md) documents these contracts and the
normal build/run workflow.

The full normal test target exposed pre-existing fixture drift hidden by the
earlier focused build. File/scan fixtures now register clusters through the API
to receive valid identity markers rather than inserting unverifiable database
rows. Download fixtures expect the documented 201 creation response, initial-key
tests expect the already-documented 404 after a key exists, and metadata-rescan
tests send the documented `records` array and inspect per-record failures in a
completed job result. No production API behavior changed to satisfy these tests.

Verification configured a new `/tmp/idhan-v02-build` with tests enabled and
compiled the **normal IDHANAPITests target**, its server, module runner, and
modules from the repository's CMake graph. Authenticated server tests passed **1722 assertions across 78 cases**, with
one accepted sibling-stub case skipped
and only the separately identified background-hash case excluded (V03). All **four transport cases pass through normal
CTest discovery**, covering query
encoding, authentication, JSON and binary bodies, chunked/interim responses,
methods, HTTP errors, redirects, both deadlines, and refused connections. The
fresh build graph/cache and executable library list contain no old-framework
link dependency. Official OpenAPI validation still covers all **101 operations**,
and `git diff --check` passes.

Verification reused cached dependency source checkouts without regenerating
CLion's build directory. Downloader asset bundling used three existing npm
packages copied into an isolated offline cache and an npm launcher selecting it;
no upstream specification or downloader generator was edited. CTest discovery
used a writable temporary directory instead of the sandbox-inaccessible desktop
runtime directory. These were environment adjustments, not API-target dependency
workarounds. No standalone API-client build was needed.

Changes are grouped in `V02 - Remove legacy HTTP framework dependency`.
Whole-file assignments preserve earlier API regressions and shared test wiring;
shared review and architecture documentation retain their existing documentation
groups where appropriate. No commits created.

**Decision:** Remove the framework completely; libcurl supplies the independent
test client. No questions remain for V02.

### V03 [P1] Background perceptual-hash processing was removed — resolved

The normal full-suite run exposed a runtime failure: after image
perceptual hashes are inserted, `hamming_distance_queue` receives work but no
pairs appear in `hamming_distance`. The existing case expected three near pairs
and observed zero after its 40-second wait. The current server contains a reader
of the cached pair table for undecided duplicates, but no queue-consumption or
periodic distance-maintenance implementation was found. Investigation also found
the missing-image-phash backfill had been removed. This is separate from
V01's extension lookup and from direct perceptual-nearby searches.

Evidence: [tests/api/hammingDistance.cpp](/home/kj16609/Desktop/Projects/cxx/IDHAN/tests/api/hammingDistance.cpp);
[queue triggers](../IDHANMigration/src/func_queue_hamming_distance/319-create.sql);
[undecided-pair reader](../IDHANServer/src/httpapi/relationships.cpp);
[server startup](../IDHANServer/src/ServerContext.cpp).

**Decision and implementation — 2026-10-20**

The approved approach ports both old workers to the native runtime. Commit
`cfaf6713998a75484fc547e435aae52df1bef95d` removed `hammingQueue.cpp`,
`phashSweeper.cpp`, their headers, and the startup calls during the HTTP-backend
replacement. The remaining maintenance contract declaration did not start any
processing. Restored implementations now live in those original source areas,
with typed queries in [db/hashMaintenance.cpp](../IDHANServer/src/db/hashMaintenance.cpp)
and a server-owned [HashMaintenance](../IDHANServer/src/maintenance/HashMaintenance.hpp).

Each worker starts immediately after database/cluster initialization, runs one
sweep at a time on the Folly database executor, and waits one second after a sweep
finishes or fails. Owned timer threads wait for completion; they do not block the
database executor. A high-water record ID and ordered cursor bound each run
without allocating the full backlog. Work added behind the cursor and locked
queue rows remain available to later sweeps. Failures are logged and retried.

Distance maintenance claims at most 32 records with `FOR UPDATE SKIP LOCKED`.
It takes relationship advisory lock `(16609, 1)` before any queue or pair locks,
matching the current relationship mutation paths. Deleting queue entries,
removing old pairs, and inserting the replacement pairs share one transaction.
Failure restores the queue and previous pair cache. A concurrent hash-changing
transaction cannot lose its trigger requeue: its insert waits for a claimed
queue row's deletion transaction, then inserts a fresh row after that commit.
The concurrency test observes both the paused worker and blocked writer before
releasing the worker and confirming that the new row survives. No generation
column or migration is needed for this protocol.

Recomputation preserves exact PostgreSQL `<~>` comparisons and the eight-bit
cache cutoff, canonical ID ordering, and pair uniqueness. The old
`flattened_duplicates` dependency is replaced with the current `duplicate_members`
table, with isolated records resolving to themselves. SQL excludes same-group
pairs and group-root alternative/unrelated rulings. Clearing a hash removes its
old pairs, and relationship-triggered requeues rebuild newly eligible pairs.
The zero-distance default for direct similarity search is unchanged.

Backfill reads at most eight eligible image rows per batch: missing phash,
assigned cluster, and no deletion timestamp. It opens through verified cluster
access and invokes the existing remote parser module, retaining verified access
across parsing and checking it again before writing. A conditional update writes
only a still-missing phash on the selected active cluster/MIME assignment. It
preserves dimensions, flags, file size/timestamps, and any foreground hash.
Unreadable originals, marker failures, unavailable parsers, and blank results
remain retryable without detaching file assignments or blocking subsequent IDs.
This restores missing-hash processing for existing image-metadata rows; it does
not introduce a scan of completely unparsed records.

Shutdown requests both stop tokens, wakes timer waits, and joins both workers
before destroying their dependencies. Already-started distance batches finish
atomically; backfill stops between records and checks shutdown after parsing
before writing. An in-flight remote parser call must return before draining can
complete; this port does not add forced cancellation to that protocol. Durable
queue entries and remaining NULL hashes are reconsidered after a crash/restart.
The exact distance join can still be expensive on large collections despite
bounded claims; no approximate limit or collection-scale performance claim is
introduced. Operational behavior is described in [jobs.md](jobs.md#periodic-maintenance).

**Verification**

The isolated normal CMake build produces the server, API tests, and new native
`IDHANHashMaintenanceTests` target. The latter passes **44 assertions in five
cases**, covering batch bounds, locked rows, injected-failure rollback, flat
group/ruling suppression and requeues, a concurrent hash update during queue
deletion, and conditional backfill under foreground writes, relocation, deletion,
and MIME changes. Its successful-write case also checks exact bit ordering and
preserved metadata.

Two added API cases pass **25 assertions**, covering real remote-parser backfill,
a missing marker followed by restoration, eight unreadable originals preceding
a valid image, preserved file assignments/metadata, a backlog seeded while the
server is stopped, and prompt clean shutdown/restart. The existing hamming case
passes with its expected boundaries and changed/cleared-hash behavior intact.
The complete API suite now passes **1762 assertions, 81 passing cases, and the
one intentional F01 sibling-stub skip**; the V03 exclusion recorded under V02 is
no longer needed. Final focused perceptual-hash verification passes **166
assertions in ten API cases**. `git diff --check` passes.

Implementation, integration, tests, and maintenance documentation are grouped in
`V03 - Restore background hash maintenance`. Whole-file assignments retain
earlier startup, schema/cluster, test-build, and documentation edits; the shared
review report remains in `API documentation`. No questions remain for V03.

## Coverage inventory

Each operation below was included in the static review. A listed operation is not a claim of an end-to-end test. Source
references point to the unbundled path descriptions.

| Methods          | Path                                                  | Documentation                                                                                                                                                                     |
|------------------|-------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| GET              | `/version`                                            | [docs/api/paths/version.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/version.yaml)                                                                               |
| GET              | `/generate_api_key`                                   | [docs/api/paths/auth/generate_key.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/auth/generate_key.yaml)                                                           |
| POST             | `/records/create`                                     | [docs/api/paths/records/create.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/create.yaml)                                                                 |
| GET              | `/records/random`                                     | [docs/api/paths/records/random.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/random.yaml)                                                                 |
| GET              | `/records/search`                                     | [docs/api/paths/records/search.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/search.yaml)                                                                 |
| GET, HEAD        | `/records/{record_id}`                                | [docs/api/paths/records/file.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/file.yaml)                                                                     |
| GET, HEAD        | `/records/{record_id}/file`                           | [docs/api/paths/records/file.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/file.yaml)                                                                     |
| GET              | `/records/{record_id}/info`                           | [docs/api/paths/records/info.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/info.yaml)                                                                     |
| GET              | `/records/{record_id}/metadata`                       | [docs/api/paths/records/info.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/info.yaml)                                                                     |
| POST             | `/records/{record_id}/metadata/scan`                  | [docs/api/paths/records/metadatascan.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/metadatascan.yaml)                                                     |
| GET              | `/records/{record_id}/thumbnail`                      | [docs/api/paths/records/thumbnail.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/thumbnail.yaml)                                                           |
| POST             | `/records/tags/add`                                   | [docs/api/paths/records/tags/addgroup.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/tags/addgroup.yaml)                                                   |
| POST             | `/records/tags/remove`                                | [docs/api/paths/records/tags/removegroup.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/tags/removegroup.yaml)                                             |
| GET              | `/records/{record_id}/tags`                           | [docs/api/paths/records/tags/info.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/tags/info.yaml)                                                           |
| POST             | `/records/{record_id}/tags/add`                       | [docs/api/paths/records/tags/add.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/tags/add.yaml)                                                             |
| POST             | `/records/{record_id}/tags/remove`                    | [docs/api/paths/records/tags/remove.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/tags/remove.yaml)                                                       |
| GET              | `/records/{record_id}/tags/active`                    | [docs/api/paths/records/tags/active.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/tags/active.yaml)                                                       |
| GET              | `/records/{record_id}/tags/active/verbose`            | [docs/api/paths/records/tags/activeverbose.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/tags/activeverbose.yaml)                                         |
| GET              | `/records/{record_id}/notes`                          | [docs/api/paths/records/notes/get_notes.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/notes/get_notes.yaml)                                               |
| POST             | `/records/{record_id}/add_note`                       | [docs/api/paths/records/notes/add_note.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/notes/add_note.yaml)                                                 |
| DELETE           | `/records/{record_id}/remove_note/{note_id}`          | [docs/api/paths/records/notes/remove_note.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/notes/remove_note.yaml)                                           |
| GET              | `/records/{record_id}/urls`                           | [docs/api/paths/records/urls/fetch.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/urls/fetch.yaml)                                                         |
| POST             | `/records/{record_id}/urls/add`                       | [docs/api/paths/records/urls/add.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/urls/add.yaml)                                                             |
| POST             | `/records/{record_id}/urls/remove`                    | [docs/api/paths/records/urls/remove.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/urls/remove.yaml)                                                       |
| POST             | `/tags/create`                                        | [docs/api/paths/tags/create.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/create.yaml)                                                                       |
| GET              | `/tags/search`                                        | [docs/api/paths/tags/search.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/search.yaml)                                                                       |
| GET              | `/tags/autocomplete`                                  | [docs/api/paths/tags/autocomplete.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/autocomplete.yaml)                                                           |
| GET              | `/tags/{tag_id}/info`                                 | [docs/api/paths/tags/info.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/info.yaml)                                                                           |
| POST             | `/tags/domain/create`                                 | [docs/api/paths/tags/domains/create.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/domains/create.yaml)                                                       |
| GET              | `/tags/domain/list`                                   | [docs/api/paths/tags/domains/list.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/domains/list.yaml)                                                           |
| GET              | `/tags/domain/{tag_domain_id}/info`                   | [docs/api/paths/tags/domains/info.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/domains/info.yaml)                                                           |
| DELETE           | `/tags/domain/{tag_domain_id}/delete`                 | [docs/api/paths/tags/domains/delete.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/domains/delete.yaml)                                                       |
| GET              | `/tags/{domain_id}/{tag_id}/relationships`            | [docs/api/paths/tags/relationships/get.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/relationships/get.yaml)                                                 |
| POST             | `/tags/parents/create`                                | [docs/api/paths/tags/relationships/parent/create.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/relationships/parent/create.yaml)                             |
| POST             | `/tags/parents/remove`                                | [docs/api/paths/tags/relationships/parent/remove.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/relationships/parent/remove.yaml)                             |
| POST             | `/tags/alias/create`                                  | [docs/api/paths/tags/relationships/alias/create.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/relationships/alias/create.yaml)                               |
| POST             | `/tags/alias/remove`                                  | [docs/api/paths/tags/relationships/alias/remove.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/relationships/alias/remove.yaml)                               |
| POST             | `/tags/siblings/create`                               | [docs/api/paths/tags/relationships/sibling/create.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/relationships/sibling/create.yaml)                           |
| POST             | `/tags/siblings/remove`                               | [docs/api/paths/tags/relationships/sibling/remove.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/relationships/sibling/remove.yaml)                           |
| POST             | `/clusters/add`                                       | [docs/api/paths/clusters/add.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/clusters/add.yaml)                                                                     |
| GET              | `/clusters/list`                                      | [docs/api/paths/clusters/list.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/clusters/list.yaml)                                                                   |
| GET              | `/clusters/{cluster_id}/info`                         | [docs/api/paths/clusters/info.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/clusters/info.yaml)                                                                   |
| PATCH            | `/clusters/{cluster_id}/modify`                       | [docs/api/paths/clusters/modify.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/clusters/modify.yaml)                                                               |
| DELETE           | `/clusters/{cluster_id}/remove`                       | [docs/api/paths/clusters/remove.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/clusters/remove.yaml)                                                               |
| POST             | `/clusters/{cluster_id}/scan`                         | [docs/api/paths/clusters/scan.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/clusters/scan.yaml)                                                                   |
| GET, POST        | `/search`                                             | [docs/api/paths/search/search.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/search/search.yaml)                                                                   |
| POST             | `/file/import`                                        | [docs/api/paths/file/import.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/file/import.yaml)                                                                       |
| POST             | `/relationships/duplicates/add`                       | [docs/api/paths/relationships/duplicates/add.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/relationships/duplicates/add.yaml)                                     |
| GET              | `/relationships/duplicates/undecided`                 | [docs/api/paths/relationships/duplicates/undecided.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/relationships/duplicates/undecided.yaml)                         |
| POST             | `/relationships/alternatives/add`                     | [docs/api/paths/relationships/alternatives/add.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/relationships/alternatives/add.yaml)                                 |
| POST             | `/mime/parse`                                         | [docs/api/paths/mime/parse.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/mime/parse.yaml)                                                                         |
| POST             | `/mime/generate_thumbnail`                            | [docs/api/paths/mime/generate_thumbnail.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/mime/generate_thumbnail.yaml)                                               |
| POST             | `/jobs/metadata/rescan`                               | [docs/api/paths/jobs/rescan_metadata.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/jobs/rescan_metadata.yaml)                                                     |
| GET              | `/jobs/{job_id}/status`                               | [docs/api/paths/jobs/status.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/jobs/status.yaml)                                                                       |
| GET              | `/jobs/status`                                        | [docs/api/paths/jobs/status_all.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/jobs/status_all.yaml)                                                               |
| GET              | `/integrity`                                          | [docs/api/paths/integrity.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/integrity.yaml)                                                                           |
| POST             | `/purge/thumbnails`                                   | [docs/api/paths/purge/thumbnails.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/purge/thumbnails.yaml)                                                             |
| GET              | `/db/stats/sunburst`                                  | [docs/api/paths/db/stats/sunburst.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/db/stats/sunburst.yaml)                                                           |
| GET, POST        | `/downloader/secrets`                                 | [docs/api/paths/downloader/secrets.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/downloader/secrets.yaml)                                                         |
| GET              | `/health`                                             | [docs/api/paths/health.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/health.yaml)                                                                                 |
| GET              | `/auth/verify`                                        | [docs/api/paths/auth/verify.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/auth/verify.yaml)                                                                       |
| GET              | `/heartbeat`                                          | [docs/api/paths/heartbeat.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/heartbeat.yaml)                                                                           |
| POST             | `/records/metadata`                                   | [docs/api/paths/records/metadata.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/records/metadata.yaml)                                                             |
| GET              | `/db/stats`                                           | [docs/api/paths/db/stats.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/db/stats.yaml)                                                                             |
| GET              | `/mime`                                               | [docs/api/paths/mime.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/mime.yaml)                                                                                     |
| GET              | `/tags/info`                                          | [docs/api/paths/tags/info_query.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/tags/info_query.yaml)                                                               |
| GET              | `/relationships/{record_id}`                          | [docs/api/paths/relationships/record_id.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/relationships/record_id.yaml)                                               |
| GET              | `/relationships/{record_id}/similar`                  | [docs/api/paths/relationships/record_id/similar.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/relationships/record_id/similar.yaml)                               |
| POST             | `/relationships/clear`                                | [docs/api/paths/relationships/clear.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/relationships/clear.yaml)                                                       |
| POST             | `/relationships/unrelated/add`                        | [docs/api/paths/relationships/unrelated/add.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/relationships/unrelated/add.yaml)                                       |
| GET, POST        | `/layouts`                                            | [docs/api/paths/layouts.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/layouts.yaml)                                                                               |
| GET, PUT, DELETE | `/layouts/{id}`                                       | [docs/api/paths/layouts/id.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/layouts/id.yaml)                                                                         |
| GET              | `/embeddings/models`                                  | [docs/api/paths/embeddings/models.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/embeddings/models.yaml)                                                           |
| DELETE           | `/embeddings/models/{model_id}`                       | [docs/api/paths/embeddings/models/model_id.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/embeddings/models/model_id.yaml)                                         |
| POST             | `/embeddings/generate`                                | [docs/api/paths/embeddings/generate.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/embeddings/generate.yaml)                                                       |
| POST             | `/embeddings/search`                                  | [docs/api/paths/embeddings/search.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/embeddings/search.yaml)                                                           |
| POST             | `/embeddings/compare`                                 | [docs/api/paths/embeddings/compare.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/embeddings/compare.yaml)                                                         |
| GET, POST        | `/download_sessions`                                  | [docs/api/paths/download_sessions.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/download_sessions.yaml)                                                           |
| GET, DELETE      | `/download_sessions/{session_id}`                     | [docs/api/paths/download_sessions/session_id.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/download_sessions/session_id.yaml)                                     |
| POST, GET        | `/download_sessions/{session_id}/urls`                | [docs/api/paths/download_sessions/session_id/urls.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/download_sessions/session_id/urls.yaml)                           |
| POST             | `/download_sessions/urls`                             | [docs/api/paths/download_sessions/urls.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/download_sessions/urls.yaml)                                                 |
| POST             | `/download_sessions/{session_id}/urls/{url_id}/retry` | [docs/api/paths/download_sessions/session_id/urls/url_id/retry.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/download_sessions/session_id/urls/url_id/retry.yaml) |
| GET              | `/download_sessions/{session_id}/records`             | [docs/api/paths/download_sessions/session_id/records.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/download_sessions/session_id/records.yaml)                     |
| GET              | `/download_sessions/{session_id}/errors`              | [docs/api/paths/download_sessions/session_id/errors.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/download_sessions/session_id/errors.yaml)                       |
| GET              | `/download_sessions/events`                           | [docs/api/paths/download_sessions/events.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/download_sessions/events.yaml)                                             |
| GET              | `/rate_limits`                                        | [docs/api/paths/rate_limits.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/rate_limits.yaml)                                                                       |
| POST             | `/rate_limits/reset`                                  | [docs/api/paths/rate_limits/reset.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/rate_limits/reset.yaml)                                                           |
| GET              | `/rate_limits/events`                                 | [docs/api/paths/rate_limits/events.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/rate_limits/events.yaml)                                                         |
| GET              | `/downloader/debug`                                   | [docs/api/paths/downloader/debug.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/downloader/debug.yaml)                                                             |
| GET              | `/plugins`                                            | [docs/api/paths/plugins.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/plugins.yaml)                                                                               |
| GET              | `/log`                                                | [docs/api/paths/log.yaml](/home/kj16609/Desktop/Projects/cxx/IDHAN/docs/api/paths/log.yaml)                                                                                       |

The external Pawchive description additionally contains 17 paths / 20 operations; its runtime behavior is outside this
repository and remains unverified.

## Approved follow-up: cluster root markers and repeatable acknowledged recovery

G01 now also checks physical directory identity and uniquely registered marker
UUIDs. Migration 416 assigns stable UUIDs; newly registered roots automatically
receive `.idhan-cluster.json`, including logically read-only roots. Existing
valid marker fields remain untouched. Copies of a registered identity conflict.
The previous name and lexical path checks remain in place.

Startup verifies all registered roots and exits on missing, invalid, or mismatched
markers. After checking the intended mounts, users can set
`[cluster] recreate_missing_markers = true` in writable TOML. The flag is durably
reset before attempting repairs, including when a subsequent repair fails.
Recovery recreates missing directories/markers with their database UUIDs; it
never overwrites an existing conflicting marker. This acknowledgement can be
repeated after future folder loss. It restores identity, not missing media.

Verified root handles cover imports, original downloads, metadata parsing,
thumbnail generation, embeddings, and scans. Runtime identity loss makes file
access unavailable and must not be mistaken for missing media or trigger a
fallback write. Scans recheck identity before later mutations. Cached thumbnails
and registration inspection/removal remain possible without media access.
See [implementation and recovery details](cluster-identity-proposal.md).

Validation: server compilation/link succeeded in separate verification artifacts;
marker unit tests and API regressions cover atomic creation, extensions, aliases,
copied identities, missing/mismatched markers, pinned directory handles, persisted
flag reset, and retained record assignments. The broader API suite passed 868
assertions in 38 cases. A separate startup exercise passed 23 checks across ten
launches, including legacy roots, complete root loss, read-only configuration,
conflicting UUIDs, and a configuration directory that prevents persisted reset.

Whole-file changelists cannot split earlier edits in shared files. The marker
changelist includes the files needed for this behavior and preserves their prior
approved API, configuration, and test-wiring changes. The shared review report
remains with API documentation. No Pawchive input changes or commits are made.
