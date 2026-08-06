# SearchBuilder rewrite: record-id Set algebra in C++

**Date:** 2026-08-06
**Status:** Draft, pending review

Replaces `SearchBuilder`'s generated-SQL CTE chain with an in-memory set algebra. Postgres returns
flat `(record_id, sort_key)` lists from simple indexed queries; every intersection, union,
difference, negation, sort and page slice happens in C++.

## 1. Motivation

### 1.1 What exists today

`SearchBuilder::construct()` (`IDHANServer/src/core/search/SearchBuilder.cpp:654`) emits one large
SQL string. Each positive and negative tag becomes a named CTE (`filter_<tag_id>`), each namespace
wildcard another (`filter_namespace_<id>`), each subtag wildcard another
(`filter_wildcard_<n>`); those are combined with `INTERSECT` / `UNION DISTINCT` / `EXCEPT` into a
`final_filter` CTE, which is then joined against `file_info`, `records`, `video_metadata`,
`image_metadata`, `image_project_metadata` and an ad-hoc tag-count subquery according to a flag
struct, filtered by system predicates, ordered, and limited.

The consequences are structural, not cosmetic:

- **The CTE names are the tag ids.** Two occurrences of the same tag would declare the same CTE
  name twice, which Postgres rejects — so `addPositiveTags()` must sort-and-dedupe as a
  *correctness* requirement, and `mergeUnique()` carries a comment saying so
  (`SearchBuilder.cpp:1032`).
- **Ordering constraints between CTEs are encoded in string-append order.** The namespace filters
  reference `positive_filter`, so they must be appended after it; the wildcard filters must be
  appended before it. Both facts live only in comments at `SearchBuilder.cpp:733` and `:748`.
- **The query shape is untestable in isolation.** Nothing can be verified without a live Postgres
  and a populated schema, so every search test is an integration test.
- **Boolean structure is not expressible.** Positives `INTERSECT`, negatives `UNION` then one
  `EXCEPT`. There is no way to write `(a OR b) AND c`, and no obvious place to add one.
- **A fast path exists to dodge the whole thing.** `construct()` opens with a branch
  (`SearchBuilder.cpp:672`) that skips the CTE chain when no filters are set, because the machinery
  is too expensive for the "browse everything" case the grid hits on first load.

### 1.2 What this buys

Logic moves to C++, where it can be unit-tested without a database, where boolean structure is a
data structure rather than string concatenation order, and where the expensive part — combining
large id lists — is a linear merge over sorted vectors instead of a planner-dependent CTE chain.

SQL's remaining job is to answer one question per term: *which records carry this, and what is
their sort key?*

## 2. Decisions taken

Each of these was settled explicitly during design; they are recorded with their rationale because
several have real costs.

| Decision | Rationale |
|---|---|
| Sorted `std::vector<RecordID>`, not a compressed bitmap | Sets are bounded at ~1M records. 4 MB/set is comfortable; Roaring would add a dependency and indirection for no gain at this scale. |
| Negation is a lazy `inverted` flag, not an eager complement | An eager complement costs a full-table id scan per negated term. The flag costs a four-way branch per operation and never materialises the universe. |
| Sort keys travel *inside* the Set as a payload column | Removes the post-algebra key fetch entirely; the result arrives already in sort order, so `LIMIT` is a head slice. Cost: a heap join on every term's full candidate list, and a Set is only valid for the sort order it was built with. Strictly better for single-term searches, worse for multi-term ones. Accepted deliberately. |
| Hashes likewise, when requested | Same reasoning; makes materialisation zero-query in the common case. Cost: 32 bytes per candidate per term, only on hash-returning searches. |
| System predicates are Sets too | One concept end to end. `no duration` is just an inverted Set, identical to a negated tag. |
| `SearchBuilder` survives as a facade | All five call sites and three test files compile unchanged; the existing DB tests become the regression net proving behaviour did not shift. |

### 2.1 Naming

`and`, `or`, `xor` and `not` are alternative operator tokens in C++ — `Set::and(...)` does not
compile. The algebra is exposed as operators with named aliases:

```cpp
Set operator&( const Set& ) const;   Set intersect( const Set& ) const;
Set operator|( const Set& ) const;   Set unite( const Set& ) const;
Set operator^( const Set& ) const;   Set symmetricDifference( const Set& ) const;
Set operator~() const;               Set negate() const;
```

## 3. Architecture

```
IDHANServer/src/core/search/
  Set.hpp / .cpp           the value type and its algebra. No DB, no coroutines, no Drogon.
  SortKey.hpp / .cpp       key column variant, composite comparator, per-SortType SQL fragments.
  SetSource.hpp / .cpp     builds and runs the per-term queries; returns Sets.
  SearchBuilder.hpp / .cpp facade: parse -> expression -> evaluate -> materialise.
```

`Set` is deliberately free of every server dependency so its algebra is unit-testable without
Postgres. That is the main testability win of the rewrite and the boundary should be held.

### 3.1 The Set type

```cpp
class Set
{
    std::vector< RecordID >              m_ids;     //!< sorted by (key, record_id), unique
    SortKeyColumn                        m_keys;    //!< parallel to m_ids
    std::optional< std::vector< SHA256 > > m_hashes; //!< parallel; present iff requested
    bool                                 m_inverted { false };
};
```

`SortKeyColumn` is a variant *of columns*, never of scalars — a per-element variant would cost tag
and padding bytes on every entry:

```cpp
using SortKeyColumn = std::variant<
    std::monostate,                //!< RANDOM: no key; ordered by record_id alone
    std::vector< std::int64_t >,   //!< size, times, width, height, num_pixels, mime_id,
                                   //!<   has_audio, duration_ms, num_tags, framerate (fixed-point)
    std::vector< double >,         //!< ratio
    std::vector< SHA256 > >;       //!< hash
```

**Ordering is the composite `(key, record_id)`.** This is what makes the algebra legal. The set
algorithms require both operands ordered by the same strict-weak comparator; ordering by key alone
would not be a total order, but `record_id` is unique so the composite is. And the key is a
property of the *record*, not of the set, so both operands necessarily agree on it. Intersecting two
size-ordered Sets therefore yields a size-ordered Set.

Two Sets may only be combined when their key types match. A single query has one sort type
throughout, so a mismatch is a programming error, asserted rather than handled.

### 3.2 The algebra

With payload columns present, the operations cannot delegate to `std::ranges::set_intersection` and
friends — those move only the key range. Each operation is a hand-written merge that advances both
operands under the composite comparator and emits the id together with every payload column. Same
`O(n + m)` single pass; just explicit.

Negation is resolved by De Morgan rewriting at each operation:

| | pos ∘ pos | pos ∘ neg | neg ∘ pos | neg ∘ neg |
|---|---|---|---|---|
| `&` | `A∩B` pos | `A\B` pos | `B\A` pos | `A∪B` **neg** |
| `\|` | `A∪B` pos | `B\A` **neg** | `A\B` **neg** | `A∩B` **neg** |
| `^` | `A△B` pos | `A△B` **neg** | `A△B` **neg** | `A△B` pos |

`~` flips the flag and touches nothing else.

The universe is never constructed. A final Set that is still inverted becomes a `!= ALL(...)` clause
on the materialisation query (§3.5) — which converts "I need every record id in the database" into
"I need a NOT on a query I was running anyway".

The only search that can end inverted is one with no positive terms — a pure blacklist browse
(`-gore -scat`), which is a real and common Hydrus-style workflow. `A | ~B` is expressible but no
current API surface produces it.

### 3.3 Fetching a Set

One query per term. Resolution of tag strings to ids continues to go through the existing
`mapTags()` path, which resolves via the unique btree on `(namespace_id, subtag_id)`;
`tags.tag_text` carries only a GIN trgm index and is unsuitable for exact equality.

```sql
SELECT f.record_id, fi.size, r.sha256
FROM active_tag_mappings_final f
JOIN file_info fi USING (record_id)
JOIN records    r  USING (record_id)      -- only when return_hashes
WHERE f.tag_id = $1 AND f.tag_domain_id = ANY($2)
  AND fi.mime_id IS NOT NULL
ORDER BY fi.size, f.record_id
```

Four points:

- **The three-branch `UNION DISTINCT` in `createFilters()` is gone.** It predates the
  `active_tag_mappings_final` fix (`view_active_tag_mappings_final/`, which dropped the alias
  `COALESCE` from the parents branch). Both branches of the view are now index-only for a
  `tag_id = $1` lookup — 31 buffers / 0.449 ms where the old shape read 20338 buffers / 171 ms.
  A single lookup against the view is now the correct query.
- **`file_info` is always joined**, even when the sort key comes from another table, because
  `mime_id IS NOT NULL` is an unconditional filter today and must remain one.
- **The sort-key join is INNER** for the types where `generateSortFilterClause()` currently excludes
  NULL keys (`WIDTH`, `HEIGHT`, `RATIO`, `NUM_PIXELS`, `MODIFIED_TIME`). This preserves existing
  behaviour: a record with no resolution data is *excluded* from a width-sorted search, not sorted
  last.
- **`ORDER BY` stays in SQL.** Postgres is sorting the fetch regardless; asking for composite order
  costs nothing and the Set arrives ready to merge.

`active_tag_mappings_final` is a `UNION ALL` of two branches documented as disjoint. Rather than pay
`DISTINCT` over the full candidate list, duplicates are dropped during the C++ ingest — in composite
order they are adjacent, so the check is free.

Term fetches are independent and are issued concurrently. Per the lazy-coroutine pitfall in
`CLAUDE.md`, the coroutines stored for `drogon::when_all` must be captureless, with all state passed
as parameters.

### 3.4 The SortType table

`SetSource` owns the one piece of SQL generation that survives: a table mapping each `SortType` to
its join fragment, key expression, key column type, and whether the join is INNER or LEFT.

| SortType | Source | Key type | Join |
|---|---|---|---|
| `FILESIZE`, `IMPORT_TIME`, `MIME` | `file_info` | int64 | (always joined) |
| `MODIFIED_TIME` | `file_info` | int64 | INNER-equivalent; NULL excluded |
| `RECORD_TIME` | `records` | int64 | INNER |
| `HASH` | `records` | SHA256 | INNER |
| `DURATION`, `FRAMERATE`, `HAS_AUDIO` | `video_metadata` | int64 | INNER |
| `WIDTH`, `HEIGHT`, `NUM_PIXELS` | `COALESCE` over `image_metadata`, `video_metadata`, `image_project_metadata` | int64 | LEFT ×3, NULL excluded |
| `RATIO` | same three | double | LEFT ×3, NULL excluded |
| `NUM_TAGS` | count subquery over `active_tag_mappings_final` | int64 | LEFT, `COALESCE(..., 0)` |
| `RANDOM` | — | `monostate` | none |

`RANDOM` carries no key, so Sets order by `record_id` alone and the page is shuffled at
materialisation. As today, offset paging under `RANDOM` is inherently unstable.

### 3.5 Evaluation and materialisation

`SearchBuilder::evaluate()` fetches every term concurrently, then folds: positive terms with `&`,
negative terms with `~` then `&`, and the members of a wildcard group with `|` before the group
joins the fold.

Materialisation has three cases:

| Final Set | Cost |
|---|---|
| Not inverted | Slice `[offset, offset + limit)` off the front. Ids and hashes already in hand. **Zero queries.** |
| Inverted | One query over `file_info` (plus `records` when hashes are requested) with `record_id != ALL($1)`, `ORDER BY`, `LIMIT`, `OFFSET`. |
| No terms at all | The same query without the NOT clause — today's fast path, preserved. |

The explicit `m_limit` continues to win over a `system:limit` predicate, matching
`appendLimitOffset()`.

## 4. Behaviour that must not change

The existing DB tests are the contract. Specifically:

- Records with `mime_id IS NULL` never appear in results.
- NULL sort keys exclude the record for `WIDTH`, `HEIGHT`, `RATIO`, `NUM_PIXELS` and
  `MODIFIED_TIME`; `NUM_TAGS` instead coalesces to 0, because zero tags is a real answer.
- Ties break on `record_id` in the same direction as the primary sort, so paging is stable.
- An unknown tag, an unknown namespace, or a wildcard matching no tag returns 404 — not an empty
  result. Silently dropping the term would widen the search to whatever the other predicates
  matched.
- Aliases and parents are followed for wildcards exactly as for an explicitly typed tag.
- Positive terms intersect; negative terms union and are then subtracted.

## 5. Testing

**`Set` unit tests, no fixture, no database.** This is the capability the rewrite exists to create:

- All twelve dispatch cells of §3.2, each with hand-checked expected output.
- Empty operands on both sides of each operation; both-empty.
- Inverted results through materialisation, including the pure-blacklist case.
- Composite ordering preserved across every operation, including equal keys with differing ids.
- Payload columns stay aligned with `m_ids` after every operation — the invariant most likely to
  break under a hand-written merge.
- Key-type mismatch assertion fires.

**Existing DB tests unchanged.** `subtagWildcards`, `namespaceWildcards` and `sortTypes` must pass
without modification. If a test needs editing to pass, that is a behaviour change and needs
justification against §4.

**New DB tests** for the paths §4 lists that are not already covered: pure-negative search,
`mime_id IS NULL` exclusion, and paging stability across two adjacent pages.

## 6. Out of scope

- **Set caching across requests.** Payload-in-Set makes a Set valid only for the sort order it was
  built with, which guts the hit rate. Revisit only if profiling asks for it.
- **Cardinality-ordered lazy fetching.** `tag_counts` already knows each tag's size, so the smallest
  term could be fetched first and later terms restricted with `record_id = ANY(<current>)` — which
  would directly cancel the wide-side join cost §2 accepts. A real win, and the natural follow-up,
  but a second change on top of a working rewrite.
- **Nested boolean groups.** The algebra makes `(a OR b) AND c` expressible; this change adds no API
  surface for it.
- **The unimplemented predicates.** `setHydrusSystemTags()` currently returns `true` for filesize,
  filetype, hash, date, ratio, num-pixels, url and note predicates without doing anything. They stay
  unimplemented; the rewrite does not add them.
- **Batching the per-wildcard resolution loop.** `setWildcardTags()` issues one query per wildcard;
  it could be one `UNNEST` query for all of them. Worth doing, unrelated to this change.

## 7. Risks

**The wide-side join is a real regression for multi-term searches.** `cat AND long_hair` at 500k and
300k candidates pays a `file_info` heap join on 800k rows to sort the 20k that survive. §2 accepts
this in exchange for zero round trips after the algebra and a faster single-term path. The mitigation
is the cardinality-ordered fetching in §6, which should be measured before it is assumed necessary.

**Memory.** Worst case with hashes requested: 1M records × (4 + 8 + 32) bytes ≈ 44 MB per term, with
several terms live during a fold. Id-returning searches — the common path, and what the web UI
uses — carry 12 bytes per record.

**The hand-written merge is the correctness hot spot.** Every operation must permute all payload
columns identically to `m_ids`. This is exactly what the §5 alignment tests exist to catch.
