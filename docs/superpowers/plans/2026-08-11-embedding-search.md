# Embedding Search Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Search the collection by meaning — a query built from weighted text phrases and weighted reference records, positive or negative, resolved to nearest neighbours over the per-model embedding tables.

**Architecture:** A query vector is a signed weighted sum of unit vectors, then normalised. Record vectors come from a single SQL lookup; text vectors come from a new text tower reached through a new `EMBED_TEXT` module call. Neighbours come from pgvector HNSW using cosine distance. The web panel publishes to the same shared result set the tag search already uses, so the grid and viewer need no changes.

**Tech Stack:** C++23, Drogon coroutines, libpqxx/Drogon ORM, PostgreSQL + pgvector (`halfvec`, HNSW), ONNX Runtime 1.28 (+ `onnxruntime-extensions`, pending Task 1), open_clip/PyTorch for export, React + Vitest.

**Spec:** `docs/superpowers/specs/2026-08-11-embedding-search-design.md`

> **STATUS — read before executing.**
>
> Tasks 2, 3, 4, 5, 8 and 9 are **implemented** (commits `e3c334c5`, `111fbf90`, `b01c7df6`). Their
> test steps were skipped at the user's direction, so they are compile-verified only.
>
> Tasks **1, 6 and 7 are superseded** and must not be executed as written. They assume the model is
> produced by `export_siglip2.py` and that the tokenizer lives inside the ONNX graph via
> `onnxruntime-extensions`. The governing constraint has since changed to *setup is one `git clone`
> of a HuggingFace ONNX repository*, which removes the export step from the setup path entirely.
> See "Section 1: the text tower" in the spec for what replaces them — model discovery from the
> clone, a BPE tokenizer over the clone's own `tokenizer.json`, and parity fixtures checked into
> IDHAN rather than generated at export.
>
> Replacement tasks have not been written yet.

## Global Constraints

- All integer IDs use the aliases in `IDHAN/include/IDHANTypes.hpp` (`RecordID`, etc.), never raw integers.
- API handlers are `drogon::HttpController` subclasses in `namespace idhan::api`; the last argument to `ADD_METHOD_TO` must be `IDHANAPIAuthName`.
- Check `isString()`/`isArray()`/`isIntegral()` before any jsoncpp `as*()` call — `asString()` throws on the wrong type and surfaces as a 500 where a 400 belongs.
- Initialise array responses as `Json::Value json { Json::arrayValue }`; a default-constructed `Json::Value` serialises as `null`, not `[]`.
- Response helpers live in `api/helpers/createBadRequest.hpp`: `createBadRequest` (400), `createNotFound` (404), `createConflict` (409), `createInternalError` (500). All take `std::format`-style strings.
- Migration files are `IDHANMigration/src/<dir>/N-<name>.sql`, executed in ascending numeric order. **191 is the current highest.**
- pgvector HNSW ceiling for `halfvec` is **4000 dimensions**; `embedding_models` CHECKs up to 16000.
- Never use emojis anywhere — code, UI, comments, or commit messages.
- Do not commit or push unless explicitly asked; the steps below say `git commit` but stop short of pushing.
- Build with an existing tree: `cmake --build build/debug --target <target>`. Do not pass `-j`.
- Style: tabs for indentation, `{ }` brace-initialisation, `//!` and `/** */` Doxygen comments. Match surrounding code.

### Known pre-existing breakage

`tests/src/profiling/fiberNameTest.cpp` fails to compile — `idhan_tracy/CoroFiber.hpp` is missing. This predates this work and blocks the whole `IDHANTests` target. Task 4's integration test therefore cannot be run until that is fixed independently. `IDHANCoreTests` (globs `tests/core/`) is unaffected and is where Task 3's unit tests go.

---

### Task 1: Spike — can the tokenizer live inside the ONNX graph?

**This task gates Tasks 5, 6 and 7 only.** Tasks 2, 3 and 4 are independent and may proceed in parallel.

This is an investigation, not a TDD task. Its deliverable is a written finding, not shipped code.

**Files:**
- Create: `docs/superpowers/specs/2026-08-11-embedding-search-spike.md`

**Interfaces:**
- Consumes: nothing.
- Produces: a decision — `extensions` or `tokenizers-cpp` — that Tasks 6 and 7 read.

**Background:** SigLIP2's text side uses the Gemma tokenizer: BPE, 256k vocab, 34 MB `tokenizer.json`, `context_length: 64`, `tokenizer_kwargs: {clean: canonicalize}`. `onnxruntime-extensions` is NOT packaged on Arch and is NOT in the reference venv at `/home/kj16609/Desktop/Projects/cxx/LanceProject/.venv`. Only base `onnxruntime` 1.28 is installed system-wide.

- [ ] **Step 1: Install the export-side dependency**

```bash
/home/kj16609/Desktop/Projects/cxx/LanceProject/.venv/bin/pip install onnxruntime-extensions
```

Expected: installs successfully. Record the version.

- [ ] **Step 2: Confirm the library ships in the wheel and note its path**

```bash
/home/kj16609/Desktop/Projects/cxx/LanceProject/.venv/bin/python -c \
  "import onnxruntime_extensions as e; print(e.__version__); print(e.get_library_path())"
```

Expected: prints a version and an absolute path to `libortextensions.so`. If this fails, the extensions path is dead — record that and skip to Step 6.

- [ ] **Step 3: Try to export the text tower with the tokenizer baked in**

Write a throwaway script at `/tmp/claude-1000/-home-kj16609-Desktop-Projects-cxx-IDHAN/e8ed4cf5-3177-450b-9ae5-5ca02d8ba71b/scratchpad/spike_text.py`. It must:

1. Load the model the same way `export_siglip2.py` does:
   `open_clip.create_model_from_pretrained("local-dir:/home/kj16609/Desktop/Projects/cxx/LanceProject/models/ViT-B-16-SigLIP2")`
2. Export `encode_text` + `torch.nn.functional.normalize(..., dim=-1)` to ONNX taking **token ids** (`int64[batch, 64]`).
3. Attempt to prepend a tokenizer op from `onnxruntime_extensions` so the graph takes `string[batch]`, using the HF tokenizer at `models/ViT-B-16-SigLIP2/tokenizer.json`.

Record whether step 3 succeeds and the exact error if not.

- [ ] **Step 4: Verify vectors match Python**

For the phrases `["catgirl", "a photo of a dog", "blurry"]`, compare the ONNX output against
`HyCLIP_Model.tokenize_text` from `/home/kj16609/Desktop/Projects/cxx/LanceProject/HyCLIP_Model.py`.

Expected: cosine similarity > 0.999 per phrase. Record actual values.

- [ ] **Step 5: Verify the library loads from C++**

Write `/tmp/claude-1000/-home-kj16609-Desktop-Projects-cxx-IDHAN/e8ed4cf5-3177-450b-9ae5-5ca02d8ba71b/scratchpad/spike_load.cpp`:

```cpp
#include <onnxruntime_cxx_api.h>
#include <cstdio>

int main( int argc, char** argv )
{
	if ( argc < 3 )
	{
		std::fputs( "usage: spike_load <ortextensions.so> <text.onnx>\n", stderr );
		return 2;
	}

	Ort::Env env { ORT_LOGGING_LEVEL_WARNING, "spike" };
	Ort::SessionOptions options {};
	options.RegisterCustomOpsLibrary( argv[ 1 ] );

	Ort::Session session { env, argv[ 2 ], options };

	std::printf( "loaded, %zu inputs\n", session.GetInputCount() );
	return 0;
}
```

Build and run:

```bash
g++ -std=c++23 -o /tmp/claude-1000/-home-kj16609-Desktop-Projects-cxx-IDHAN/e8ed4cf5-3177-450b-9ae5-5ca02d8ba71b/scratchpad/spike_load \
    /tmp/claude-1000/-home-kj16609-Desktop-Projects-cxx-IDHAN/e8ed4cf5-3177-450b-9ae5-5ca02d8ba71b/scratchpad/spike_load.cpp \
    $(pkg-config --cflags --libs libonnxruntime)
```

Expected: prints `loaded, 1 inputs`.

- [ ] **Step 6: Write the finding**

Create `docs/superpowers/specs/2026-08-11-embedding-search-spike.md` stating, with evidence:

- Whether extensions can bake in this Gemma tokenizer (yes/no + error text).
- Cosine similarities from Step 4.
- Whether `RegisterCustomOpsLibrary` worked from C++.
- **The decision:** `extensions` or `tokenizers-cpp`.
- If `extensions`: where `libortextensions.so` comes from for (a) the Docker image and (b) local dev.
- If `tokenizers-cpp`: the graph takes `int64[batch, 64]` token ids instead, and Task 7 gains a tokenizer dependency. Tasks 2, 3, 4, 5, 8, 9 are unchanged either way.

- [ ] **Step 7: Commit**

```bash
git add docs/superpowers/specs/2026-08-11-embedding-search-spike.md
git commit -m "docs: record the embedding text-tower spike finding"
```

---

### Task 2: HNSW index on every embedding table

**Files:**
- Create: `IDHANMigration/src/func_create_embedding_table/192-hnsw.sql`
- Test: `tests/src/db/embeddingIndexTest.cpp`

**Interfaces:**
- Consumes: `embedding_models(model_id, model_dimensions)` and `embeddings_<model_id>` from migrations 190/191.
- Produces: an index named `embeddings_<model_id>_hnsw` on every model table whose `model_dimensions <= 4000`. Task 4's query relies on it existing but is correct without it.

- [ ] **Step 1: Write the migration**

Create `IDHANMigration/src/func_create_embedding_table/192-hnsw.sql`:

```sql
-- Search wants a nearest-neighbour index, and the cheapest moment to have one is from the table's
-- first row: an index built later has to be remembered, and a model that is registered but
-- unsearchable is a worse failure than a backfill that runs slower. The cost is real -- every
-- backfill INSERT now maintains the graph -- and is accepted deliberately.
--
-- halfvec_cosine_ops: vectors are stored L2-normalised, so cosine and inner product rank
-- identically. Cosine is chosen because it stays correct if a norm ever drifts.
--
-- 4000 is pgvector's HNSW ceiling for halfvec, while embedding_models allows up to 16000. A model
-- above the ceiling still registers and still works -- unindexed and slow -- rather than failing to
-- register at all.
CREATE OR REPLACE FUNCTION create_embedding_model_table()
    RETURNS TRIGGER AS
$$
BEGIN
    EXECUTE format(
        'CREATE TABLE IF NOT EXISTS embeddings_%s (
             record_id INTEGER PRIMARY KEY REFERENCES records (record_id) ON DELETE CASCADE,
             embedding halfvec(%s)               NOT NULL
         )', new.model_id, new.model_dimensions );

    IF new.model_dimensions <= 4000 THEN
        -- Not CONCURRENTLY: illegal inside a trigger, and the table is empty at this point anyway.
        EXECUTE format(
            'CREATE INDEX IF NOT EXISTS embeddings_%s_hnsw
                 ON embeddings_%s USING hnsw (embedding halfvec_cosine_ops)',
            new.model_id, new.model_id );
    ELSE
        RAISE WARNING 'embedding model % has % dimensions, above pgvector''s HNSW limit of 4000; searches over it will be unindexed',
            new.model_name, new.model_dimensions;
    END IF;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

-- Models registered before this migration already have tables built by the previous version of the
-- function, which created no index. Without this block the feature would only ever work for models
-- registered after the upgrade -- which, on any existing install, means none of them.
DO
$$
    DECLARE
        model RECORD;
    BEGIN
        FOR model IN SELECT model_id, model_name, model_dimensions FROM embedding_models
            LOOP
                IF model.model_dimensions <= 4000 THEN
                    EXECUTE format(
                        'CREATE INDEX IF NOT EXISTS embeddings_%s_hnsw
                             ON embeddings_%s USING hnsw (embedding halfvec_cosine_ops)',
                        model.model_id, model.model_id );
                ELSE
                    RAISE WARNING 'embedding model % has % dimensions, above pgvector''s HNSW limit of 4000; leaving it unindexed',
                        model.model_name, model.model_dimensions;
                END IF;
            END LOOP;
    END
$$;
```

- [ ] **Step 2: Write the failing test**

Create `tests/src/db/embeddingIndexTest.cpp`:

```cpp
//
// Verifies migration 192: every embedding model table carries an HNSW index.
//
#include <gtest/gtest.h>

#include "fixtures/ServerDBFixture.hpp"

//! Registering a model must create both its table and its index, in one trigger.
TEST_F( ServerDBFixture, EmbeddingModelTableGetsHnswIndex )
{
	auto db { getDbClient() };

	db->execSqlSync(
		"INSERT INTO embedding_models (model_name, model_dimensions) VALUES ($1, $2)", "index-test-model", 8 );

	const auto model_id {
		db->execSqlSync( "SELECT model_id FROM embedding_models WHERE model_name = $1", "index-test-model" )[ 0 ]
			[ "model_id" ]
				.as< std::int32_t >()
	};

	const auto indexes { db->execSqlSync(
		"SELECT indexdef FROM pg_indexes WHERE tablename = $1 AND indexname = $2",
		std::format( "embeddings_{}", model_id ),
		std::format( "embeddings_{}_hnsw", model_id ) ) };

	ASSERT_EQ( indexes.size(), 1 ) << "migration 192 did not create the HNSW index";
	EXPECT_NE( indexes[ 0 ][ "indexdef" ].as< std::string >().find( "hnsw" ), std::string::npos );
	EXPECT_NE( indexes[ 0 ][ "indexdef" ].as< std::string >().find( "halfvec_cosine_ops" ), std::string::npos );
}
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
cmake --build build/debug --target IDHANTests
./build/bin/IDHANTests --gtest_filter="ServerDBFixture.EmbeddingModelTableGetsHnswIndex" --testmode --use_stdout
```

Expected: FAIL — `migration 192 did not create the HNSW index` (0 rows), because the migration has not run against the test schema yet.

If the build fails on `tests/src/profiling/fiberNameTest.cpp` (missing `idhan_tracy/CoroFiber.hpp`), that is the pre-existing breakage noted in Global Constraints. Fix or exclude it first; do not work around it by deleting the test.

- [ ] **Step 4: Run the test to verify it passes**

The migration from Step 1 runs automatically at server startup, which `ServerDBFixture` triggers.

```bash
./build/bin/IDHANTests --gtest_filter="ServerDBFixture.EmbeddingModelTableGetsHnswIndex" --testmode --use_stdout
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add IDHANMigration/src/func_create_embedding_table/192-hnsw.sql tests/src/db/embeddingIndexTest.cpp
git commit -m "feat(db): index every embedding table with HNSW"
```

---

### Task 3: Query vector assembly

Header-only on purpose: `IDHANServer` is an executable, so test binaries cannot link its `.cpp` files. Keeping this maths in a header makes it testable from `IDHANCoreTests`, which needs neither a database nor drogon.

**Files:**
- Create: `IDHANServer/src/embeddings/queryVector.hpp`
- Test: `tests/core/queryVectorTest.cpp`
- Modify: `tests/CMakeLists.txt:28` (add the server include root to `IDHANCoreTests`)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct idhan::embeddings::WeightedVector { std::vector< float > m_vector; float m_weight; }`
  - `std::expected< std::vector< float >, std::string > idhan::embeddings::assembleQueryVector( std::span< const WeightedVector > terms, std::size_t dimensions )`
  - `std::string idhan::embeddings::toHalfvecLiteral( std::span< const float > values )`

  Task 4 and Task 8 both call `assembleQueryVector` and `toHalfvecLiteral`.

- [ ] **Step 1: Write the failing test**

Create `tests/core/queryVectorTest.cpp`:

```cpp
//
// Query vector assembly: the signed weighted sum at the heart of embedding search.
//
#include <gtest/gtest.h>

#include <cmath>

#include "embeddings/queryVector.hpp"

using namespace idhan::embeddings;

namespace
{

//! Cosine similarity, for asserting direction without caring about magnitude.
[[nodiscard]] float cosine( const std::vector< float >& lhs, const std::vector< float >& rhs )
{
	float dot { 0.0f };
	float lhs_norm { 0.0f };
	float rhs_norm { 0.0f };

	for ( std::size_t index = 0; index < lhs.size(); ++index )
	{
		dot += lhs[ index ] * rhs[ index ];
		lhs_norm += lhs[ index ] * lhs[ index ];
		rhs_norm += rhs[ index ] * rhs[ index ];
	}

	return dot / ( std::sqrt( lhs_norm ) * std::sqrt( rhs_norm ) );
}

} // namespace

//! A single positive term comes back as itself, normalised.
TEST( QueryVector, SingleTermIsItsOwnDirection )
{
	const std::vector< WeightedVector > terms { { .m_vector = { 3.0f, 4.0f }, .m_weight = 1.0f } };

	const auto result { assembleQueryVector( terms, 2 ) };

	ASSERT_TRUE( result.has_value() ) << result.error();
	EXPECT_NEAR( ( *result )[ 0 ], 0.6f, 1e-5f );
	EXPECT_NEAR( ( *result )[ 1 ], 0.8f, 1e-5f );
}

//! The output is always unit length, whatever the weights were.
TEST( QueryVector, ResultIsNormalised )
{
	const std::vector< WeightedVector > terms { { .m_vector = { 1.0f, 0.0f }, .m_weight = 7.5f },
		                                        { .m_vector = { 0.0f, 1.0f }, .m_weight = 2.5f } };

	const auto result { assembleQueryVector( terms, 2 ) };

	ASSERT_TRUE( result.has_value() ) << result.error();

	float norm { 0.0f };
	for ( const float value : *result ) norm += value * value;

	EXPECT_NEAR( std::sqrt( norm ), 1.0f, 1e-5f );
}

//! A negative weight pushes away from that term, which is what a negative reference image means.
TEST( QueryVector, NegativeWeightSubtracts )
{
	const std::vector< WeightedVector > terms { { .m_vector = { 1.0f, 0.0f }, .m_weight = 1.0f },
		                                        { .m_vector = { 0.0f, 1.0f }, .m_weight = -1.0f } };

	const auto result { assembleQueryVector( terms, 2 ) };

	ASSERT_TRUE( result.has_value() ) << result.error();
	EXPECT_GT( ( *result )[ 0 ], 0.0f );
	EXPECT_LT( ( *result )[ 1 ], 0.0f );
	EXPECT_NEAR( cosine( *result, { 1.0f, -1.0f } ), 1.0f, 1e-5f );
}

//! Terms that cancel leave no direction at all. Cosine distance against zero is undefined and
//! pgvector answers NaN, which sorts arbitrarily rather than failing -- so this must be an error.
TEST( QueryVector, CancellingTermsAreRejected )
{
	const std::vector< WeightedVector > terms { { .m_vector = { 1.0f, 0.0f }, .m_weight = 1.0f },
		                                        { .m_vector = { 1.0f, 0.0f }, .m_weight = -1.0f } };

	const auto result { assembleQueryVector( terms, 2 ) };

	ASSERT_FALSE( result.has_value() );
	EXPECT_NE( result.error().find( "cancel" ), std::string::npos );
}

//! An empty query has no meaning and must not reach the database.
TEST( QueryVector, EmptyTermListIsRejected )
{
	const auto result { assembleQueryVector( {}, 2 ) };

	ASSERT_FALSE( result.has_value() );
}

//! A term of the wrong width would be summed elementwise into nonsense.
TEST( QueryVector, WrongWidthTermIsRejected )
{
	const std::vector< WeightedVector > terms { { .m_vector = { 1.0f, 0.0f, 0.0f }, .m_weight = 1.0f } };

	const auto result { assembleQueryVector( terms, 2 ) };

	ASSERT_FALSE( result.has_value() );
}

//! pgvector parses its own text format; this is the shape it expects.
TEST( QueryVector, HalfvecLiteralFormat )
{
	EXPECT_EQ( toHalfvecLiteral( std::vector< float > { 1.0f, -0.5f } ), "[1,-0.5]" );
}
```

- [ ] **Step 2: Add the server include root to the core test target**

In `tests/CMakeLists.txt`, immediately after the `target_link_libraries(IDHANCoreTests ...)` line, add:

```cmake
# queryVectorTest includes IDHANServer/src/embeddings/queryVector.hpp, which is header-only precisely
# so it can be tested without linking the server executable.
target_include_directories(IDHANCoreTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
cmake --build build/debug --target IDHANCoreTests
```

Expected: FAIL — `embeddings/queryVector.hpp: No such file or directory`.

- [ ] **Step 4: Write the implementation**

Create `IDHANServer/src/embeddings/queryVector.hpp`:

```cpp
//
// Created by kj16609 on 8/11/26.
//
#pragma once

#include <cmath>
#include <cstddef>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <vector>

namespace idhan::embeddings
{

//! Below this magnitude a summed query has no direction left to search along.
/** Not merely a guard against exactly zero: terms that nearly cancel leave a vector whose direction
 *  is floating-point noise, and normalising it would amplify that noise into a confident-looking
 *  query pointing nowhere in particular. */
constexpr float MIN_QUERY_MAGNITUDE { 1e-6f };

//! One resolved term: a unit vector and the signed weight it contributes to the query.
/** Where the vector came from -- a phrase through the text tower, or a record through a table
 *  lookup -- is deliberately not represented. By this point they are the same thing. */
struct WeightedVector
{
	std::vector< float > m_vector {};
	//! Already signed: negative means the query is pushed away from this direction.
	float m_weight { 1.0f };
};

//! Formats a vector in pgvector's text input format.
[[nodiscard]] inline std::string toHalfvecLiteral( const std::span< const float > values )
{
	std::string literal {};
	literal.reserve( values.size() * 12 + 2 );
	literal.push_back( '[' );

	for ( std::size_t index = 0; index < values.size(); ++index )
	{
		if ( index > 0 ) literal.push_back( ',' );
		literal += std::format( "{:.6g}", values[ index ] );
	}

	literal.push_back( ']' );
	return literal;
}

//! Sums \p terms by their signed weights and normalises the result.
/** The whole of the query model, and the same arithmetic as the reference prototype's
 *  `GUI.py:236`.
 *
 *  Normalising does not change which records come back: cosine distance is scale-invariant in the
 *  query vector, so any positive multiple of this result ranks identically. It is done so the
 *  distances returned to the caller are interpretable on a fixed 0..2 scale -- not as a tuning
 *  knob, and it must never be exposed as one. */
[[nodiscard]] inline std::expected< std::vector< float >, std::string > assembleQueryVector(
	const std::span< const WeightedVector > terms,
	const std::size_t dimensions )
{
	if ( terms.empty() ) return std::unexpected( std::string { "a query needs at least one term" } );

	std::vector< float > summed( dimensions, 0.0f );

	for ( const auto& term : terms )
	{
		if ( term.m_vector.size() != dimensions )
			return std::unexpected(
				std::format( "a term has {} values but the model has {}", term.m_vector.size(), dimensions ) );

		for ( std::size_t index = 0; index < dimensions; ++index )
			summed[ index ] += term.m_vector[ index ] * term.m_weight;
	}

	float magnitude { 0.0f };
	for ( const float value : summed ) magnitude += value * value;
	magnitude = std::sqrt( magnitude );

	if ( magnitude < MIN_QUERY_MAGNITUDE )
		return std::unexpected(
			std::string { "the terms cancel out, leaving no direction to search along; "
			              "adjust the weights so the positives and negatives do not balance" } );

	for ( float& value : summed ) value /= magnitude;

	return summed;
}

} // namespace idhan::embeddings
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build/debug --target IDHANCoreTests
./build/bin/IDHANCoreTests --gtest_filter="QueryVector.*"
```

Expected: PASS, 7 tests.

- [ ] **Step 6: Commit**

```bash
git add IDHANServer/src/embeddings/queryVector.hpp tests/core/queryVectorTest.cpp tests/CMakeLists.txt
git commit -m "feat(embeddings): add query vector assembly"
```

---

### Task 4: POST /embeddings/search over record references

Delivers a working, testable feature with no dependency on Task 1. Text terms are accepted by the schema but rejected with a clear message until Task 8.

**Files:**
- Create: `IDHANServer/src/embeddings/searchEmbeddings.hpp`, `IDHANServer/src/embeddings/searchEmbeddings.cpp`
- Modify: `IDHANServer/src/api/EmbeddingAPI.hpp` (class comment, new method, new route)
- Modify: `IDHANServer/src/api/embeddings/embeddingEndpoints.cpp` (handler)
- Test: `tests/src/server/embeddingSearchTest.cpp`

**Interfaces:**
- Consumes: `assembleQueryVector`, `toHalfvecLiteral`, `WeightedVector` from Task 3.
- Produces:
  - `struct idhan::embeddings::QueryTerm { bool m_is_text; std::string m_text; RecordID m_record_id; float m_weight; }`
  - `struct idhan::embeddings::SearchHit { RecordID m_record_id; double m_distance; }`
  - `idhan::ExpectedTask< std::vector< idhan::embeddings::SearchHit > > idhan::embeddings::searchEmbeddings( std::int32_t model_id, std::size_t dimensions, std::vector< QueryTerm > terms, std::size_t limit, std::size_t ef_search, DbClientPtr db )`

  Task 8 modifies `searchEmbeddings` to resolve text terms.

- [ ] **Step 1: Write the failing test**

Create `tests/src/server/embeddingSearchTest.cpp`:

```cpp
//
// POST /embeddings/search. Vectors are seeded directly, so this needs no model and no onnxruntime.
//
#include <gtest/gtest.h>

#include "helpers/serverStarterHelper.hpp"

namespace
{

//! A unit vector in 4 dimensions, as pgvector's text format.
[[nodiscard]] std::string unitAt( const int axis )
{
	std::string literal { "[" };
	for ( int index = 0; index < 4; ++index )
	{
		if ( index > 0 ) literal += ",";
		literal += ( index == axis ) ? "1" : "0";
	}
	return literal + "]";
}

} // namespace

//! The reference record is its own nearest neighbour, and the opposite direction sorts last.
TEST( EmbeddingSearch, RecordReferenceRanksByDirection )
{
	SERVER_HANDLE( server );

	auto db { server.getDbClient() };

	db->execSqlSync(
		"INSERT INTO embedding_models (model_name, model_dimensions) VALUES ($1, $2)", "search-test-model", 4 );

	const auto model_id { db->execSqlSync(
							  "SELECT model_id FROM embedding_models WHERE model_name = $1",
							  "search-test-model" )[ 0 ][ "model_id" ]
	                          .as< std::int32_t >() };

	// Three records: one on +x, one on +y (orthogonal), one on -x (opposite).
	const auto record_a { server.createRecord( "aa" ) };
	const auto record_b { server.createRecord( "bb" ) };
	const auto record_c { server.createRecord( "cc" ) };

	const auto insert { std::format( "INSERT INTO embeddings_{} (record_id, embedding) VALUES ($1, $2::halfvec)",
		                             model_id ) };

	db->execSqlSync( insert, record_a, unitAt( 0 ) );
	db->execSqlSync( insert, record_b, unitAt( 1 ) );
	db->execSqlSync( insert, record_c, "[-1,0,0,0]" );

	Json::Value term {};
	term[ "type" ] = "record";
	term[ "record_id" ] = record_a;
	term[ "weight" ] = 1.0;

	Json::Value body {};
	body[ "model_name" ] = "search-test-model";
	body[ "terms" ].append( term );
	body[ "limit" ] = 10;

	const auto response { server.post( "/embeddings/search", body ) };

	ASSERT_EQ( response.status, 200 ) << response.text;

	const auto& ids { response.json[ "record_ids" ] };
	ASSERT_EQ( ids.size(), 3 );
	EXPECT_EQ( ids[ 0 ].asInt(), record_a ) << "the reference record must be its own nearest neighbour";
	EXPECT_EQ( ids[ 2 ].asInt(), record_c ) << "the opposite direction must sort last";
	EXPECT_NEAR( response.json[ "distances" ][ 0 ].asDouble(), 0.0, 1e-4 );
}

//! A negative reference inverts the ordering.
TEST( EmbeddingSearch, NegativeReferenceInvertsOrder )
{
	SERVER_HANDLE( server );

	auto db { server.getDbClient() };

	db->execSqlSync(
		"INSERT INTO embedding_models (model_name, model_dimensions) VALUES ($1, $2)", "negative-test-model", 4 );

	const auto model_id { db->execSqlSync(
							  "SELECT model_id FROM embedding_models WHERE model_name = $1",
							  "negative-test-model" )[ 0 ][ "model_id" ]
	                          .as< std::int32_t >() };

	const auto record_a { server.createRecord( "dd" ) };
	const auto record_c { server.createRecord( "ee" ) };

	const auto insert { std::format( "INSERT INTO embeddings_{} (record_id, embedding) VALUES ($1, $2::halfvec)",
		                             model_id ) };

	db->execSqlSync( insert, record_a, unitAt( 0 ) );
	db->execSqlSync( insert, record_c, "[-1,0,0,0]" );

	Json::Value term {};
	term[ "type" ] = "record";
	term[ "record_id" ] = record_a;
	term[ "weight" ] = -1.0;

	Json::Value body {};
	body[ "model_name" ] = "negative-test-model";
	body[ "terms" ].append( term );

	const auto response { server.post( "/embeddings/search", body ) };

	ASSERT_EQ( response.status, 200 ) << response.text;
	EXPECT_EQ( response.json[ "record_ids" ][ 0 ].asInt(), record_c )
		<< "negating the reference must rank its opposite first";
}

//! A reference record with no vector changes the query silently if dropped, so it is an error.
TEST( EmbeddingSearch, ReferenceWithoutEmbeddingIsRejected )
{
	SERVER_HANDLE( server );

	auto db { server.getDbClient() };

	db->execSqlSync(
		"INSERT INTO embedding_models (model_name, model_dimensions) VALUES ($1, $2)", "missing-test-model", 4 );

	const auto orphan { server.createRecord( "ff" ) };

	Json::Value term {};
	term[ "type" ] = "record";
	term[ "record_id" ] = orphan;
	term[ "weight" ] = 1.0;

	Json::Value body {};
	body[ "model_name" ] = "missing-test-model";
	body[ "terms" ].append( term );

	const auto response { server.post( "/embeddings/search", body ) };

	EXPECT_EQ( response.status, 400 );
	EXPECT_NE( response.text.find( std::to_string( orphan ) ), std::string::npos )
		<< "the error must name the record that has no embedding";
}

//! An empty query has no meaning.
TEST( EmbeddingSearch, EmptyTermListIsRejected )
{
	SERVER_HANDLE( server );

	Json::Value body {};
	body[ "model_name" ] = "search-test-model";
	body[ "terms" ] = Json::Value { Json::arrayValue };

	EXPECT_EQ( server.post( "/embeddings/search", body ).status, 400 );
}

//! An unregistered model is a 404, not a 500 from a missing table.
TEST( EmbeddingSearch, UnknownModelIsNotFound )
{
	SERVER_HANDLE( server );

	Json::Value term {};
	term[ "type" ] = "record";
	term[ "record_id" ] = 1;
	term[ "weight" ] = 1.0;

	Json::Value body {};
	body[ "model_name" ] = "no-such-model-at-all";
	body[ "terms" ].append( term );

	EXPECT_EQ( server.post( "/embeddings/search", body ).status, 404 );
}
```

**Note:** `server.createRecord( ... )` and `server.post( ... )` must match the helpers actually provided by `tests/src/helpers/serverStarterHelper.hpp` and the `SERVER_HANDLE` macro. Read that header first and adapt these calls to its real API rather than assuming these names; the assertions are what matter.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build/debug --target IDHANTests
./build/bin/IDHANTests --gtest_filter="EmbeddingSearch.*" --testmode --use_stdout
```

Expected: FAIL — all five, with 404 from an unrouted `/embeddings/search`.

- [ ] **Step 3: Write the search header**

Create `IDHANServer/src/embeddings/searchEmbeddings.hpp`:

```cpp
//
// Created by kj16609 on 8/11/26.
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "api/helpers/ResponseCallback.hpp"
#include "db/DbTypes.hpp"

namespace idhan::embeddings
{

//! One term of a query, before its vector has been resolved.
struct QueryTerm
{
	bool m_is_text { false };
	std::string m_text {}; //!< Free text. Never a tag: this system does not touch the tag tables.
	RecordID m_record_id { 0 };
	//! Already signed by the caller: negative means push away from this term.
	float m_weight { 1.0f };
};

//! One result, in distance order.
struct SearchHit
{
	RecordID m_record_id { 0 };
	double m_distance { 0.0 };
};

//! Resolves \p terms to vectors, sums them, and returns the nearest records.
/** Record terms resolve in one query for all of them. Text terms are rejected until the text tower
 *  exists. */
[[nodiscard]] ExpectedTask< std::vector< SearchHit > > searchEmbeddings(
	std::int32_t model_id,
	std::size_t dimensions,
	std::vector< QueryTerm > terms,
	std::size_t limit,
	std::size_t ef_search,
	DbClientPtr db );

} // namespace idhan::embeddings
```

Check the exact include for `DbClientPtr` and `ExpectedTask` against `IDHANServer/src/embeddings/embeddings.hpp` and match it; the paths above are the expected ones but the existing file is authoritative.

- [ ] **Step 4: Write the search implementation**

Create `IDHANServer/src/embeddings/searchEmbeddings.cpp`:

```cpp
//
// Created by kj16609 on 8/11/26.
//

#include "searchEmbeddings.hpp"

#include <drogon/drogon.h>

#include <algorithm>
#include <format>

#include "api/helpers/createBadRequest.hpp"
#include "db/drogonArrayBind.hpp"
#include "queryVector.hpp"

namespace idhan::embeddings
{

namespace
{

//! Parses pgvector's "[1,-0.5,...]" text output back into floats.
[[nodiscard]] std::vector< float > parseHalfvecLiteral( const std::string& literal )
{
	std::vector< float > values {};

	std::size_t cursor { literal.find( '[' ) };
	if ( cursor == std::string::npos ) return values;
	++cursor;

	while ( cursor < literal.size() && literal[ cursor ] != ']' )
	{
		const auto end { literal.find_first_of( ",]", cursor ) };
		if ( end == std::string::npos ) break;

		values.push_back( std::strtof( literal.substr( cursor, end - cursor ).c_str(), nullptr ) );
		cursor = ( literal[ end ] == ',' ) ? end + 1 : end;
	}

	return values;
}

} // namespace

ExpectedTask< std::vector< SearchHit > > searchEmbeddings(
	const std::int32_t model_id,
	const std::size_t dimensions,
	std::vector< QueryTerm > terms,
	const std::size_t limit,
	const std::size_t ef_search,
	DbClientPtr db )
{
	const auto table { std::format( "embeddings_{}", model_id ) };

	std::vector< WeightedVector > resolved {};
	resolved.reserve( terms.size() );

	// Every record term in one query. Resolving them one at a time would be a round trip per
	// reference image for data that is a single index scan.
	std::vector< RecordID > wanted {};
	for ( const auto& term : terms )
	{
		if ( !term.m_is_text ) wanted.push_back( term.m_record_id );
	}

	std::unordered_map< RecordID, std::vector< float > > vectors {};

	if ( !wanted.empty() )
	{
		const auto rows { co_await db->execSqlCoro(
			std::format( "SELECT record_id, embedding::text AS embedding FROM {} WHERE record_id = ANY($1)", table ),
			std::forward< const std::vector< RecordID > >( wanted ) ) };

		for ( const auto& row : rows )
			vectors.emplace(
				row[ "record_id" ].as< RecordID >(), parseHalfvecLiteral( row[ "embedding" ].as< std::string >() ) );
	}

	for ( const auto& term : terms )
	{
		if ( term.m_is_text )
		{
			// Replaced in the text-tower task. Refused rather than skipped: dropping a term changes
			// the query into one the caller did not ask for.
			co_return std::unexpected(
				createBadRequest( "Text terms are not supported yet; use record references" ) );
		}

		const auto found { vectors.find( term.m_record_id ) };

		// Named, not skipped, for the same reason.
		if ( found == vectors.end() )
			co_return std::unexpected(
				createBadRequest(
					"Record {} has no embedding for this model. Run a backfill first, or remove it from the query",
					term.m_record_id ) );

		resolved.emplace_back( WeightedVector { .m_vector = found->second, .m_weight = term.m_weight } );
	}

	const auto query_vector { assembleQueryVector( resolved, dimensions ) };
	if ( !query_vector ) co_return std::unexpected( createBadRequest( "{}", query_vector.error() ) );

	// SET LOCAL, so the recall setting dies with the transaction rather than leaking onto whatever
	// this pooled connection serves next.
	co_await db->execSqlCoro( std::format( "SET LOCAL hnsw.ef_search = {}", ef_search ) );

	const auto rows { co_await db->execSqlCoro(
		std::format(
			"SELECT record_id, embedding <=> $1::halfvec AS distance FROM {} ORDER BY distance LIMIT {}",
			table,
			limit ),
		toHalfvecLiteral( *query_vector ) ) };

	std::vector< SearchHit > hits {};
	hits.reserve( rows.size() );

	for ( const auto& row : rows )
		hits.emplace_back(
			SearchHit { .m_record_id = row[ "record_id" ].as< RecordID >(),
			            .m_distance = row[ "distance" ].as< double >() } );

	co_return hits;
}

} // namespace idhan::embeddings
```

- [ ] **Step 5: Add the route**

In `IDHANServer/src/api/EmbeddingAPI.hpp`, replace the class comment and add the method plus route. The comment currently claims search is absent and must not survive this change:

```cpp
//! Endpoints for image embeddings: what models exist, filling in the vectors, and searching them.
/** Search is standalone top-K over one model's vectors. Composing it with tag predicates is a
 *  follow-up: pre-filtering and post-filtering trade recall against speed in ways that depend on
 *  collection size, and there is no data to choose between them on yet. */
class EmbeddingAPI : public drogon::HttpController< EmbeddingAPI >
{
	using ResponseTask = drogon::Task< drogon::HttpResponsePtr >;

	ResponseTask listModels( drogon::HttpRequestPtr request );

	ResponseTask generate( drogon::HttpRequestPtr request );

	ResponseTask search( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN
	ADD_METHOD_TO( EmbeddingAPI::listModels, "/embeddings/models", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( EmbeddingAPI::generate, "/embeddings/generate", drogon::Post, IDHANAPIAuthName );
	ADD_METHOD_TO( EmbeddingAPI::search, "/embeddings/search", drogon::Post, IDHANAPIAuthName );
	METHOD_LIST_END
};
```

- [ ] **Step 6: Write the handler**

Append to `IDHANServer/src/api/embeddings/embeddingEndpoints.cpp`, inside `namespace idhan::api`, and add `#include "embeddings/searchEmbeddings.hpp"` to its includes:

```cpp
namespace
{

//! Upper bound on results. A caller-chosen limit is an allocation of the caller's choosing.
constexpr std::size_t MAX_SEARCH_LIMIT { 5000 };
constexpr std::size_t DEFAULT_SEARCH_LIMIT { 200 };

//! HNSW recall knob. Higher searches more of the graph: better recall, more time.
constexpr std::size_t DEFAULT_EF_SEARCH { 100 };
constexpr std::size_t MAX_EF_SEARCH { 1000 };

} // namespace

drogon::Task< drogon::HttpResponsePtr > EmbeddingAPI::search( drogon::HttpRequestPtr request )
{
	const auto json { request->getJsonObject() };
	if ( json == nullptr ) co_return createBadRequest( "Expected a JSON body" );

	if ( !( *json )[ "model_name" ].isString() ) co_return createBadRequest( "Expected a string \"model_name\"" );

	const auto model_name { ( *json )[ "model_name" ].asString() };

	const auto& terms_json { ( *json )[ "terms" ] };
	if ( !terms_json.isArray() ) co_return createBadRequest( "Expected an array \"terms\"" );
	if ( terms_json.empty() ) co_return createBadRequest( "A query needs at least one term" );

	std::vector< embeddings::QueryTerm > terms {};
	terms.reserve( terms_json.size() );

	for ( const auto& entry : terms_json )
	{
		if ( !entry.isObject() ) co_return createBadRequest( "Every term must be an object" );
		if ( !entry[ "type" ].isString() ) co_return createBadRequest( "Every term needs a string \"type\"" );

		const auto type { entry[ "type" ].asString() };

		embeddings::QueryTerm term {};

		// Defaulted rather than required: an unweighted term is the common case, and 1.0 is what the
		// reference prototype's rows start at.
		term.m_weight = entry[ "weight" ].isNumeric() ? static_cast< float >( entry[ "weight" ].asDouble() ) : 1.0f;

		if ( type == "text" )
		{
			if ( !entry[ "text" ].isString() ) co_return createBadRequest( "A text term needs a string \"text\"" );

			term.m_is_text = true;
			term.m_text = entry[ "text" ].asString();

			if ( term.m_text.empty() ) co_return createBadRequest( "A text term cannot be empty" );
		}
		else if ( type == "record" )
		{
			if ( !entry[ "record_id" ].isIntegral() )
				co_return createBadRequest( "A record term needs an integral \"record_id\"" );

			term.m_is_text = false;
			term.m_record_id = static_cast< RecordID >( entry[ "record_id" ].asInt64() );
		}
		else
		{
			co_return createBadRequest( "Unknown term type \"{}\"; expected \"text\" or \"record\"", type );
		}

		terms.emplace_back( std::move( term ) );
	}

	auto db { drogon::app().getDbClient() };

	const auto rows { co_await db->execSqlCoro(
		"SELECT model_id, model_dimensions FROM embedding_models WHERE model_name = $1", model_name ) };

	if ( rows.empty() ) co_return createNotFound( "The model \"{}\" is not registered", model_name );

	const auto model_id { rows[ 0 ][ "model_id" ].as< std::int32_t >() };
	const auto dimensions { static_cast< std::size_t >( rows[ 0 ][ "model_dimensions" ].as< std::int32_t >() ) };

	const auto limit { ( *json )[ "limit" ].isIntegral() && ( *json )[ "limit" ].asInt64() > 0 ?
		                   std::min( static_cast< std::size_t >( ( *json )[ "limit" ].asInt64() ), MAX_SEARCH_LIMIT ) :
		                   DEFAULT_SEARCH_LIMIT };

	const auto ef_search {
		( *json )[ "ef_search" ].isIntegral() && ( *json )[ "ef_search" ].asInt64() > 0 ?
			std::min( static_cast< std::size_t >( ( *json )[ "ef_search" ].asInt64() ), MAX_EF_SEARCH ) :
			DEFAULT_EF_SEARCH
	};

	const auto started { std::chrono::steady_clock::now() };

	const auto hits { co_await embeddings::searchEmbeddings( model_id, dimensions, std::move( terms ), limit, ef_search, db ) };
	return_unexpected_error( hits );

	// arrayValue explicitly: an empty result must serialise as [] rather than null.
	Json::Value record_ids { Json::arrayValue };
	Json::Value distances { Json::arrayValue };

	for ( const auto& hit : hits.value() )
	{
		record_ids.append( hit.m_record_id );
		distances.append( hit.m_distance );
	}

	Json::Value response {};
	response[ "record_ids" ] = std::move( record_ids );
	response[ "distances" ] = std::move( distances );
	response[ "query_ms" ] = static_cast< Json::UInt64 >(
		std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - started ).count() );

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}
```

- [ ] **Step 7: Run the tests to verify they pass**

```bash
cmake --build build/debug --target IDHANServer IDHANTests
./build/bin/IDHANTests --gtest_filter="EmbeddingSearch.*" --testmode --use_stdout
```

Expected: PASS, 5 tests.

- [ ] **Step 8: Commit**

```bash
git add IDHANServer/src/embeddings/searchEmbeddings.hpp IDHANServer/src/embeddings/searchEmbeddings.cpp \
        IDHANServer/src/api/EmbeddingAPI.hpp IDHANServer/src/api/embeddings/embeddingEndpoints.cpp \
        tests/src/server/embeddingSearchTest.cpp
git commit -m "feat(embeddings): search by record reference"
```

---

### Task 5: EMBED_TEXT over IPC

**Files:**
- Modify: `IDHANModules/ipc/include/ipc/Protocol.hpp:37-71` (new field), `:91-98` (new op), `:122-139` (`requiredFlag`)
- Modify: `IDHANModules/ipc/src/Protocol.cpp:75-95` (`toString`), `:136-145` (`callOpFromString`)
- Modify: `IDHANModules/include/EmbeddingModule.hpp` (interface)
- Modify: `IDHANModules/runner-src/WorkerRunner.cpp` (fileless call path, `invoke`, `describeResult`)
- Modify: `IDHANServer/src/modules/RemoteModule.hpp`, `IDHANServer/src/modules/RemoteModule.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `ipc::CallOp::EMBED_TEXT`, wire name `"embed_text"`; `ipc::field::PHRASE` = `"phrase"`.
  - `virtual bool idhan::EmbeddingModuleI::supportsText()` and
    `virtual std::expected< EmbeddingInfo, ModuleError > idhan::EmbeddingModuleI::embedText( std::string_view )`.
  - `IDHANTask< std::expected< EmbeddingInfo, ModuleError > > idhan::modules::RemoteModule::embedText( std::string phrase ) const`
    and `bool idhan::modules::RemoteModule::supportsText() const`.

  Task 7 implements the module side; Task 8 calls `RemoteModule::embedText`.

- [ ] **Step 1: Add the op, the field, and the interface flag**

In `IDHANModules/ipc/include/ipc/Protocol.hpp`, add to the field list near `EMBEDDING` (line 69):

```cpp
inline constexpr auto PHRASE { "phrase" };
inline constexpr auto SUPPORTS_TEXT { "supports_text" };
```

Extend `CallOp`:

```cpp
	EMBED, //!< EmbeddingModuleI::embed
	EMBED_TEXT, //!< EmbeddingModuleI::embedText -- the only op that carries no file
};
```

Extend `requiredFlag`:

```cpp
		case CallOp::EMBED:
			[[fallthrough]];
		case CallOp::EMBED_TEXT:
			return ModuleTypeFlags::EMBEDDING;
```

Add `supports_text` to `ManifestEntry` beside `dimensions`:

```cpp
	//! EMBEDDING modules only. Whether this model has a text tower; false is a normal configuration.
	bool supports_text { false };
```

- [ ] **Step 2: Extend the string conversions and the manifest**

In `IDHANModules/ipc/src/Protocol.cpp`: add `case CallOp::EMBED_TEXT: return "embed_text";` to `toString`, and
`if ( value == "embed_text" ) return CallOp::EMBED_TEXT;` to `callOpFromString`.

In `toJson( const ManifestEntry& )` and `manifestEntryFromJson`, carry `supports_text` through using `field::SUPPORTS_TEXT`.

In `manifestSignature`, extend the model clause so text support is part of the identity — a library rebuilt with a text tower added must count as changed:

```cpp
		signature += std::format( "|{}|{}|{}", entry.model_name, entry.dimensions, entry.supports_text );
```

- [ ] **Step 3: Extend the module interface**

In `IDHANModules/include/EmbeddingModule.hpp`, add to `EmbeddingModuleI`:

```cpp
	//! Whether this model has a text tower.
	/** False is a normal configuration, not an error: a model may ship image-only, its text graph may
	 *  be absent, or its tokenizer may have failed its parity check at startup. Callers ask before
	 *  sending text rather than discovering it from a failed call. */
	[[nodiscard]] virtual bool supportsText() { return false; }

	//! Embeds a phrase into the same space as embed().
	/** Takes no file, so none of the file plumbing applies. Must not be called unless supportsText().
	 *  \return An L2-normalised vector of dimensions() floats, or a ModuleError. */
	[[nodiscard]] virtual std::expected< EmbeddingInfo, ModuleError > embedText( [[maybe_unused]] std::string_view phrase )
	{
		return std::unexpected( ModuleError { "this model has no text encoder" } );
	}
```

Both are defaulted rather than pure so existing embedding modules keep compiling unchanged.

- [ ] **Step 4: Make the worker accept a call with no file**

In `IDHANModules/runner-src/WorkerRunner.cpp`, in `handleCall` (around line 521), make the input adoption conditional. `EMBED_TEXT` is the only op that carries no descriptor:

```cpp
	// Adopted here rather than on the pool thread, and that placement is the security property, not
	// an optimisation: a ring's descriptor has to be registered and closed before any module code
	// runs, because while it exists /proc/self/fdinfo prints the path of the file it was given.
	//
	// EMBED_TEXT is the one op with nothing to adopt -- it operates on a phrase, not a file -- so
	// requiring a descriptor here would reject it before it ever reached a module.
	if ( call.op != ipc::CallOp::EMBED_TEXT )
	{
		auto file { adoptInput( frame ) };
		if ( !file )
		{
			reject( file.error() );
			return;
		}
		call.file = std::move( *file );
	}
```

Read the phrase alongside the other body fields near line 486:

```cpp
	call.phrase = frame.body[ ipc::field::PHRASE ].asString();
```

Add `std::string phrase {};` to `QueuedCall` in `IDHANModules/runner-src/WorkerRunner.hpp`.

- [ ] **Step 5: Dispatch it in `invoke`**

`invoke` builds `ModuleCallData` from `*call.file`, which is null for this op, so the new arm must come before that construction. Restructure the top of `WorkerRunner::invoke`:

```cpp
	std::unique_lock< std::recursive_mutex > serialised {};
	if ( !module->threadSafe() && call.module_index < m_module_locks.size() )
		serialised = std::unique_lock< std::recursive_mutex > { *m_module_locks[ call.module_index ] };

	Json::Value body {};

	// Before ModuleCallData is built: this op has no file, and ModuleCallData holds a reference.
	if ( call.op == ipc::CallOp::EMBED_TEXT )
	{
		const auto embedder { std::static_pointer_cast< EmbeddingModuleI >( module ) };

		if ( !embedder->supportsText() )
			return std::unexpected( std::string { "this model has no text encoder" } );

		auto result { embedder->embedText( call.phrase ) };
		if ( !result ) return std::unexpected( result.error() );

		if ( result->m_vector.size() != embedder->dimensions() )
			return std::unexpected(
				std::format(
					"embedding module '{}' returned {} values but declares {} dimensions",
					embedder->modelName(),
					result->m_vector.size(),
					embedder->dimensions() ) );

		Json::Value values { Json::arrayValue };
		for ( const float value : result->m_vector ) values.append( static_cast< double >( value ) );

		body[ ipc::field::EMBEDDING ] = std::move( values );
		return std::pair { std::move( body ), ipc::UniqueFd {} };
	}

	ModuleCallData data { .file = *call.file, .mime_name = call.mime, .extra = call.extra };
```

Then move the existing `switch ( call.op )` below, and add `case ipc::CallOp::EMBED_TEXT: break;` to it so the `-Wswitch-enum` build stays clean (it is unreachable, having returned above).

- [ ] **Step 6: Extend `describeResult` and the manifest builder**

In `describeResult` (anonymous namespace of `WorkerRunner.cpp`), add:

```cpp
		case ipc::CallOp::EMBED_TEXT:
			[[fallthrough]];
		case ipc::CallOp::EMBED:
			return std::format( "{}-dimension vector", body[ ipc::field::EMBEDDING ].size() );
```

Where the manifest is built (search `ModuleLibrary.cpp` for `model_name`), set `entry.supports_text` from
`std::static_pointer_cast< EmbeddingModuleI >( module )->supportsText()` for EMBEDDING modules.

- [ ] **Step 7: Add the server-side proxy**

In `IDHANServer/src/modules/RemoteModule.hpp`, beside `embed`:

```cpp
	//! Whether this model has a text tower, as reported by its manifest.
	[[nodiscard]] bool supportsText() const { return m_supports_text; }

	//! Embeds a phrase into the same space as embed(). Carries no file.
	[[nodiscard]] IDHANTask< std::expected< EmbeddingInfo, ModuleError > > embedText( std::string phrase ) const;
```

Add `bool m_supports_text { false };` to the members and populate it from the manifest entry wherever `m_model_name` and `m_dimensions` are set.

In `IDHANServer/src/modules/RemoteModule.cpp`:

```cpp
IDHANTask< std::expected< EmbeddingInfo, ModuleError > > RemoteModule::embedText( std::string phrase ) const
{
	if ( !m_supports_text ) co_return std::unexpected( ModuleError { "this model has no text encoder" } );

	Json::Value body {};
	body[ ipc::field::TYPE ] = std::string { toString( ipc::MessageType::CALL ) };
	body[ ipc::field::OP ] = std::string { toString( ipc::CallOp::EMBED_TEXT ) };
	body[ ipc::field::MODULE_INDEX ] = Json::UInt64 { m_module_index };
	body[ ipc::field::PHRASE ] = std::move( phrase );

	// nullptr input: this is the one call with no file to send.
	auto outcome { co_await m_pool->dispatch( std::move( body ), nullptr ) };

	if ( !outcome->ok ) co_return std::unexpected( ModuleError { outcome->error } );

	const auto& values { outcome->body[ ipc::field::EMBEDDING ] };
	if ( !values.isArray() ) co_return std::unexpected( ModuleError { "embed_text result carried no vector" } );

	if ( values.size() != m_dimensions )
		co_return std::unexpected(
			ModuleError { std::format(
				"embed_text result for model '{}' had {} values, expected {}",
				m_model_name,
				values.size(),
				m_dimensions ) } );

	EmbeddingInfo info {};
	info.m_vector.reserve( values.size() );

	for ( const auto& value : values )
	{
		if ( !value.isNumeric() )
			co_return std::unexpected( ModuleError { "embed_text result had a non-numeric value" } );

		info.m_vector.push_back( static_cast< float >( value.asDouble() ) );
	}

	co_return info;
}
```

`baseBody` is not reused because it stamps MIME and input-related fields this call has none of. Inspect `RemoteModule::baseBody` and `WorkerPool::dispatch` and adapt: if `dispatch` cannot accept a null input, give it an overload that sends no descriptor rather than fabricating an empty one.

- [ ] **Step 8: Build and verify nothing regressed**

```bash
cmake --build build/debug --target IDHANServer IDHANModuleRunner IDHANEmbedding
./build/bin/IDHANModuleRunner --library build/debug/bin/modules/libIDHANEmbedding.so --describe
```

Expected: builds clean; `--describe` prints a manifest including `"supports_text": false`.

- [ ] **Step 9: Commit**

```bash
git add IDHANModules/ipc IDHANModules/include/EmbeddingModule.hpp IDHANModules/runner-src \
        IDHANServer/src/modules/RemoteModule.hpp IDHANServer/src/modules/RemoteModule.cpp
git commit -m "feat(modules): add EMBED_TEXT, the first fileless module call"
```

---

### Task 6: Export the text tower

**Depends on Task 1's decision.**

**Files:**
- Modify: `tools/embedding-export/export_siglip2.py`
- Modify: `Dockerfile:138-172` (export stage), `:215-216` (copy stage)

**Interfaces:**
- Consumes: Task 1's decision.
- Produces, in each model directory: `text.onnx` and `text_parity.json`, plus `model.json` keys
  `text_onnx`, `text_input_name`, `text_output_name`, `context_length`, `text_takes_strings`.
  Task 7 reads all of these.

- [ ] **Step 1: Add the text encoder wrapper and the `--with-text` flag**

In `tools/embedding-export/export_siglip2.py`, beside the existing `ImageEncoder`:

```python
class TextEncoder(torch.nn.Module):
    """encode_text plus the L2 normalisation, mirroring ImageEncoder.

    Folded in for the same reason: no consumer can then forget it, and the module's norm check
    exists to catch a bad export rather than to do the work.
    """

    def __init__(self, model):
        super().__init__()
        self.model = model

    def forward(self, input_ids):
        features = self.model.encode_text(input_ids)
        return torch.nn.functional.normalize(features, dim=-1)
```

Add `parser.add_argument("--with-text", action="store_true", help="Also export the text tower")`.

- [ ] **Step 2: Export the text graph**

After the image export, when `args.with_text` is set:

```python
    context_length = int(model_cfg["text_cfg"]["context_length"])
    tokenizer = open_clip.get_tokenizer(f"local-dir:{model_dir}")

    text_wrapper = TextEncoder(model)
    text_wrapper.eval()

    text_onnx_path = out_dir / "text.onnx"
    example_ids = tokenizer(["a photo of a cat", "a photo of a dog"])

    print(f"exporting text tower to {text_onnx_path}")
    with torch.no_grad():
        torch.onnx.export(
            text_wrapper,
            example_ids,
            str(text_onnx_path),
            input_names=["input_ids"],
            output_names=["text_features"],
            dynamic_axes={"input_ids": {0: "batch"}, "text_features": {0: "batch"}},
            opset_version=args.opset,
            do_constant_folding=True,
            dynamo=False,
        )
```

**If Task 1 chose `extensions`:** additionally prepend the tokenizer op so the graph takes strings, set
`model_json["text_takes_strings"] = True`, and name the input `text`. **If Task 1 chose `tokenizers-cpp`:**
leave the graph taking `input_ids` and set `text_takes_strings` to `False`.

- [ ] **Step 3: Write the parity sidecar**

This is the mechanism that turns a silently-wrong tokenizer into a loud failure, so it is not optional:

```python
    # The module re-embeds these at startup and compares. A tokenizer that is subtly wrong produces
    # a plausible vector and no error anywhere -- this is the only thing that makes that visible.
    parity_phrases = ["catgirl", "a photo of a dog", "blurry", "1girl, solo"]

    with torch.no_grad():
        parity_ids = tokenizer(parity_phrases)
        parity_vectors = text_wrapper(parity_ids).tolist()

    parity_path = out_dir / "text_parity.json"
    with parity_path.open("w") as handle:
        json.dump(
            {"phrases": parity_phrases, "vectors": parity_vectors},
            handle,
        )
        handle.write("\n")

    print(f"wrote {parity_path}")
```

Extend `model_json` before it is written:

```python
    if args.with_text:
        model_json["text_onnx"] = "text.onnx"
        model_json["text_input_name"] = "text" if model_json.get("text_takes_strings") else "input_ids"
        model_json["text_output_name"] = "text_features"
        model_json["context_length"] = context_length
```

- [ ] **Step 4: Verify the export runs**

```bash
/home/kj16609/Desktop/Projects/cxx/LanceProject/.venv/bin/python \
  tools/embedding-export/export_siglip2.py \
  --model-dir /home/kj16609/Desktop/Projects/cxx/LanceProject/models/ViT-B-16-SigLIP2 \
  --out /tmp/claude-1000/-home-kj16609-Desktop-Projects-cxx-IDHAN/e8ed4cf5-3177-450b-9ae5-5ca02d8ba71b/scratchpad/models \
  --with-text
```

Expected: writes `model.onnx`, `text.onnx`, `model.json`, `text_parity.json`. Confirm `model.json` has all five text keys and `text_parity.json` has 4 phrases with 768 floats each.

- [ ] **Step 5: Update the Dockerfile**

Pass `--with-text` to the export at `Dockerfile:172`. If Task 1 chose `extensions`, add
`onnxruntime-extensions` to the modelbuilder stage's pip install and copy `libortextensions.so` into the
runtime image alongside the models at `Dockerfile:216`.

- [ ] **Step 6: Commit**

```bash
git add tools/embedding-export/export_siglip2.py Dockerfile
git commit -m "feat(embeddings): export the SigLIP2 text tower with a parity sidecar"
```

---

### Task 7: Text tower in the module

**Depends on Tasks 1, 5 and 6.**

**Files:**
- Modify: `IDHANModules/premade/embedding/ModelConfig.hpp`, `ModelConfig.cpp`
- Modify: `IDHANModules/premade/embedding/OnnxEmbedder.hpp`, `OnnxEmbedder.cpp`

**Interfaces:**
- Consumes: `model.json` text keys and `text_parity.json` from Task 6; `supportsText`/`embedText` from Task 5.
- Produces: a working `OnnxEmbedder::supportsText()` and `OnnxEmbedder::embedText()`.

- [ ] **Step 1: Extend the model config**

In `ModelConfig.hpp`, add to `ModelConfig`:

```cpp
	//! Empty when the model ships image-only, which is a normal configuration.
	std::filesystem::path m_text_onnx_path {};
	std::string m_text_input_name { "input_ids" };
	std::string m_text_output_name { "text_features" };
	std::size_t m_context_length { 64 };
	//! True when the tokenizer is baked into the graph and it takes raw strings.
	bool m_text_takes_strings { false };

	//! Phrases and the vectors Python produced for them, for the startup parity check.
	std::vector< std::string > m_parity_phrases {};
	std::vector< std::vector< float > > m_parity_vectors {};
```

In `ModelConfig.cpp`, read the five `model.json` keys, resolving `text_onnx` relative to the model
directory and only accepting it when the file exists. Then load `text_parity.json` from the same
directory if present. A missing text graph or sidecar is not an error — the model loads image-only.

- [ ] **Step 2: Add the text session**

In `OnnxEmbedder.hpp`:

```cpp
	//! Null when the model ships image-only or failed its parity check.
	std::unique_ptr< Ort::Session > m_text_session {};

	//! Runs one text forward pass. Separate from runOne: different graph, different input type.
	[[nodiscard]] std::expected< std::vector< float >, idhan::ModuleError > runText( std::string_view phrase );

	//! Re-embeds the exported phrases and compares. \return an error describing the first mismatch.
	[[nodiscard]] std::expected< void, idhan::ModuleError > checkTextParity();
```

and the overrides:

```cpp
	[[nodiscard]] bool supportsText() override { return m_text_session != nullptr; }

	[[nodiscard]] std::expected< idhan::EmbeddingInfo, idhan::ModuleError > embedText( std::string_view phrase ) override;
```

- [ ] **Step 3: Build the text session in `startup()`**

Append to `OnnxEmbedder::startup()`, after the image checks:

```cpp
	if ( m_config.m_text_onnx_path.empty() )
	{
		spdlog::info( "Model '{}' ships image-only; text queries will be refused", m_config.m_model_name );
		return;
	}

	try
	{
		Ort::SessionOptions text_options {};
		text_options.SetGraphOptimizationLevel( GraphOptimizationLevel::ORT_ENABLE_ALL );
		text_options.SetIntraOpNumThreads( 1 );

		// Only when the tokenizer lives in the graph. Skipped otherwise, so a build without the
		// extensions library still serves image-only models.
		if ( m_config.m_text_takes_strings ) registerTokenizerOps( text_options );

		m_text_session = std::make_unique< Ort::Session >( *m_env, m_config.m_text_onnx_path.c_str(), text_options );
	}
	catch ( const Ort::Exception& e )
	{
		// Degraded, not fatal: image embedding and record-reference search are unaffected, and a
		// server that refuses to start over an optional text tower is worse than one that says so.
		spdlog::error( "Model '{}' has a text tower that would not load: {}. Text queries will be refused",
		               m_config.m_model_name, e.what() );
		m_text_session.reset();
		return;
	}

	// The whole reason the sidecar exists. A tokenizer that disagrees with the one the model was
	// trained with returns plausible vectors and no error, so failing here is the only signal.
	if ( const auto parity { checkTextParity() }; !parity )
	{
		spdlog::error( "Model '{}' failed its text parity check: {}. Text queries will be refused",
		               m_config.m_model_name, parity.error() );
		m_text_session.reset();
		return;
	}

	spdlog::info( "Model '{}' text tower ready and parity-checked", m_config.m_model_name );
```

`registerTokenizerOps` is a small helper in the anonymous namespace calling
`options.RegisterCustomOpsLibrary( path )`, where the path comes from `IDHAN_ORT_EXTENSIONS_LIB` or a
default beside the runner binary. Implement it per Task 1's finding.

- [ ] **Step 4: Implement `checkTextParity` and `embedText`**

```cpp
std::expected< void, ModuleError > OnnxEmbedder::checkTextParity()
{
	if ( m_config.m_parity_phrases.empty() )
		return std::unexpected( ModuleError { "no text_parity.json shipped with this model" } );

	for ( std::size_t index = 0; index < m_config.m_parity_phrases.size(); ++index )
	{
		const auto produced { runText( m_config.m_parity_phrases[ index ] ) };
		if ( !produced ) return std::unexpected( produced.error() );

		const auto& expected { m_config.m_parity_vectors[ index ] };

		if ( produced->size() != expected.size() )
			return std::unexpected( ModuleError { std::format(
				"phrase '{}' produced {} values, sidecar has {}",
				m_config.m_parity_phrases[ index ], produced->size(), expected.size() ) } );

		float dot { 0.0f };
		for ( std::size_t axis = 0; axis < expected.size(); ++axis ) dot += ( *produced )[ axis ] * expected[ axis ];

		// Both sides are unit vectors, so the dot product is the cosine. A different tokenisation
		// moves this far more than fp accumulation across runtimes does.
		if ( dot < 0.999f )
			return std::unexpected( ModuleError { std::format(
				"phrase '{}' embeds to cosine {} against the exported vector; the tokenizer disagrees "
				"with the one this model was exported with",
				m_config.m_parity_phrases[ index ], dot ) } );
	}

	return {};
}

std::expected< EmbeddingInfo, ModuleError > OnnxEmbedder::embedText( const std::string_view phrase )
{
	if ( m_text_session == nullptr ) return std::unexpected( ModuleError { "this model has no text encoder" } );

	auto vector { runText( phrase ) };
	if ( !vector ) return std::unexpected( vector.error() );

	EmbeddingInfo info {};
	info.m_vector = std::move( *vector );
	return info;
}
```

And `runText`, which mirrors `runOne` but differs in its input type:

```cpp
std::expected< std::vector< float >, ModuleError > OnnxEmbedder::runText( const std::string_view phrase )
{
	try
	{
		const std::array< std::int64_t, 1 > shape { { 1 } };

		// Held outside the branch: Ort borrows this buffer rather than copying it, so it has to
		// outlive the Run below.
		const std::string text { phrase };
		const std::array< const char*, 1 > strings { { text.c_str() } };

		auto input_tensor { m_config.m_text_takes_strings ?
			                    Ort::Value::CreateTensor(
								    Ort::AllocatorWithDefaultOptions {},
								    shape.data(),
								    shape.size(),
								    ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING ) :
			                    Ort::Value {} };

		if ( m_config.m_text_takes_strings )
		{
			input_tensor.FillStringTensor( strings.data(), strings.size() );
		}
		else
		{
			// Token-id path, taken when the tokenizer lives outside the graph.
			return std::unexpected(
				ModuleError { "this build tokenizes outside the graph, which is not implemented" } );
		}

		const std::array< const char*, 1 > input_names { { m_config.m_text_input_name.c_str() } };
		const std::array< const char*, 1 > output_names { { m_config.m_text_output_name.c_str() } };

		auto outputs { m_text_session->Run(
			Ort::RunOptions { nullptr }, input_names.data(), &input_tensor, 1, output_names.data(), 1 ) };

		const float* const data { outputs.front().GetTensorData< float >() };

		std::vector< float > vector( data, data + m_config.m_dimensions );

		float sum { 0.0f };
		for ( const float value : vector ) sum += value * value;

		const float norm { std::sqrt( sum ) };

		// The export folds normalisation in, exactly as it does for the image tower, so a norm that
		// is not 1 means the graph is not the one this module thinks it is.
		if ( std::abs( norm - 1.0f ) > NORM_TOLERANCE )
			return std::unexpected(
				ModuleError { std::format(
					"text tower for '{}' produced a vector of norm {}", m_config.m_model_name, norm ) } );

		return vector;
	}
	catch ( const Ort::Exception& e )
	{
		return std::unexpected( ModuleError { std::format( "onnxruntime failed on text: {}", e.what() ) } );
	}
}
```

If Task 1 chose `tokenizers-cpp`, replace the `return std::unexpected(...)` placeholder branch with the
tokenizer call producing `int64[1, m_context_length]` ids, padded to `m_context_length`, and build the
input tensor from those instead. The rest of the function is unchanged.

- [ ] **Step 5: Verify against the exported model**

```bash
cmake --build build/debug --target IDHANEmbedding IDHANModuleRunner
IDHAN_EMBEDDING_MODELS=/tmp/claude-1000/-home-kj16609-Desktop-Projects-cxx-IDHAN/e8ed4cf5-3177-450b-9ae5-5ca02d8ba71b/scratchpad/models \
  ./build/debug/bin/IDHANModuleRunner --library build/debug/bin/modules/libIDHANEmbedding.so --describe
```

Expected: manifest reports `"supports_text": true`. Startup logs show `text tower ready and parity-checked`
and no parity error.

- [ ] **Step 6: Prove the parity check actually catches a mismatch**

Corrupt one vector in the scratchpad copy of `text_parity.json` and re-run Step 5.

Expected: logs `failed its text parity check`, and the manifest reports `"supports_text": false`. Restore
the file afterwards. A parity check that cannot fail is worth nothing, so do not skip this step.

- [ ] **Step 7: Commit**

```bash
git add IDHANModules/premade/embedding
git commit -m "feat(embeddings): add the text tower with a startup parity check"
```

---

### Task 8: Text terms in search

**Depends on Tasks 4, 5 and 7.**

**Files:**
- Modify: `IDHANServer/src/embeddings/searchEmbeddings.hpp`, `searchEmbeddings.cpp`
- Modify: `IDHANServer/src/api/embeddings/embeddingEndpoints.cpp` (`listModels`, and the `search` call site)

**Interfaces:**
- Consumes: `RemoteModule::embedText`, `RemoteModule::supportsText` from Task 5.
- Produces: `searchEmbeddings` gains a leading `std::shared_ptr< modules::RemoteModule > module` parameter
  (may be null when the query has no text terms). `GET /embeddings/models` gains a `supports_text` boolean.

- [ ] **Step 1: Write the failing test**

Append to `tests/src/server/embeddingSearchTest.cpp`:

```cpp
//! A model with no text tower must say so up front rather than failing mid-query.
TEST( EmbeddingSearch, ModelsReportTextSupport )
{
	SERVER_HANDLE( server );

	auto db { server.getDbClient() };
	db->execSqlSync(
		"INSERT INTO embedding_models (model_name, model_dimensions) VALUES ($1, $2)", "text-flag-model", 4 );

	const auto response { server.get( "/embeddings/models" ) };

	ASSERT_EQ( response.status, 200 ) << response.text;

	bool found { false };
	for ( const auto& model : response.json )
	{
		if ( model[ "model_name" ].asString() != "text-flag-model" ) continue;

		found = true;
		ASSERT_TRUE( model.isMember( "supports_text" ) ) << "listModels must report text support";
		EXPECT_FALSE( model[ "supports_text" ].asBool() ) << "no module is loaded, so text is unsupported";
	}

	EXPECT_TRUE( found );
}

//! A text term against a model with no text tower is a clear 400, not a 500.
TEST( EmbeddingSearch, TextTermAgainstImageOnlyModelIsRejected )
{
	SERVER_HANDLE( server );

	auto db { server.getDbClient() };
	db->execSqlSync(
		"INSERT INTO embedding_models (model_name, model_dimensions) VALUES ($1, $2)", "no-text-model", 4 );

	Json::Value term {};
	term[ "type" ] = "text";
	term[ "text" ] = "catgirl";
	term[ "weight" ] = 0.5;

	Json::Value body {};
	body[ "model_name" ] = "no-text-model";
	body[ "terms" ].append( term );

	const auto response { server.post( "/embeddings/search", body ) };

	EXPECT_EQ( response.status, 400 );
	EXPECT_NE( response.text.find( "no-text-model" ), std::string::npos ) << "the error must name the model";
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build/debug --target IDHANTests
./build/bin/IDHANTests --gtest_filter="EmbeddingSearch.ModelsReportTextSupport:EmbeddingSearch.TextTermAgainstImageOnlyModelIsRejected" --testmode --use_stdout
```

Expected: FAIL — `supports_text` absent; the text-term error does not name the model.

- [ ] **Step 3: Report text support from `listModels`**

In `embeddingEndpoints.cpp`, in `listModels`, beside the existing `available` line:

```cpp
		model[ "available" ] = module != nullptr;
		// The panel needs this to decide whether to enable its text input. Discovering it by
		// submitting a query that 400s is not an acceptable substitute.
		model[ "supports_text" ] = module != nullptr && module->supportsText();
```

- [ ] **Step 4: Resolve text terms**

Add the module parameter to `searchEmbeddings` in both the header and the implementation, then replace
the rejecting branch:

```cpp
		if ( term.m_is_text )
		{
			// Checked per term rather than once up front so the message can name the phrase that
			// could not be resolved.
			const auto embedded { co_await module->embedText( term.m_text ) };

			if ( !embedded )
				co_return std::unexpected(
					createBadRequest( "Could not embed \"{}\": {}", term.m_text, embedded.error() ) );

			resolved.emplace_back(
				WeightedVector { .m_vector = embedded->m_vector, .m_weight = term.m_weight } );
			continue;
		}
```

At the call site in the handler, resolve the module and check it before dispatching:

```cpp
	const auto has_text_term { std::ranges::any_of( terms, []( const auto& term ) { return term.m_is_text; } ) };

	std::shared_ptr< modules::RemoteModule > module {};

	if ( has_text_term )
	{
		module = modules::ModuleLoader::instance().getEmbedderFor( model_name );

		if ( module == nullptr )
			co_return createNotFound( "No loaded module provides the model \"{}\", so text terms cannot be embedded", model_name );

		if ( !module->supportsText() )
			co_return createBadRequest(
				"The model \"{}\" has no text encoder; use record references instead", model_name );
	}
```

Record-only queries deliberately leave `module` null and never touch the module system at all.

Then update the call itself — `searchEmbeddings` now takes the module first:

```cpp
	const auto hits { co_await embeddings::searchEmbeddings(
		module, model_id, dimensions, std::move( terms ), limit, ef_search, db ) };
	return_unexpected_error( hits );
```

and the declaration in `searchEmbeddings.hpp` gains the matching leading parameter:

```cpp
[[nodiscard]] ExpectedTask< std::vector< SearchHit > > searchEmbeddings(
	std::shared_ptr< modules::RemoteModule > module,
	std::int32_t model_id,
	std::size_t dimensions,
	std::vector< QueryTerm > terms,
	std::size_t limit,
	std::size_t ef_search,
	DbClientPtr db );
```

Null is a valid argument and means "this query has no text terms"; the handler above guarantees a
non-null module whenever one is needed, so `searchEmbeddings` never has to check.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build/debug --target IDHANServer IDHANTests
./build/bin/IDHANTests --gtest_filter="EmbeddingSearch.*" --testmode --use_stdout
```

Expected: PASS, 7 tests.

- [ ] **Step 6: Commit**

```bash
git add IDHANServer/src/embeddings IDHANServer/src/api/embeddings/embeddingEndpoints.cpp \
        tests/src/server/embeddingSearchTest.cpp
git commit -m "feat(embeddings): resolve text terms in search"
```

---

### Task 9: Embedding Search panel

**Files:**
- Create: `IDHANWeb/src/panels/builtins/EmbeddingSearchPanel.tsx`
- Create: `IDHANWeb/src/panels/builtins/embeddingTerms.ts`
- Create: `IDHANWeb/src/panels/builtins/embeddingTerms.test.ts`
- Modify: `IDHANWeb/src/panels/builtins/index.ts`

**Interfaces:**
- Consumes: `POST /embeddings/search` and `GET /embeddings/models` from Tasks 4 and 8.
- Produces: `embeddingSearchPanel`, registered in the builtin catalog.

- [ ] **Step 1: Write the failing parse test**

Create `IDHANWeb/src/panels/builtins/embeddingTerms.test.ts`:

```ts
import { describe, expect, it } from 'vitest';
import { parseTermInput } from './embeddingTerms';

describe('parseTermInput', () => {
  it('reads a trailing numeric suffix as the weight', () => {
    expect(parseTermInput('catgirl:0.5')).toEqual({
      kind: 'text', text: 'catgirl', weight: 0.5, positive: true, enabled: true,
    });
  });

  it('keeps colons in the phrase when only the last part is numeric', () => {
    expect(parseTermInput('character:hatsune miku:0.8')).toEqual({
      kind: 'text', text: 'character:hatsune miku', weight: 0.8, positive: true, enabled: true,
    });
  });

  it('treats a leading minus as a negative term', () => {
    expect(parseTermInput('-blurry:0.3')).toEqual({
      kind: 'text', text: 'blurry', weight: 0.3, positive: false, enabled: true,
    });
  });

  it('defaults the weight to 1 when there is no suffix', () => {
    expect(parseTermInput('catgirl')).toEqual({
      kind: 'text', text: 'catgirl', weight: 1, positive: true, enabled: true,
    });
  });

  it('leaves a non-numeric suffix in the phrase', () => {
    expect(parseTermInput('rating:safe')).toEqual({
      kind: 'text', text: 'rating:safe', weight: 1, positive: true, enabled: true,
    });
  });

  it('rejects empty input', () => {
    expect(parseTermInput('   ')).toBeNull();
  });
});
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd IDHANWeb && pnpm vitest run src/panels/builtins/embeddingTerms.test.ts
```

Expected: FAIL — cannot resolve `./embeddingTerms`.

- [ ] **Step 3: Write the term module**

Create `IDHANWeb/src/panels/builtins/embeddingTerms.ts`:

```ts
/**
 * The term model behind the Embedding Search panel, and the shorthand that produces it.
 *
 * Text terms are free text. They are NOT tags and never reach the tag tables — embedding search is a
 * separate system, so `rating:safe` here is a phrase, not a namespaced tag.
 */

export interface TextTerm {
  kind: 'text';
  text: string;
  weight: number;
  positive: boolean;
  enabled: boolean;
}

export interface RecordTerm {
  kind: 'record';
  recordId: number;
  weight: number;
  positive: boolean;
  enabled: boolean;
}

export type Term = TextTerm | RecordTerm;

/**
 * Parses `phrase:weight` shorthand.
 *
 * The weight is taken only when what follows the FINAL colon parses as a number, so a phrase may
 * contain colons of its own. A leading `-` makes the term negative.
 */
export function parseTermInput(input: string): TextTerm | null {
  let rest = input.trim();
  if (!rest) return null;

  let positive = true;
  if (rest.startsWith('-')) {
    positive = false;
    rest = rest.slice(1).trim();
    if (!rest) return null;
  }

  let weight = 1;
  const lastColon = rest.lastIndexOf(':');

  if (lastColon > 0) {
    const suffix = rest.slice(lastColon + 1).trim();
    // Number() accepts '' and whitespace as 0, which would silently eat a trailing colon.
    const parsed = suffix.length > 0 ? Number(suffix) : NaN;

    if (Number.isFinite(parsed)) {
      weight = parsed;
      rest = rest.slice(0, lastColon).trim();
    }
  }

  if (!rest) return null;

  return { kind: 'text', text: rest, weight, positive, enabled: true };
}

/** The request body shape POST /embeddings/search expects. */
export function termsToRequest(terms: readonly Term[]): Array<Record<string, unknown>> {
  return terms
    .filter((term) => term.enabled)
    .map((term) => {
      const weight = term.positive ? term.weight : -term.weight;
      return term.kind === 'text'
        ? { type: 'text', text: term.text, weight }
        : { type: 'record', record_id: term.recordId, weight };
    });
}

/** Human-readable tokens for the results header, matching the shorthand that produces them. */
export function termsToTokens(terms: readonly Term[]): string[] {
  return terms
    .filter((term) => term.enabled)
    .map((term) => {
      const sign = term.positive ? '' : '-';
      const body = term.kind === 'text' ? term.text : `record:${term.recordId}`;
      return term.weight === 1 ? `${sign}${body}` : `${sign}${body}:${term.weight}`;
    });
}
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd IDHANWeb && pnpm vitest run src/panels/builtins/embeddingTerms.test.ts
```

Expected: PASS, 6 tests.

- [ ] **Step 5: Write the panel**

Create `IDHANWeb/src/panels/builtins/EmbeddingSearchPanel.tsx`. Read `SearchPanel.tsx` first and follow
its structure — config read/write, the request hook, and the `host.results.set` call at line 191.

Requirements:

- Model selector from `GET /embeddings/models`. Disable the text input when the selected model reports
  `supports_text: false`, showing why rather than hiding the control.
- A text input; Enter runs `parseTermInput` and appends the resulting row.
- An "Add selection as reference" button appending one `RecordTerm` per id from `host.selection.get()`.
- Each row: weight number input, `+`/`-` toggle, enable checkbox, remove button.
- Run posts `{ model_name, terms: termsToRequest(terms), limit }` and publishes:

```ts
host.results.set({
  ids: Int32Array.from(response.record_ids),
  queryMs: response.query_ms,
  query: termsToTokens(terms),
});
```

- Persist `{ modelName, terms, limit }` through the panel config so a tuned query survives reload.
- Export the definition:

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

- [ ] **Step 6: Register the panel**

In `IDHANWeb/src/panels/builtins/index.ts`, add the import beside the others and
`registerPanel(embeddingSearchPanel);` inside `registerBuiltinPanels`. Update the file's header comment,
which enumerates the catalog, to include Embedding Search.

- [ ] **Step 7: Verify the build and the suite**

```bash
cd IDHANWeb && pnpm vitest run && pnpm build
```

Expected: all tests pass; the production build succeeds.

- [ ] **Step 8: Commit**

```bash
git add IDHANWeb/src/panels/builtins/EmbeddingSearchPanel.tsx \
        IDHANWeb/src/panels/builtins/embeddingTerms.ts \
        IDHANWeb/src/panels/builtins/embeddingTerms.test.ts \
        IDHANWeb/src/panels/builtins/index.ts
git commit -m "feat(web): add the Embedding Search panel"
```

---

## Verification

After Task 9, the whole feature should be exercisable end to end:

1. Start the server with an embedding model present and run a backfill from the Embeddings panel.
2. Open the Embedding Search panel, pick the model.
3. Type `catgirl:0.5`, press Enter, run — results appear in the grid.
4. Select a record in the grid, click "Add selection as reference", run — results shift toward it.
5. Flip that row to `-`, run — results shift away from it.

Only steps 1-2 and 4-5 are meaningful if Task 1 concluded that no text path is viable; the record-reference
half of the feature stands alone by design.
