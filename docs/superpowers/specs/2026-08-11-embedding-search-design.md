# Embedding search

Search the collection by meaning rather than by tag: a query is assembled from weighted text phrases
and weighted reference records, positive or negative, and resolved against the per-model embedding
tables added by the image-embeddings work.

The reference implementation is `/home/kj16609/Desktop/Projects/cxx/LanceProject` — a working PyQt
prototype over the same SigLIP2 models. Where this document says "the reference does X", that is what
is being described. This design follows its maths exactly and diverges only where IDHAN's storage and
process model demand it.

## Scope

In:

- A text tower exported alongside the existing image tower, so a phrase becomes a vector.
- `EmbeddingModuleI::embedText`, and the IPC to reach it.
- `POST /embeddings/search`: signed weighted sum of terms, nearest neighbours, top K.
- An HNSW index on every per-model embedding table.
- An `Embedding Search` web panel publishing to the shared result set.

Out, deliberately:

- **Tag filtering.** Composing embedding search with `SearchBuilder` predicates is a follow-up. Both
  ways of doing it (pre-filter in the `WHERE`, post-filter by intersection) trade recall against
  speed, and the trade-off depends on collection size and on how selective real queries turn out to
  be. Neither is measurable yet, so choosing now would be guessing.
- **Uploaded reference images.** Reference images must already be records. Their vectors are then a
  table lookup, and phase 1 needs no multipart handling, no temp files, and no per-reference module
  call.
- **Relevance feedback.** The reference nudges the query using vectors from results the user marks
  good or bad (`GUI.py`, `feedback_weight`, line 199). It composes cleanly with the term model below
  and is worth having later; it needs result-marking UI and is independent of everything here.

## Background: what a query is

From `HyCLIP_Model.py` and `GUI.py:203-250`, a query vector is a signed weighted sum of unit vectors
drawn from two sources — `tokenize_text(phrase)` and `eval_image(path)` — followed by an optional
normalisation:

```
final = Σ (vec_i · w_i)      where w_i = +weight if positive else -weight
```

That is the whole of it. `catgirl:0.5` is one text term at weight 0.5. A negative reference image is
the same operation with the sign flipped. Everything in this design exists to produce that vector and
then find its neighbours.

Two consequences worth stating up front, because both are easy to get wrong later:

- **Normalising the query cannot change the result order.** Cosine distance is scale-invariant in the
  query vector, so the reference's `normalize` checkbox is, under cosine, a no-op for ranking. We
  normalise anyway so the returned distances are interpretable, but this must not be exposed as a
  user-facing toggle implying it affects results.
- **Weights can cancel to a zero vector.** Cosine distance against zero is undefined and pgvector
  yields NaN, which sorts arbitrarily rather than failing. This is rejected explicitly.

## Section 1: the text tower

### The blocker

`export_siglip2.py` wraps `encode_image` only. There is no text tower in the exported graph and no
tokenizer beside it, so today there is no path from a phrase to a vector. SigLIP2's text side uses the
Gemma tokenizer: BPE, 256k vocab, a 34 MB `tokenizer.json`, `context_length: 64`, and
`tokenizer_kwargs: {clean: canonicalize}` (`open_clip_config.json`).

A tokenizer that is subtly wrong yields a plausible vector and no error anywhere. This is the central
risk of the feature, and the design below is shaped around making it loud.

### Approach: tokenizer inside the graph

The text tower is exported with the tokenizer baked in as an `onnxruntime-extensions` custom op, so
the ONNX graph takes a string and emits a vector. Tokenizer correctness then lives in Python, where it
is verified once at export against the library that trained the model, rather than being
re-implemented in C++.

**This is gated by a spike** (see Plan of work). `onnxruntime-extensions` is not packaged on Arch and
is absent from the reference project's venv — only base `onnxruntime` 1.28 is present — so the spike
must confirm two separate things:

1. The extensions tokenizer ops cover this Gemma tokenizer.
2. `libortextensions.so` can be obtained and loaded from C++ via `RegisterCustomOpsLibrary`. It ships
   in the pip wheel; the Dockerfile's existing `modelbuilder` stage can copy it into the runtime image
   the same way it already copies models. Local non-Docker development needs it fetched too.

If either fails, the fallback is `tokenizers-cpp` (MLC's wrapper over the HuggingFace Rust tokenizers
library), which reads `tokenizer.json` directly and is bit-identical to Python by construction, at the
cost of a Rust toolchain in the build. **Only this section changes** if the fallback is taken; sections
2 and 3 are unaffected.

### Export

`export_siglip2.py --with-text` emits `text.onnx` beside `model.onnx`. A `TextEncoder` wrapper mirrors
the existing `ImageEncoder`: `encode_text` with the L2 normalisation folded into the graph, for the
reason the existing wrapper already gives — no consumer can then forget it.

`model.json` gains:

```json
"text_onnx": "text.onnx",
"text_input_name": "text",
"text_output_name": "text_features",
"context_length": 64
```

### Parity sidecar

The export also writes `text_parity.json`: a handful of fixed phrases with the vectors Python produced
for them.

At startup the module embeds those same phrases and compares. A mismatch beyond tolerance disables the
text tower with a loud error rather than serving quietly-wrong vectors. This is the only mechanism that
converts the tokenizer risk into a visible failure, and it follows the same instinct as the existing
norm and output-dimension checks in `OnnxEmbedder::startup`.

Tolerance follows `NORM_TOLERANCE` reasoning: loose enough for fp accumulation differences across
runtimes, tight enough to catch a different tokenisation, which moves a vector far more than rounding
does.

### Module interface

`EmbeddingModuleI` gains:

```cpp
//! Whether this model has a text tower. False is a normal configuration, not an error.
[[nodiscard]] virtual bool supportsText() = 0;

//! Embeds a phrase into the same space as embed(). Takes no file, so no file plumbing applies.
[[nodiscard]] virtual std::expected< EmbeddingInfo, ModuleError > embedText( std::string_view phrase ) = 0;
```

Text support is **optional at runtime**. A model with no `text.onnx`, a missing extensions library, or
a failed parity check still registers and still serves record-reference search; only text terms are
refused, naming the reason. This matches how the build already treats onnxruntime itself — absent means
a reduced feature set, not a failure.

`embedText` runs one forward pass per call, consistent with the CPU-only, no-batching decision already
taken for the image path.

### IPC

A new `ipc::CallOp::EMBED_TEXT` carries the phrase in the body and returns the vector inline as JSON,
exactly as `EMBED` does and for the same reason: a vector is a few KiB, and a memfd per call would cost
more than the text does.

This is the first call **with no file attached**, which is a real change rather than just a new enum
value:

- `WorkerRunner::runCall` builds `ModuleCallData` from `call.file`, and `dispatchCall` expects a
  descriptor. Both need a fileless path.
- `requiredFlag( call.op )` gains the `EMBEDDING` mapping for the new op.
- `describeResult` gains an `EMBED_TEXT` arm.

`RemoteModule` gains a matching `embedText( std::string )` coroutine.

## Section 2: database and endpoint

### Migration

`IDHANMigration/src/func_create_embedding_table/192-hnsw.sql` (191 is the current highest).

Two parts, both required:

1. `CREATE OR REPLACE` the trigger function so newly registered models get an index with their table.
2. A `DO` block adding the index to `embeddings_*` tables the **old** trigger already created. Without
   this, the feature only works for models registered after the upgrade.

```sql
CREATE INDEX IF NOT EXISTS embeddings_%s_hnsw
    ON embeddings_%s USING hnsw (embedding halfvec_cosine_ops);
```

Notes:

- Not `CONCURRENTLY`: illegal inside a trigger, and the table is empty at creation time. In the
  backfill `DO` block the table may be populated, and a plain `CREATE INDEX` takes a write lock for
  its duration — acceptable during startup migration.
- `halfvec_cosine_ops`: vectors are unit-norm, so cosine and inner product rank identically. Cosine is
  chosen because it remains correct if a norm ever drifts.
- **Dimension ceiling.** pgvector's HNSW limit for `halfvec` is 4000 dimensions, but
  `embedding_models` already `CHECK`s up to 16000. 768 (ViT-B-16-SigLIP2) and 1152 (ViT-SO400M) are
  both fine. Both the trigger and the backfill `DO` block create the index only when
  `model_dimensions` permits, and warn otherwise, so an oversized model still works — unindexed and
  slow — rather than failing to register or aborting the migration.
- Index maintenance cost lands on backfill inserts. This is the accepted trade for never having a
  model that is registered but unsearchable.

### Query assembly

New `IDHANServer/src/embeddings/searchEmbeddings.{hpp,cpp}`, kept out of `embeddings.cpp` so backfill
and search stay separately readable.

1. Resolve model name to `model_id` and dimensions; 404 if unregistered.
2. Partition terms into text and record.
3. Record terms: one query for all of them.
   ```sql
   SELECT record_id, embedding FROM embeddings_1 WHERE record_id = ANY($1);
   ```
   A record with no embedding for this model is a **400 naming that record**, not a silent skip —
   dropping a reference silently changes the query the user asked for.
4. Text terms: `co_await module->embedText( phrase )` each, after checking `supportsText()`.
5. Signed weighted sum, then normalise. Reject a zero-magnitude result with 400.
6. Nearest neighbours:
   ```sql
   SELECT record_id, embedding <=> $1::halfvec AS distance
   FROM embeddings_1 ORDER BY distance LIMIT $2;
   ```

`hnsw.ef_search` is set per query via `SET LOCAL` from an optional clamped request field, so recall can
be traded against latency without a server restart.

### Endpoint

`POST /embeddings/search`, added to `EmbeddingAPI`.

```json
{
  "model_name": "ViT-B-16-SigLIP2",
  "terms": [
    { "type": "text",   "text": "catgirl", "weight":  0.5 },
    { "type": "record", "record_id": 4021, "weight":  1.0 },
    { "type": "record", "record_id": 77,   "weight": -0.5 }
  ],
  "limit": 200,
  "ef_search": 100
}
```

Response: `{ "record_ids": [...], "distances": [...], "query_ms": 12 }`.

`GET /embeddings/models` gains a `supports_text` field per model, alongside the existing `available`.
The panel cannot otherwise know whether to enable its text input, and discovering the answer by
submitting a query that fails with a 400 is not an acceptable substitute. It is derived the same way
`available` is — from the loaded module — and is `false` whenever no module provides the model.

The class comment on `EmbeddingAPI` currently reads "Searching over the vectors is deliberately absent";
it is replaced as part of this work.

**Synchronous, not a job.** The text tower is 12 layers over at most 64 tokens and the embedding worker
is `PERSISTENT`, so its session is warm; the whole call is tens of milliseconds. Routing this through
the job system would add poll latency to something that should feel immediate. This differs from
backfill, which is genuinely long-running and correctly a job.

Error cases:

| Condition | Status |
| --- | --- |
| Unknown or unregistered model | 404 |
| No loaded module provides the model, and any text term present | 404 |
| Empty term list, or every term disabled | 400 |
| Text term against a model with no text tower | 400, naming the model |
| Record term whose record has no embedding for this model | 400, naming the record |
| Terms cancel to a zero-magnitude vector | 400 |
| Registered dimensions disagree with the loaded module | 409 (existing check) |

`limit` is clamped, following the existing handler conventions in `createBadRequest.hpp`. JSON types
are checked with `isString`/`isArray`/`isIntegral` before any `as*()` call, per the handler conventions
in CLAUDE.md.

### Term syntax

Text terms are free text and never touch `splitTag` or the tag tables — this is a separate system from
tag search, and the two do not interact.

Within a single typed term, the weight is parsed as: an optional leading `-` for a negative term, and a
trailing `:<number>` **only when what follows the final colon parses as a number**. Everything else is
the phrase verbatim.

```
catgirl:0.5                 ->  +0.5  "catgirl"
character:hatsune miku:0.8  ->  +0.8  "character:hatsune miku"
-blurry:0.3                 ->  -0.3  "blurry"
catgirl                     ->  +1.0  "catgirl"
rating:safe                 ->  +1.0  "rating:safe"
```

## Section 3: web panel

`IDHANWeb/src/panels/builtins/EmbeddingSearchPanel.tsx`, registered in `builtins/index.ts` beside the
rest.

```ts
export const embeddingSearchPanel = {
  type: 'embedding-search',
  title: 'Embedding Search',
  description: 'Search by meaning: weighted text phrases and reference records.',
  component: EmbeddingSearchPanel,
  defaultConfig: DEFAULT_CONFIG,
  configVersion: 1,
} as const;
```

It publishes through the same contract as the tag search panel —
`host.results.set({ ids, queryMs, query })` — so the grid, viewer, and info panels page against
embedding results with no changes at all. `ids` is an `Int32Array` per `SearchResultSet`; `query`
carries human-readable tokens (`catgirl:0.5`, `-record:4021`) for the results header.

One term model backs both kinds, mirroring the reference's `PromptRow` and `ReferenceImageItem`, which
both carry weight, sign, and an enabled flag:

```ts
type Term =
  | { kind: 'text';   text: string;     weight: number; positive: boolean; enabled: boolean }
  | { kind: 'record'; recordId: number; weight: number; positive: boolean; enabled: boolean };
```

Behaviour:

- Model selector from `GET /embeddings/models`. A model reporting no text tower disables the text input
  rather than hiding it, showing why.
- Reference records are added from `host.selection.get()` via an "Add selection as reference" button —
  the natural IDHAN gesture for "these images", and the reason records-only costs no upload plumbing.
- Each term row has a weight control, a sign toggle, an enable checkbox, and a remove button. Typing
  the shorthand adds a row; the row is then the truth.
- Terms and the selected model live in the persisted panel config, so a tuned query survives reload.

## Testing

In order of what each actually protects:

1. **Parity** — the module's `embedText` against the vectors recorded by the export script. This is the
   only test that catches tokenizer drift, which is the one failure mode here that produces no error
   signal of its own.
2. **Integration without ONNX** — seed known vectors directly into `embeddings_N` through
   `ServerDBFixture`, then `POST /embeddings/search` with a record term and assert the ordering,
   including that the reference record itself returns first at distance ≈ 0. Requires no model and no
   onnxruntime, so it runs in any environment.
3. **Unit** — the `:weight` parse rule (including the two-colon and no-number cases above), the signed
   weighted sum, and zero-vector rejection.
4. **Migration** — the index exists both on a newly registered model and on a table the old trigger had
   already created.

## Plan of work

Ordered so that the risky, gating item comes first and nothing downstream is built on an unverified
assumption.

1. **Spike: `onnxruntime-extensions`.** Export the SigLIP2 text tower with the tokenizer baked in;
   load it from a throwaway C++ binary; compare a few phrases against Python. Decide extensions vs
   `tokenizers-cpp` on the result. Nothing else starts until this resolves.
2. **Migration 192** — HNSW on new and existing tables. Independent of the spike.
3. **Export script** — `--with-text`, `model.json` fields, parity sidecar.
4. **Module** — `supportsText`, `embedText`, parity check at startup, graceful degradation.
5. **IPC** — `EMBED_TEXT`, the fileless call path, `RemoteModule::embedText`.
6. **Server** — `searchEmbeddings`, `POST /embeddings/search`.
7. **Panel** — `EmbeddingSearchPanel`, registration.
8. **Tests** — as above; 2 and 3 land with the code they cover.

Steps 2 and 6 are testable end-to-end with seeded vectors before any of the text work exists, so
record-reference search can be proven independently of the spike outcome.
