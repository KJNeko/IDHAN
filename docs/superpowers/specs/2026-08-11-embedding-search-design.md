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

- Loading a HuggingFace ONNX model clone directly, so setup is one `git clone` and nothing else.
- A BPE tokenizer reading the clone's own `tokenizer.json`, so a phrase becomes a vector.
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

### The problem

`export_siglip2.py` wraps `encode_image` only, so there is no text tower in the exported graph and no
tokenizer beside it. Today there is no path from a phrase to a vector.

### Setup is the governing constraint

Model setup is one `git clone` into the models directory, and nothing else:

```
git clone https://huggingface.co/onnx-community/siglip2-base-patch16-224-ONNX
```

Nothing to install, nothing to convert, nothing to run. This matches how the LanceProject prototype is
set up -- `models/ViT-B-16-SigLIP2` is literally a clone of `https://huggingface.co/timm/ViT-B-16-SigLIP2`
-- while being reachable from C++, which that clone is not.

**ONNX is the only accepted format.** A clone must contain the graphs; IDHAN never converts anything.

### Why the canonical repositories cannot be used directly

`timm/ViT-B-16-SigLIP2` and `google/siglip2-base-patch16-224` ship `open_clip_model.safetensors` and
`.bin` files -- bags of named tensors, with nothing describing what multiplies what, in what order.
Python can use them because open_clip *constructs* the architecture (`factory.py:139` reads
`open_clip_config.json`, builds the PyTorch modules, then pours the weights in). ONNX Runtime cannot:
it executes a graph that already exists.

Converting one requires torch, which would put roughly 3 GB of Python into the runtime image to serve
a step that only ever runs once per model. `onnx-community` has already done that conversion, so the
clone contains the result.

### The model

`onnx-community/siglip2-base-patch16-224-ONNX`, converted from `google/siglip2-base-patch16-224`:

```
onnx/vision_model.onnx      372 MB   (+ fp16 / int8 / q4 / q4f16 variants)
onnx/text_model.onnx       1.13 GB   (+ the same variants)
tokenizer.json            34.4 MB
tokenizer.model           4.24 MB    (sentencepiece proto; unused)
config.json
preprocessor_config.json
```

Separate vision and text towers, which is already this system's two-call shape, plus the tokenizer and
the preprocessing parameters. 768 dimensions, 224x224 input -- the same shape the image tower already
handles.

Quantized variants come free: `text_model_q4f16.onnx` at 443 MB against 1.13 GB is a real option for a
CPU-only deployment, selected by config rather than by re-exporting anything.

`export_siglip2.py` leaves the setup path entirely. It is retained only as optional tooling, for
generating parity fixtures (below) and for anyone preparing a model directory by hand.

This also empties the Dockerfile's `modelbuilder` stage of its heaviest content. Today that stage
installs torch and open_clip, downloads a 1.5 GB checkpoint, and runs an export
(`Dockerfile:138-172`). It becomes a plain fetch of the already-converted repository, so the build
stops depending on PyTorch at all.

A model directory without `onnx/*.onnx` is skipped exactly as an unloadable module library is: logged
with the reason, never fatal.

### Rejected alternatives, and why

On tokenization:

- **`onnxruntime-extensions`** (tokenizer inside the graph): not packaged on Arch, and ships
  `libortextensions.so` only inside a pip wheel, so every developer and image must extract it from
  Python packaging by hand.
- **`tokenizers-cpp`**: bit-identical to Python by construction, but puts a Rust toolchain in the
  build for every developer, CI job and Docker stage.
- **`sentencepiece`**: the natural fit, since Gemma's tokenizer is SentencePiece-based and the clone
  even ships `tokenizer.model` -- but it is not in the Arch repositories.

On inference:

- **ggml with a safetensors reader**, as `stable-diffusion.cpp` does -- which would let a `timm/`
  clone run with no Python, no ONNX and no third-party repository at all. Rejected on where it puts
  the model definition: ONNX keeps the architecture *in the file*, so a new model is a new file and
  no new code, while ggml keeps it *in our source*, so every architecture is 1,500-2,000 lines of
  numerically sensitive C++ to write and then maintain.

  It also doubles the verification problem rather than reducing it. The tokenizer is already
  hand-written and can be silently wrong; a hand-written model has the identical failure signature --
  a subtly wrong attention mask or pooling head yields plausible vectors and no error -- and there is
  no free parity source to check either against. Worth revisiting only if published ONNX conversions
  stop being available for the models IDHAN wants.

### Discovery, not configuration

Everything is read from the clone, and where the clone is ambiguous, **from the ONNX graph itself**,
which is authoritative in a way a config file is not.

| Fact | Source |
| --- | --- |
| Output dimensionality | text/vision graph output shape (already done for the image tower) |
| Image geometry | vision graph input shape `[N, 3, H, W]`, cross-checked against `preprocessor_config.json` |
| Mean / std / rescale | `preprocessor_config.json` |
| Context length | text graph input shape `[N, L]`; 64 when that axis is dynamic |
| Input / output tensor names | `Session::GetInputNameAllocated` / `GetOutputNameAllocated` |
| Tokenizer | `tokenizer.json` |

Reading names from the graph rather than hard-coding them is what lets a differently-exported model
work without new C++.

`config.json` is deliberately **not** relied upon for geometry. The onnx-community config is minimal --
`hidden_size`, `image_size`, `patch_size` and `max_position_embeddings` are all absent, inherited from
transformers' Python-side class defaults, which are not in the repository at all. A reader that trusted
it would have to hard-code another project's defaults.

`preprocessor_config.json` confirms the image path already matches: `size` 224x224, `image_mean` and
`image_std` all 0.5, `rescale_factor` 1/255, and a resize to exactly `height` x `width` with no aspect
preservation -- which is the `resize_mode: force` the module already requests.

### What the tokenizer must do

From `tokenizer.json` and `tokenizer_config.json` in the clone:

1. **Clean**: lowercase (`do_lower_case: true`), and strip ASCII punctuation. Whitespace is collapsed
   and trimmed.
2. **Normalize**: replace `" "` with `"▁"`.
3. **Pre-tokenize**: split on `" "`, `MergedWithPrevious`.
4. **BPE**: 256,000 vocabulary entries, 580,604 ranked merges, `byte_fallback: true` -- an unknown
   character becomes its `<0xNN>` byte tokens rather than a single unk.
5. **Terminate and pad**: append `<eos>` (`add_eos_token: true`, `add_bos_token: false`), then pad
   right with `<pad>` to the context length.

`model_max_length` in `tokenizer_config.json` is the `1e30` sentinel and carries no information; the
context length comes from the graph.

Steps 1, 2, 3 and 5 are a few dozen lines. Step 4 is the standard ranked-merge algorithm. The
preprocessing is described by the model rather than hard-coded -- `clean` selects `canonicalize`,
`lower` or `none`, and the normalizer, pre-tokenizer and byte-fallback are all read from
`tokenizer.json` -- so a differently-tokenized model ships a different description instead of needing
new C++.

### The honest risk: parity has no free source

A hand-written tokenizer that is subtly wrong produces plausible vectors and no error. The earlier
design answered that with a parity sidecar written by the export script. **Git-clone-only removes that
answer**, because there is no export step to generate one.

Worse, there is a specific known ambiguity. `tokenizer_class` here is `GemmaTokenizer`, not
`SiglipTokenizer`, and `do_lower_case: true` is stated while punctuation stripping is not. open_clip's
`canonicalize_text` (`tokenizer.py:104`) removes punctuation; whether the transformers path does the
same for SigLIP2 is not answerable from the files in the clone.

Three mitigations, in order of value:

1. **Parity fixtures checked into IDHAN**, not into the clone: for each model IDHAN is tested against,
   a small file of phrases with their expected token ids and vectors. Verified at startup exactly as
   the sidecar would have been. Present for known models, absent for others -- so its absence is a
   warning, never a failure, or an unknown model could not be used at all.
2. **A tokenization diagnostic**: `POST /embeddings/tokenize` returning the token ids and their
   decoded pieces for a phrase, so a mismatch can be found by eye and compared against Python in
   seconds rather than inferred from bad search results.
3. **The export script, retained**, which can still generate a parity fixture for any model where
   Python and torch are available. Optional tooling, off the setup path.

This is a real reduction in safety compared with the export-generated sidecar, accepted deliberately in
exchange for setup being one `git clone`.

### Module interface

`EmbeddingModuleI` gains:

```cpp
//! Whether this model has a text tower. False is a normal configuration, not an error.
[[nodiscard]] virtual bool supportsText() = 0;

//! Embeds a phrase into the same space as embed(). Takes no file, so no file plumbing applies.
[[nodiscard]] virtual std::expected< EmbeddingInfo, ModuleError > embedText( std::string_view phrase ) = 0;
```

Text support is **optional at runtime**. A model with no text graph, an unparseable `tokenizer.json`,
or a failed parity check still registers and still serves record-reference search; only text terms are
refused, naming the reason. This matches how the build already treats onnxruntime itself -- absent
means a reduced feature set, not a failure.

Both are defaulted rather than pure, so an existing embedding module keeps compiling unchanged.

### IPC

A new `ipc::CallOp::EMBED_TEXT` carries the phrase in the body and returns the vector inline as JSON,
exactly as `EMBED` does and for the same reason: a vector is a few KiB, and a memfd per call would cost
more than the text does.

This is the first call **with no file attached**, which is a change to the IPC layer rather than just a
new enum value:

- `WorkerRunner::handleCall` must skip `adoptInput` for it.
- `WorkerRunner::invoke` must answer it *before* building `ModuleCallData`, which holds a
  `ModuleFile&` bound from `*call.file` -- null for this op.
- `WorkerProcess::call` rejected a null input outright and must stop doing so.
- `requiredFlag` gains the `EMBEDDING` mapping; `describeResult` gains an arm.

`RemoteModule` gains a matching `embedText( std::string )` coroutine and a `supportsText()` accessor
fed from the manifest.

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

1. **Tokenizer parity** — the module's tokenization and `embedText` against fixtures checked into
   IDHAN, comparing token ids exactly and vectors within tolerance. This is the only thing that
   catches tokenizer drift, the one failure mode here that produces no error signal of its own. Ids
   and vectors are compared separately because they fail for different reasons: ids differing means
   the C++ tokenizer is wrong, ids matching while vectors differ means the graph is.
2. **Tokenizer units, no model needed** — the clean/normalize/pre-tokenize steps and byte-fallback
   against hand-written expectations, so the cheap half of the tokenizer is covered without a
   1.5 GB download in the loop.
3. **Integration without ONNX** — seed known vectors directly into `embeddings_N` through
   `ServerDBFixture`, then `POST /embeddings/search` with a record term and assert the ordering,
   including that the reference record itself returns first at distance ≈ 0. Requires no model and no
   onnxruntime, so it runs in any environment.
4. **Unit** — the `:weight` parse rule (including the two-colon and no-number cases above), the signed
   weighted sum, and zero-vector rejection.
5. **Migration** — the index exists both on a newly registered model and on a table the old trigger had
   already created.

## Plan of work

Ordered so the record-reference half -- which needs no model at all -- lands and is provable first,
and the tokenizer, which is the only genuinely risky part, comes last with its verification attached.

1. **Migration 192** — HNSW on new and existing tables. *(done)*
2. **Query vector assembly** — header-only, so it is testable without linking the server. *(done)*
3. **Server** — `searchEmbeddings`, `POST /embeddings/search`, record terms. *(done)*
4. **IPC** — `EMBED_TEXT`, the fileless call path, `RemoteModule::embedText`. *(done)*
5. **Text terms in search** — `supports_text` on `GET /embeddings/models`. *(done)*
6. **Panel** — `EmbeddingSearchPanel`, registration. *(done)*
7. **Model discovery** — locate `onnx/vision_model.onnx` and `onnx/text_model.onnx` in the clone,
   honouring a configured quantization suffix. Read geometry and tensor names from the graphs, and
   mean/std from `preprocessor_config.json`. `model.json` becomes an optional override rather than a
   requirement -- a file the clone does not contain cannot be mandatory. A directory with no graphs is
   skipped with the reason logged.
8. **Tokenizer** — parse `tokenizer.json`; implement clean, normalize, pre-tokenize, BPE with
   byte-fallback, eos and padding. Unit-tested against hand-written expectations.
9. **Text tower in the module** — `supportsText`, `embedText`, parity fixtures checked at startup,
   graceful degradation to image-only on any failure.
10. **Tokenization diagnostic** — `POST /embeddings/tokenize`, so a mismatch is findable by eye rather
    than inferred from bad search results.

Steps 1-6 are complete and record-reference search works today. Steps 7-9 are what `catgirl:0.5`
needs; step 10 exists because step 8 is hand-written and will eventually be wrong about something.
