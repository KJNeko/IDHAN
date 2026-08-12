# Tag System Migrations & Trigger Review

> This system is hell, I'm not sorry if you are trying to understand this.

This document is a deep-dive description of every trigger and function that drives IDHAN's tag
resolution system: raw mappings → active (alias-resolved) mappings → parent propagation →
counts. It documents the *current* effective behaviour and walks through worked examples for the
cases that matter operationally.

Migrations execute in ascending numeric-prefix order at server startup. Each database object lives
in its own directory (`func_<name>/`, `table_<name>/`, `view_<name>/`), a trigger's `CREATE TRIGGER`
binding lives with its function, and the `NNN` prefix is global across all directories. See
`IDHANMigration/src/README.md` for the layout. All file references below are `directory/NNN-file.sql`
relative to `IDHANMigration/src/`.

## 1. Schema map

```
tag_namespaces ─┐
tag_subtags ────┼─→ tags (namespace_id, subtag_id, generated tag_text)
                 │
tag_domains ─────┼─→ tag_mappings   (record_id, tag_id, tag_domain_id)      -- raw, user-applied
                 │        │  ON DELETE CASCADE
                 │        ▼
                 ├─→ active_tag_mappings (record_id, tag_id, ideal_tag_id, tag_domain_id)
                 │        │  effective_tag_id = COALESCE(ideal_tag_id, tag_id)
                 │        ▼
                 ├─→ active_tag_mappings_parents (record_id, tag_id, origin_id, internal_count, tag_domain_id)
                 │
                 ├─→ tag_aliases  (aliased_id, alias_id, ideal_alias_id, tag_domain_id)
                 ├─→ tag_parents  (parent_id, ideal_parent_id, child_id, ideal_child_id, tag_domain_id)
                 ├─→ tag_siblings (older_id, younger_id, tag_domain_id)   -- see §5, read-only via the API
                 └─→ tag_counts   (tag_id, tag_domain_id, storage_count, display_count)

active_tag_mappings_final (view) = active_tag_mappings ∪ active_tag_mappings_parents (UNION ALL)
```

The two view branches resolve aliases at *different times*, which is easy to misread:

- The `active_tag_mappings` branch resolves at **read** time, `COALESCE(ideal_tag_id, tag_id)`.
- The `active_tag_mappings_parents` branch selects `tag_id` verbatim, because that column is kept
  alias-resolved at **write** time by `repair_parents_on_alias` (`func_repair_parents_on_alias/098-create.sql`).

`view_active_tag_mappings_final/034-create.sql` originally resolved the parents branch through a
`LEFT JOIN tag_aliases`; `099-rewrite.sql` dropped it once 098 closed the write path. That join was
not just redundant, it defeated the index: putting the predicate on the output of a join meant
`tag_id = <id>` could not be pushed down to `active_tag_mappings_parents (tag_id) INCLUDE (record_id)`
(`table_active_tag_mappings_parents/043-index.sql`). Measured on a 3M-row parents table with 195k
aliases, one lookup read 20338 buffers in 171 ms; without the COALESCE the same lookup is an
index-only scan at 31 buffers and 0.449 ms. The branches are disjoint by construction, so `UNION ALL`
needs no dedup.

`tag_mappings`, `tag_aliases`, and `tag_parents` are `PARTITION BY LIST (tag_domain_id)`; a
partition per domain is created automatically the moment a row is inserted into `tag_domains`
(`func_createtagdomainpartitions/031-create.sql`). `active_tag_mappings`,
`active_tag_mappings_parents`, and `tag_counts` are **not** partitioned; `tag_domain_id` is
just an indexed column there.

## 2. Concepts

- **Raw tag** — what the user directly applied (`tag_mappings`).
- **Ideal tag** — the fully-resolved alias target (`COALESCE(ideal_alias_id, alias_id)` /
  `COALESCE(ideal_tag_id, tag_id)` / `COALESCE(ideal_parent_id, parent_id)` /
  `COALESCE(ideal_child_id, child_id)` — the same pattern repeats across four tables). Every
  table that can point at an aliased tag has a `GENERATED ... VIRTUAL` `effective_*`/`ideal_*`
  convenience column following this pattern except `tag_parents`, which has the columns but no
  generated `effective_*` column.
- **Alias chain collapsing** — aliases are stored as a flat map, not a linked list. When `A→B`
  is inserted and `B→C` already exists, `A`'s row gets `ideal_alias_id = C` directly (not `B`).
  This is maintained incrementally by triggers so that `effective_tag_id` is always a single
  lookup away, never requiring a recursive walk at query time.
- **`internal_count` / `internal`** on `active_tag_mappings_parents` — a reference count. A
  parent tag can be reached through more than one route (e.g. two different applied tags share
  a common grandparent). `internal_count` counts how many *other* parent-tag rows are
  responsible for adding this one; `internal = (internal_count > 0)`. A row with
  `internal_count = 0` was added directly by a `tag_mappings`/`active_tag_mappings` entry; it is
  deleted outright when that origin is removed. A row with `internal_count > 0` is kept alive by
  other rows and is only decremented, never force-deleted, until its count reaches zero.

## 3. Trigger inventory (final, effective state)

Postgres binds a trigger to its function by OID, so a later `CREATE OR REPLACE FUNCTION` upgrades
every trigger already pointing at it without touching the `CREATE TRIGGER`. Where a function has
more than one file below, the later file is such a replacement.

Within one table and timing class, Postgres fires triggers in **name order**, and all row-level
triggers fire before any statement-level one. On `tag_aliases` AFTER INSERT that means
`tag_aliases_after_insert`, then `trg_repair_tag_mappings_ideals_insert`, then the statement-level
`repair_parents_on_alias_insert`.

| Table                         | Trigger                                   | Timing/Level                                                      | Function                                  | Defined in                                                |
|-------------------------------|-------------------------------------------|-------------------------------------------------------------------|-------------------------------------------|-----------------------------------------------------------|
| `tag_domains`                 | `trg_create_tag_domain_partitions`        | BEFORE INSERT, ROW                                                | `createtagdomainpartitions()`             | `func_createtagdomainpartitions/031-create.sql`           |
| `tag_mappings`                | `trg_tag_mappings_after_insert`           | AFTER INSERT, **STATEMENT** (`REFERENCING NEW TABLE new_rows`)    | `func_tag_mappings_after_insert()`        | `func_tag_mappings_after_insert/026-create.sql`           |
| `file_info`                   | `trg_tag_mappings_after_active_insert`    | AFTER INSERT, **STATEMENT** (`REFERENCING NEW TABLE new_rows`)    | `func_tag_mappings_after_active_insert()` | `func_tag_mappings_after_active_insert/027-create.sql`    |
| `tag_aliases`                 | `tag_aliases_before_insert`               | BEFORE INSERT, ROW                                                | `tag_aliases_before_insert_trigger()`     | `func_tag_aliases_before_insert_trigger/023-create.sql`   |
| `tag_aliases`                 | `tag_aliases_after_insert`                | AFTER INSERT, ROW                                                 | `tag_aliases_after_insert_trigger()`      | `func_tag_aliases_after_insert_trigger/022-create.sql`    |
| `tag_aliases`                 | `tag_aliases_after_delete`                | AFTER DELETE, ROW                                                 | `tag_aliases_after_delete_trigger()`      | `func_tag_aliases_after_delete_trigger/024-create.sql`    |
| `tag_aliases`                 | `tag_aliases_after_update`                | AFTER UPDATE, ROW                                                 | `tag_aliases_after_update_trigger()`      | `func_tag_aliases_after_update_trigger/025-create.sql`    |
| `tag_aliases`                 | `trg_repair_tag_mappings_ideals_insert`   | AFTER INSERT, ROW                                                 | `repair_tag_mappings_ideals_insert()`     | `func_repair_tag_mappings_ideals_insert/028-create.sql`   |
| `tag_aliases`                 | `trg_repair_tag_mappings_ideals_update`   | AFTER UPDATE, ROW                                                 | `repair_tag_mappings_ideals_update()`     | `func_repair_tag_mappings_ideals_update/029-create.sql`   |
| `tag_aliases`                 | `trg_repair_tag_mappings_ideals_delete`   | AFTER DELETE, ROW                                                 | `repair_tag_mappings_ideals_delete()`     | `func_repair_tag_mappings_ideals_delete/030-create.sql`   |
| `tag_aliases`                 | `repair_parents_on_alias_insert`          | AFTER INSERT, **STATEMENT** (`REFERENCING NEW TABLE new_aliases`) | `repair_parents_on_alias()`               | `func_repair_parents_on_alias/098-create.sql`             |
| `tag_aliases`                 | `repair_parents_on_alias_update`          | AFTER UPDATE, **STATEMENT** (`REFERENCING NEW TABLE new_aliases`) | `repair_parents_on_alias()`               | `func_repair_parents_on_alias/098-create.sql`             |
| `tag_parents`                 | `trg_tag_parents_before_insert`           | BEFORE INSERT, ROW                                                | `tag_parents_before_insert_trigger()`     | `func_tag_parents_before_insert_trigger/083-create.sql`   |
| `tag_parents`                 | `trg_check_parent_cycle`                  | BEFORE INSERT, ROW                                                | `check_parent_cycle()`                    | `func_check_parent_cycle/044-create.sql`                  |
| `tag_parents`                 | `trg_insert_parent_mapping`               | AFTER INSERT, ROW                                                 | `insert_parent_mapping()`                 | `func_insert_parent_mapping/045-create.sql`               |
| `tag_parents`                 | `trg_delete_parent_mapping`               | AFTER DELETE, ROW                                                 | `delete_parent_mapping()`                 | `func_delete_parent_mapping/046-create.sql`               |
| `active_tag_mappings`         | `trg_intercept_parent_mapping`            | BEFORE INSERT, ROW                                                | `intercept_parent_mapping()`              | `func_intercept_parent_mapping/049-create.sql`            |
| `active_tag_mappings`         | `trg_insert_parents_from_active_mappings` | AFTER INSERT, ROW                                                 | `insert_parents_from_active_mappings()`   | `func_insert_parents_from_active_mappings/047-create.sql` |
| `active_tag_mappings`         | `trg_delete_parents_from_active_mappings` | AFTER DELETE, ROW                                                 | `delete_parents_from_active_mappings()`   | `func_delete_parents_from_active_mappings/048-create.sql` |
| `active_tag_mappings`         | `trg_repropagate_parents_on_ideal_change` | AFTER UPDATE, ROW (`WHEN` guarded on effective-tag change)        | `repropagate_parents_on_ideal_change()`   | `func_repropagate_parents_on_ideal_change/085-create.sql` |
| `active_tag_mappings`         | `accumulate_tag_count_trigger`            | AFTER INSERT OR UPDATE OR DELETE, ROW                             | `accumulate_tag_count_storage()`          | `func_accumulate_tag_count_storage/038-create.sql`        |
| `active_tag_mappings_parents` | `trg_atmp_internal_insert`                | AFTER INSERT, ROW                                                 | `atmp_internal_on_insert()`               | `func_atmp_internal_on_insert/039-create.sql`             |
| `active_tag_mappings_parents` | `trg_atmp_internal_delete`                | AFTER DELETE, ROW                                                 | `atmp_internal_on_delete()`               | `func_atmp_internal_on_delete/040-create.sql`             |
| `active_tag_mappings_parents` | `trg_accumulate_tag_count_parents`        | AFTER INSERT OR DELETE, ROW                                       | `accumulate_tag_count_parents()`          | `func_accumulate_tag_count_parents/086-create.sql`        |

The count helpers `add_count()` and `remove_count()` are not triggers; they are called by the two
`accumulate_*` functions above. Both were rewritten to drop a `LOCK TABLE tag_counts IN EXCLUSIVE
MODE` that serialised every writer process-wide for the duration of a transaction
(`func_add_count/094-rewrite.sql`, `func_remove_count/095-rewrite.sql`). The `ON CONFLICT` target is
already the primary key, so the lock bought nothing.

`tag_text(...)` overloads (`func_tag_text/008-create.sql`, `func_tag_text_by_id/051-create.sql`) and
`concat_tag(...)` (`func_concat_tag/007-create.sql`) are plain `IMMUTABLE` functions backing the
`GENERATED ALWAYS AS (tag_text(...)) STORED` column on `tags`. Not triggers, listed here only
because they are part of the tag pipeline.

### On migration history

There are no `DROP TRIGGER` statements anywhere in the tree. Earlier revisions of this document
described migration `105` dropping a superseded trigger set created by migration `91`, and listed a
"redefined in" column citing hotfix migrations `170`/`174`/`175`/`187`/`189`. Those flat migration
files no longer exist: the tree was reorganised into per-object directories and the hotfixes were
folded into each function's `-create.sql`. The behaviour they introduced survives in the current
bodies; the separate migrations recording it do not. Use `git log` on a function's directory for
that history.

## 4. Worked examples

Throughout, tag IDs are written as short names (`A`, `B`, `cat`, `animal`) for readability;
assume a single `tag_domain_id`.

### 4.1 Tagging a record (no aliases involved)

```
INSERT INTO tag_mappings (record_id=1, tag_id=cat, tag_domain_id=0)
```

1. `trg_tag_mappings_after_insert` (statement-level) fires once for the whole batch. It joins
   `new_rows` to `file_info` — **this join is an existence gate**: if `record_id=1` has no
   `file_info` row yet, the join produces zero rows and nothing is inserted into
   `active_tag_mappings` here.
2. Assuming `file_info` already exists: `LEFT JOIN tag_aliases ta ON ta.aliased_id = cat` finds
   nothing (no alias), so `ideal_tag_id = NULL` is inserted.
3. `INSERT INTO active_tag_mappings (record_id=1, tag_id=cat, ideal_tag_id=NULL, tag_domain_id=0)`.
   `trg_intercept_parent_mapping` (BEFORE INSERT) re-resolves `ideal_tag_id` independently from
   `tag_aliases` — redundant with step 2 here, but this is what actually makes the value
   authoritative regardless of what the STATEMENT-level insert computed (defense in depth: the
   row-level trigger is the source of truth).
4. `trg_insert_parents_from_active_mappings` (AFTER INSERT on `active_tag_mappings`) looks for
   `tag_parents` rows with `child_id = cat` — none exist yet, so nothing further happens.
5. `accumulate_tag_count_trigger` calls `add_count(cat, NULL, 0)` →
   `tag_counts(cat,0).storage_count += 1`, `tag_counts(cat,0).display_count += 1`.

**Result:** `active_tag_mappings = {(1, cat, NULL, 0)}`, `tag_counts(cat) = (storage=1, display=1)`.

### 4.2 Tagging before `file_info` exists (deferred activation)

```
INSERT INTO tag_mappings (record_id=2, tag_id=dog, tag_domain_id=0)   -- file_info(2) does NOT exist yet
```

1. `trg_tag_mappings_after_insert` fires; the `JOIN file_info` matches nothing for record 2.
   `active_tag_mappings` gets **no row**. The raw mapping exists in `tag_mappings`, but it is
   invisible to search/display until the file is actually stored.
2. Later: `INSERT INTO file_info (record_id=2, ...)`. `trg_tag_mappings_after_active_insert`
   (statement-level, on `file_info`) fires, joins `new_rows` back to `tag_mappings` for
   record 2, finds the pre-existing `dog` mapping, and inserts it into `active_tag_mappings` now
   (`ON CONFLICT DO NOTHING` makes this safe to run even if some tags were already active).

This two-trigger split (one gated on `tag_mappings` insert, one gated on `file_info` insert) is
what allows tags to be applied to a record whose file bytes/metadata haven't finished processing
yet, without ever needing the application layer to know or care about ordering.

### 4.3 Creating an alias with no existing content (`bad → good`)

```
INSERT INTO tag_aliases (aliased_id=bad, alias_id=good, tag_domain_id=0)
```

1. `tag_aliases_before_insert_trigger` (BEFORE INSERT) looks for an existing `tag_aliases` row
   with `aliased_id = good` (i.e. is `good` itself already aliased further?). None found →
   `new.ideal_alias_id = NULL`. The 187 cycle guard compares `ideal_alias_id` (`NULL`) to
   `aliased_id` (`bad`) — not equal, no cycle, proceeds. (Backstop: the table `CHECK
   (aliased_id != ideal_alias_id)` would also reject `NULL`-vs-`bad` trivially, since it's only
   ever equal when a real cycle resolves.)
2. Row inserted: `(bad, good, ideal_alias_id=NULL, effective_tag_id=good)`.
3. `tag_aliases_after_insert_trigger` (AFTER INSERT) propagates: updates any other
   `tag_aliases` rows whose `effective_tag_id = bad` (none yet), and any `tag_parents` rows
   where `parent_id = bad` or `child_id = bad` (none yet).
4. `trg_repair_tag_mappings_ideals_insert` (028): `UPDATE active_tag_mappings SET ideal_tag_id =
   new.effective_tag_id WHERE tag_id = bad AND tag_domain_id = ...`. This repairs any records
   **already** tagged with `bad` so they now display as `good`. That `UPDATE` is itself what
   `trg_repropagate_parents_on_ideal_change` (085, see §4.5) reacts to, so parent-tag propagation
   stays in sync at the same time.
5. `repair_parents_on_alias_insert` (098) runs last, once for the whole statement. If any row in
   `active_tag_mappings_parents` was materialised under the raw id `bad`, it is rewritten to
   `good`. This is a third cache of the tag id, distinct from the two above: 028 fixes
   `active_tag_mappings.ideal_tag_id` and 022 fixes `tag_parents.ideal_parent_id`/`ideal_child_id`,
   but neither touches a materialised parent row. The function short-circuits when no parent row
   names the aliased tag, so the common case pays only one `EXISTS`. When it does fire it suspends
   this table's own triggers via `set_config('session_replication_role', 'replica', true)`, because
   `atmp_internal_on_delete` and `atmp_internal_on_insert` both key off values the rewrite is
   deliberately changing, and neither applies: the same logical parent is being restored under its
   ideal id, not retracted and re-granted. `origin_id` is deliberately left as the raw tag.

### 4.4 Chained aliases, insertion order matters for intermediate state (not final state)

Goal: `A → B → C` (A aliases to B, B aliases to C).

**Order 1: insert `B→C` first, then `A→B`.**
- `B→C`: `ideal_alias_id(B row) = NULL` (nothing aliases `C` further). Row: `(B,C,NULL)`,
  `effective_tag_id=C`.
- `A→B`: before-insert trigger looks up the row where `aliased_id = B` — finds `(B,C,NULL)`,
  reads its `effective_tag_id = C`. So `new.ideal_alias_id = C` directly. Row:
  `(A,B,ideal_alias_id=C)`, `effective_tag_id=C`.
- Final state: both `A` and `B`'s alias rows resolve to `C` in one hop. No chain-walking needed
  at read time, ever.

**Order 2: insert `A→B` first, then `B→C`.**
- `A→B`: before-insert looks up `aliased_id = B` — nothing yet. Row: `(A,B,ideal_alias_id=NULL)`,
  `effective_tag_id=B`.
- `B→C`: before-insert looks up `aliased_id = C` — nothing. Row: `(B,C,NULL)`,
  `effective_tag_id=C`. Then `tag_aliases_after_insert_trigger` for the `B→C` row runs its
  propagation UPDATE: `UPDATE tag_aliases SET ideal_alias_id = C WHERE
  COALESCE(ideal_alias_id, alias_id) = B` — this matches `A`'s row (`alias_id=B`,
  `ideal_alias_id` currently `NULL`, so `COALESCE(...) = B`). `A`'s row becomes
  `(A,B,ideal_alias_id=C)`.
- Same final state as Order 1. `trg_repair_tag_mappings_ideals_insert` also fires for the `B→C`
  insert and repairs any `active_tag_mappings` rows with `tag_id=B` to `ideal_tag_id=C`, but note
  it does **not** look at rows with `tag_id=A`, because the repair trigger only matches
  `tag_id = new.aliased_id` (`B`), not the whole downstream chain. Records tagged with `A` get
  fixed by the *cascaded* `UPDATE ... tag_aliases` from the propagation step above. That is a real
  `UPDATE` on `A`'s row, so it fires `tag_aliases`' AFTER UPDATE triggers, and
  `trg_repair_tag_mappings_ideals_update` (029) is one of them; it matches `tag_id = old.aliased_id`
  (`A`) and rewrites the mapping. Note these two are **sibling** triggers on the same table and
  event, fired in name order by the same `UPDATE`; neither invokes the other. So both orders fully
  repair pre-existing content, just via different trigger paths (`_insert` repair vs. `_update`
  repair cascading from the chain propagation).

### 4.5 Aliasing a tag after content is already tagged repropagates parent tags

Setup: `parent(animal) -child- (dog)` exists in `tag_parents`. Record 3 is already tagged with
`shiba` (no alias yet). Now someone runs:

```
INSERT INTO tag_aliases (aliased_id=shiba, alias_id=dog, tag_domain_id=0)
```

- `trg_repair_tag_mappings_ideals_insert` (028) updates `active_tag_mappings`: record 3's row for
  `shiba` gets `ideal_tag_id = dog`. `active_tag_mappings_final` (the view) now shows record 3
  as tagged `dog` (via `COALESCE(ideal_tag_id, tag_id)`).
- That `UPDATE` on `active_tag_mappings` fires `trg_repropagate_parents_on_ideal_change` (085),
  since `COALESCE(ideal_tag_id, tag_id)` just changed from `shiba` to `dog`. Its insert branch
  looks up `tag_parents` for `COALESCE(ideal_child_id, child_id) = dog`, finds the `animal`
  relationship, and inserts `active_tag_mappings_parents(record=3, tag=animal, origin=dog)`.
  `trg_atmp_internal_insert` then cascades that further up `animal`'s own ancestors, if any, the
  same way it would for a fresh direct tagging.
- If `shiba` is later un-aliased (or re-aliased to something else), the delete branch of the
  same trigger removes the `animal` row again — unless another raw synonym on record 3 still
  independently resolves to `dog`, in which case it's left alone (see the multi-synonym note in
  `IDHANMigration/src/func_repropagate_parents_on_ideal_change/085-create.sql`).

### 4.6 Parent relationship created for an already-aliased tag

Setup: `shiba → dog` alias already exists (from §4.5, or independently). Record 3 is tagged
`shiba` (already resolved to `dog` in `active_tag_mappings.ideal_tag_id`). Now, either form
below produces the same result:

```
INSERT INTO tag_parents (parent_id=animal, child_id=dog, tag_domain_id=0)     -- ideal form
INSERT INTO tag_parents (parent_id=animal, child_id=shiba, tag_domain_id=0)   -- raw/synonym form
```

- `trg_check_parent_cycle` walks raw `tag_parents` ancestors of `animal` — fine, no cycle.
- `trg_tag_parents_before_insert` (083) resolves `ideal_child_id` from `tag_aliases` regardless
  of which form was used: for the raw form, it looks up `aliased_id = shiba`, finds `dog`, and
  sets `new.ideal_child_id = dog`. For the ideal form, the lookup on `aliased_id = dog` finds
  nothing (`dog` isn't itself aliased further), so `ideal_child_id` stays `NULL` — which is fine,
  since `COALESCE(ideal_child_id, child_id)` already evaluates to `dog` either way.
- `trg_insert_parent_mapping` (045) backfills existing content by matching effective tag to
  effective tag:
  ```sql
  FROM active_tag_mappings tm
  WHERE COALESCE(tm.ideal_tag_id, tm.tag_id) = COALESCE(new.ideal_child_id, new.child_id)
  ```
  Both sides evaluate to `dog` regardless of which form the relationship was created with, so
  record 3 gets `animal` backfilled correctly either way. Net-new tagging events afterward go
  through the equivalent COALESCE-on-both-sides matching in `insert_parents_from_active_mappings`
  (047), so a record freshly tagged with any synonym of `dog` — not just `dog` or `shiba`
  specifically — still picks up `animal`.

### 4.7 Multi-level parent chain, backfill and teardown

Setup: `bird -parent-of-> sparrow`, `animal -parent-of-> bird`. Record 4 already tagged `sparrow`
before either parent relationship existed.

```
INSERT INTO tag_parents (parent_id=bird, child_id=sparrow, tag_domain_id=0)
```
- `trg_insert_parent_mapping` step 1: finds record 4 tagged `sparrow`, inserts
  `active_tag_mappings_parents(record=4, tag=bird, origin=sparrow, internal_count=0)`.
- Step 2 (ancestor ripple): looks for existing `active_tag_mappings_parents` rows with
  `tag_id = sparrow` (there are none yet, since `sparrow` itself has no parents recorded as an
  *origin* in that table) — no-op here.
- `trg_atmp_internal_insert` (AFTER INSERT on `active_tag_mappings_parents`, fired for the row
  just inserted): looks for `tag_parents` where `child_id = bird` — none yet. No-op.

```
INSERT INTO tag_parents (parent_id=animal, child_id=bird, tag_domain_id=0)
```
- `trg_insert_parent_mapping` step 1: finds active mappings with effective tag `bird` — none
  (`bird` was never directly applied to a record) — no direct insert.
- Step 2 (ancestor ripple): finds `active_tag_mappings_parents` rows with `tag_id = bird`
  (record 4's row from the previous step) → inserts
  `(record=4, tag=animal, origin=bird, internal_count=1)`. The `origin_id` written here is
  `new.child_id` of the `tag_parents` row being inserted, which is `bird`, **not** the original
  `sparrow` that seeded the chain. Each ripple step re-homes `origin_id` to its own immediate
  child; that is what makes the teardown below terminate one level at a time.
- Result: record 4 now shows `sparrow` (storage), `bird` (parent, `internal_count=0`,
  origin=sparrow), `animal` (parent, `internal_count=1`, origin=bird).

Teardown — untag `sparrow` from record 4 (`DELETE FROM tag_mappings WHERE record_id=4 AND
tag_id=sparrow`):
1. Native FK `ON DELETE CASCADE` on `active_tag_mappings` removes `(4, sparrow, ...)` — this is
   **not** an explicit app-level `DELETE FROM active_tag_mappings`; it's the FK from
   `active_tag_mappings.record_id,tag_id,tag_domain_id → tag_mappings(...)` doing it implicitly.
2. That delete fires `trg_delete_parents_from_active_mappings`: removes
   `active_tag_mappings_parents` rows with `origin_id IN (sparrow, sparrow)` (raw = ideal here,
   no alias) `AND NOT internal` → deletes `(4, bird, sparrow, internal_count=0)` since it's
   non-internal.
3. That delete (of the `bird` row) fires `trg_atmp_internal_delete`: decrements
   `internal_count` on rows with `origin_id = old.tag_id` (`bird`), finding
   `(4, animal, origin=bird, internal_count=1)` → decrements to `0`, and since it now matches
   `internal_count = 0` it is deleted in the same function call.
4. `accumulate_tag_count_trigger` runs `remove_count` for the original `active_tag_mappings` row
   deleted in step 1, decrementing `sparrow`'s counts. Separately, `trg_accumulate_tag_count_parents`
   (086) ran `remove_count(NULL, ...)` for each `active_tag_mappings_parents` row deleted in
   steps 2 and 3, decrementing `bird` and `animal`'s `display_count`; see §4.10.

Final state: all four rows related to record 4's `sparrow` tagging are gone, cleanly, purely via
cascading row-level triggers — no application code needs to know about the parent hierarchy
depth.

### 4.8 Alias cycle rejection

```
INSERT INTO tag_aliases (aliased_id=A, alias_id=B, ...)   -- ok, A→B
INSERT INTO tag_aliases (aliased_id=B, alias_id=A, ...)   -- attempt B→A
```
`tag_aliases_before_insert_trigger` for the second insert resolves `ideal_alias_id` by looking
up `aliased_id = A` (the target, `A`) — finds the first row, `effective_tag_id = B`. Compares
`new.ideal_alias_id (B) = new.aliased_id (B)` → equal → `RAISE EXCEPTION 'Cycle detected:
aliasing B to A would create an alias cycle in domain 0'`. `createTagAliases` (API layer) greps
for the `Cycle detected` prefix and returns **409 Conflict** rather than a generic 500.

Longer chains (`A→B→C`, then attempting `C→A`) are also caught: `ideal_alias_id` for a `C→A`
insert resolves through the collapsed chain to `aliased_id=A`'s effective target, which is
itself `A`'s own... actually resolving `aliased_id = A` (the new alias's *target*) finds nothing
if `A` isn't aliased to anything (`A` is the root of the chain) — so a 3-node cycle `A→B→C→A`
*is* caught because inserting `C→A` looks up `aliased_id = A`: no row (A is not further
aliased), so `ideal_alias_id = NULL`, and `NULL != C` (`new.aliased_id`), so **no exception is
raised by the before-insert trigger** — the row `(C, A, ideal_alias_id=NULL)` is inserted. This
is fine and not actually a cycle *yet* in the alias-resolution sense (`A`'s `effective_tag_id`
is still `A` itself pre-insert). The unnamed `CHECK (aliased_id != ideal_alias_id)` also doesn't
fire, since `ideal_alias_id` is `NULL`. **What actually creates the cycle** is the direct
`aliased_id=A, alias_id=B` edge combined with `alias_id` chains eventually pointing back — for
`tag_aliases`, since each tag can only be `aliased_id` **once** (`UNIQUE (tag_domain_id,
aliased_id)`), a true cycle would require `A`'s row (`aliased_id=A`) to itself later be
`UPDATE`d to point back into the chain, which is where `tag_aliases_after_update_trigger`'s
explicit `RAISE EXCEPTION 'Recursive alias detected during update'` guard is the actual
backstop for that direction — not the before-insert trigger. In short: the insert-time cycle
guard only catches an immediate 2-cycle (`X→Y` then `Y→X`) or an N-cycle where the *last* edge
closes the loop back to a tag that already resolves through the chain; genuine 3+-cycles formed
by inserting edges in "the wrong order" are prevented by the `UNIQUE(tag_domain_id, aliased_id)`
constraint making it structurally impossible to give a tag two outgoing alias edges in the first
place — a tag can only be `aliased_id` once, so you cannot build a cycle by insertion order at
all; the only way back into a chain is via `UPDATE`, which is where the update-trigger's
explicit guard applies.

### 4.9 Parent cycle rejection

```
INSERT INTO tag_parents (parent_id=mammal, child_id=dog, ...)
INSERT INTO tag_parents (parent_id=canine, child_id=mammal, ...)
INSERT INTO tag_parents (parent_id=dog, child_id=canine, ...)   -- would close the loop
```
Unlike `tag_aliases`, `tag_parents` has no uniqueness constraint preventing multiple parents per
child (a tag can have many parents, and many children), so cycles are structurally possible and
must be caught procedurally. `trg_check_parent_cycle`'s recursive CTE walks *up* from
`new.parent_id` (`dog`) through existing `tag_parents` ancestors: `dog`'s ancestors via the
first two inserts are `mammal`, then `canine` (parent of `mammal`). The third insert's
`new.child_id = canine` is found among `dog`'s ancestor set → `RAISE EXCEPTION 'Cycle detected:
inserting parent dog for child canine would create a cycle in domain 0'`. This correctly catches
cycles of any depth (recursive CTE), unlike the alias cycle guard which relies on the unique
outgoing-edge constraint. Note this walk uses **raw** `parent_id`/`child_id` only — it does not
resolve through `tag_aliases`, so a cycle that only exists in "ideal" space (via aliased
parent/child ids) would not be caught here.

### 4.10 `tag_counts` accounting

`add_count(tag_id, ideal_tag_id, domain)`:
- If `tag_id IS NOT NULL`: `tag_counts(tag_id).storage_count += 1` (upsert from 0).
- Always: `tag_counts(COALESCE(ideal_tag_id, tag_id)).display_count += 1`.

So for an un-aliased tag, `storage_count` and `display_count` land on the same row and move
together. For an aliased tag (record tagged with `shiba`, resolved to `dog`):
`tag_counts(shiba).storage_count += 1` (nobody displays `shiba` directly, but "how many raw
mappings point at this exact tag id" is tracked), and `tag_counts(dog).display_count += 1`
(this is what a UI tag-count badge should show for `dog`). `tag_counts(dog).storage_count` is
**not** incremented by tagging with `shiba` — only literal `tag_id = dog` insertions bump
`dog`'s own storage count. This storage/display split is intentional and mirrors Hydrus's
sibling/alias count semantics.

`trg_accumulate_tag_count_parents` (086) applies the same `add_count`/`remove_count` helpers to
`active_tag_mappings_parents`, always with a `NULL` raw-tag argument (parent tags have no
"storage" concept — nothing is ever directly applied to get one): `add_count(NULL, tag_id,
domain)` only ever bumps `display_count`. So a tag that's only ever reached as an ancestor (never
applied directly or aliased-to) still gets a `tag_counts` row and a working display count. This
inherits the same per-row (not per-distinct-record) counting characteristic as the rest of
`add_count`/`remove_count`: if a record has two different raw tags that both resolve to the same
parent (e.g. two siblings both eventually leading to `animal` through different chains), that
parent's `display_count` is bumped once per contributing chain, not deduplicated to one per
record.

## 5. `tag_siblings` — present in the schema, write endpoints disabled

`tag_siblings` (`table_tag_siblings/064-create.sql`) is a plain table: `(older_id, younger_id,
tag_domain_id)`, no triggers, no `ideal_*`/`effective_*` columns, no `CHECK` beyond
`older_id != younger_id`. `getTagRelationships.cpp` reads it normally — `GET` requests listing a
tag's siblings work as expected.

`createTagSiblings`/`removeTagSiblings`
(`IDHANServer/src/api/tags/siblings/{createSiblings,removeSiblings}.cpp`) unconditionally return
`501 Not Implemented`, since sibling relationships have no effect on tag resolution, search, or
display in the current schema — there is no silencing of the "younger" tag when the "older" tag
is present, no alias-style collapsing, nothing feeding `active_tag_mappings`/
`active_tag_mappings_parents`. Existing rows in `tag_siblings` (if any) are inert and left as-is.

History (confirmed via `git log`): full sibling-resolution infrastructure was added in commit
`db6c1b44` ("Add sibling relationship handling, silencing logic, and associated tests") —
migrations `166`–`173` added `ideal_older_id`/`ideal_younger_id`/`effective_*` columns, a
`trg_tag_siblings_before_insert` alias-resolution trigger, an `aliased_siblings` view, a
`silenced` boolean on `active_tag_mappings`, and silencing propagation logic. All of that was
then **deleted** in commit `25da681d` ("...remove sibling-handling logic"), which is an ancestor
of the current `HEAD`. Migrations `166`–`173` no longer exist on disk, but are recoverable via
`git show 25da681d~1:IDHANMigration/src/166-tag_siblings_triggers.sql` and siblings, if sibling
resolution is rebuilt in the future. Those numbers refer to the pre-reorganisation flat layout,
which is what that commit's tree still has.

## 6. Migration directories covered

All under `IDHANMigration/src/`:

| Directory                                   | Files                               |
|---------------------------------------------|-------------------------------------|
| `func_concat_tag/`                          | `007-create.sql`                    |
| `func_tag_text/`                            | `008-create.sql`                    |
| `func_tag_text_by_id/`                      | `051-create.sql`                    |
| `func_tag_aliases_after_insert_trigger/`    | `022-create.sql`                    |
| `func_tag_aliases_before_insert_trigger/`   | `023-create.sql`                    |
| `func_tag_aliases_after_delete_trigger/`    | `024-create.sql`                    |
| `func_tag_aliases_after_update_trigger/`    | `025-create.sql`                    |
| `func_tag_mappings_after_insert/`           | `026-create.sql`                    |
| `func_tag_mappings_after_active_insert/`    | `027-create.sql`                    |
| `func_repair_tag_mappings_ideals_insert/`   | `028-create.sql`                    |
| `func_repair_tag_mappings_ideals_update/`   | `029-create.sql`                    |
| `func_repair_tag_mappings_ideals_delete/`   | `030-create.sql`                    |
| `func_createtagdomainpartitions/`           | `031-create.sql`                    |
| `func_add_count/`                           | `036-create.sql`, `094-rewrite.sql` |
| `func_remove_count/`                        | `037-create.sql`, `095-rewrite.sql` |
| `func_accumulate_tag_count_storage/`        | `038-create.sql`                    |
| `func_atmp_internal_on_insert/`             | `039-create.sql`                    |
| `func_atmp_internal_on_delete/`             | `040-create.sql`                    |
| `func_check_parent_cycle/`                  | `044-create.sql`                    |
| `func_insert_parent_mapping/`               | `045-create.sql`                    |
| `func_delete_parent_mapping/`               | `046-create.sql`                    |
| `func_insert_parents_from_active_mappings/` | `047-create.sql`                    |
| `func_delete_parents_from_active_mappings/` | `048-create.sql`                    |
| `func_intercept_parent_mapping/`            | `049-create.sql`                    |
| `func_tag_parents_before_insert_trigger/`   | `083-create.sql`                    |
| `func_repropagate_parents_on_ideal_change/` | `085-create.sql`                    |
| `func_accumulate_tag_count_parents/`        | `086-create.sql`                    |
| `func_repair_parents_on_alias/`             | `098-create.sql`                    |
| `table_tag_mappings/`                       | `018-create.sql`                    |
| `table_active_tag_mappings/`                | `019-create.sql`, `042-index.sql`   |
| `table_tag_aliases/`                        | `020-create.sql`                    |
| `table_tag_parents/`                        | `021-create.sql`                    |
| `table_active_tag_mappings_parents/`        | `033-create.sql`, `043-index.sql`   |
| `table_tag_siblings/`                       | `064-create.sql`                    |
| `view_active_tag_mappings_final/`           | `034-create.sql`, `099-rewrite.sql` |

Plus the deleted-but-git-recoverable flat migrations `166`–`173` referenced in §5.
