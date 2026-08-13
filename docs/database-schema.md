# Database Schema {#database_schema}

This document describes IDHAN's PostgreSQL schema: every table, its columns, constraints,
relationships, and the indexes that support them. It reflects the **current effective schema** —
that is, the state produced after *all* migrations in `IDHANMigration/src/` have run in order, not
any single migration file. Where a later migration alters or supersedes an earlier definition, the
column descriptions below describe the final result and note the migration that changed it.

For the deep behavioural walk-through of the tag-resolution triggers (raw → active → parents →
counts), see the companion document [Tag System Migrations & Trigger Review](tag-system-triggers.md).
This document is the structural reference; that one is the behavioural reference.

## Conventions

- **Migrations** live in `IDHANMigration/src/` as `N-name.sql` and execute in ascending numeric
  order at server startup. The `idhan_info` table records how far each table has been migrated.
- **ID types.** Every surrogate key is a distinct C++ type alias over an integer (`RecordID`,
  `TagID`, `TagDomainID`, `ClusterID`, …; see `IDHAN/include/IDHANTypes.hpp`). In SQL they are
  `SERIAL`/`SMALLSERIAL` (`int4`/`int2`). Column-width choices matter: `tag_domain_id` is
  `SMALLINT` everywhere, `record_id`/`tag_id` are `INTEGER`.
- **`bytea` hashes.** `SHA256` values are stored as raw 32-byte `bytea` with a
  `CHECK (length(...) = 32)`, never as hex text.
- **Ideal vs. raw ids.** Throughout the tag system, `*_id` is the tag as literally applied and
  `ideal_*_id` is its alias-resolved target. The *effective* value is `COALESCE(ideal_id, id)` and
  several tables expose it as a generated column named `effective_tag_id`.
- **Partitioning.** `tag_mappings`, `tag_aliases`, and `tag_parents` are `PARTITION BY LIST
  (tag_domain_id)`. Creating a row in `tag_domains` fires a trigger that creates the matching
  `*_domain_<id>` partition for each (see [Tag domains](#tag-domains)).


---

## Records and files

### `records`

The core object. Everything else keys off `record_id`. A record is a content identity (a SHA-256)
and may exist without any associated file.

| Column          | Type                                 | Notes                                                                                                                                                                                                     |
|-----------------|--------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `record_id`     | `SERIAL` PK                          | `RecordID`                                                                                                                                                                                                |
| `sha256`        | `bytea` UNIQUE NOT NULL              | `CHECK (length(sha256) = 32)`                                                                                                                                                                             |
| `creation_time` | `TIMESTAMP` NOT NULL `DEFAULT now()` | Added in migration 194. Distinct from `file_info.cluster_store_time`. Pre-194 rows were backfilled with the migration's run time, so `RECORD_TIME` sort is only meaningful for records created after 194. |

Indexes: `records_pkey` (record_id), UNIQUE (sha256) — also serves the `HASH` sort —, and
`(creation_time, record_id)` (migration 195).

### `file_info`

One-to-one with `records`, present only once a physical file is associated. `record_id` is the
primary key (the earlier redundant `UNIQUE`/manual index were dropped in migrations 101 and 172).

| Column                | Type                                 | Notes                                       |
|-----------------------|--------------------------------------|---------------------------------------------|
| `record_id`           | `INTEGER` PK → `records`             | one file_info per record                    |
| `size`                | `BIGINT` NOT NULL                    | `CHECK (size >= 0)` (migration 185)         |
| `mime_id`             | `INTEGER` → `mime`                   | nullable                                    |
| `extension`           | `TEXT`                               | nullable                                    |
| `cluster_id`          | `SMALLINT` → `file_clusters`         | NULL when the file is not currently stored  |
| `cluster_store_time`  | `TIMESTAMP` NOT NULL `DEFAULT now()` | made NOT NULL/defaulted in migration 120    |
| `cluster_delete_time` | `TIMESTAMP`                          | set when a file is removed from its cluster |
| `modified_time`       | `TIMESTAMP`                          | added in migration 120; nullable            |

Constraints:

- `file_infocheck_mime_or_extension` — `NOT (mime_id IS NULL AND extension IS NULL)` (145): a file
  must be identifiable by at least one of mime or extension.
- `cluster_id_xor_delete_time` — exactly one of `cluster_id` / `cluster_delete_time` is set (155):
  a file is either currently stored *or* recorded as deleted, never both/neither.

Indexes (migration 195, supporting sort types): `(size, record_id)`,
`(cluster_store_time, record_id)`, `(mime_id, record_id)`, and a partial
`(modified_time, record_id) WHERE modified_time IS NOT NULL`.

### `file_clusters`

A cluster maps a `ClusterID` to an on-disk directory. Managed exclusively by
`filesystem::ClusterManager`. See CLAUDE.md § File storage.

| Column               | Type                             | Notes                                   |
|----------------------|----------------------------------|-----------------------------------------|
| `cluster_id`         | `SMALLSERIAL` PK                 | `ClusterID`                             |
| `ratio_number`       | `SMALLINT` NOT NULL DEFAULT 1    | fill ratio weight (ratio / total ratio) |
| `size_used`          | `BIGINT` NOT NULL DEFAULT 0      | bytes currently used                    |
| `size_limit`         | `BIGINT` NOT NULL DEFAULT 0      | byte cap                                |
| `file_count`         | `INTEGER` NOT NULL DEFAULT 0     | files stored here                       |
| `read_only`          | `BOOLEAN` NOT NULL DEFAULT TRUE  | read-only clusters accept no new files  |
| `allowed_thumbnails` | `BOOLEAN` NOT NULL DEFAULT FALSE | may hold thumbnails                     |
| `allowed_files`      | `BOOLEAN` NOT NULL DEFAULT TRUE  | may hold source files                   |
| `cluster_name`       | `TEXT` UNIQUE                    | falls back to `folder_path` when NULL   |
| `folder_path`        | `TEXT` NOT NULL UNIQUE           | on-disk directory                       |

### `mime`

Lookup of known MIME types and their preferred extension. Seeded on creation (migration 40) with
common image/video/archive types and later `unknown/unknown → ''` (migration 186, matching Hydrus's
representation of unknown-filetype files).

| Column           | Type                   | Notes            |
|------------------|------------------------|------------------|
| `mime_id`        | `SERIAL` PK            |                  |
| `name`           | `TEXT` UNIQUE NOT NULL | e.g. `image/png` |
| `best_extension` | `TEXT` NOT NULL        | e.g. `png`       |

### Metadata tables

Each is one-to-one with `records`, keyed by `record_id` (promoted from UNIQUE to explicit PRIMARY
KEY in migration 173). Which table a record populates depends on its media type.

**`metadata`** — generic per-record metadata blob.

| Column             | Type                     | Notes                   |
|--------------------|--------------------------|-------------------------|
| `record_id`        | `INTEGER` PK → `records` |                         |
| `simple_mime_type` | `SMALLINT` NOT NULL      | coarse category         |
| `json`             | `json`                   | free-form; DEFAULT NULL |

**`image_metadata`**

| Column            | Type                     | Notes |
|-------------------|--------------------------|-------|
| `record_id`       | `INTEGER` PK → `records` |       |
| `width`, `height` | `INTEGER` NOT NULL       |       |
| `channels`        | `SMALLINT` NOT NULL      |       |

**`video_metadata`**

| Column            | Type                     | Notes   |
|-------------------|--------------------------|---------|
| `record_id`       | `INTEGER` PK → `records` |         |
| `duration`        | `FLOAT` NOT NULL         | seconds |
| `framerate`       | `FLOAT` NOT NULL         |         |
| `width`, `height` | `INTEGER` NOT NULL       |         |
| `bitrate`         | `INTEGER` NOT NULL       |         |
| `has_audio`       | `BOOLEAN` NOT NULL       |         |

Indexes (migration 195): `(duration, record_id)`, `(framerate, record_id)`,
`(has_audio, record_id)`.

**`image_project_metadata`** — layered image projects (e.g. PSD).

| Column            | Type                     | Notes |
|-------------------|--------------------------|-------|
| `record_id`       | `INTEGER` PK → `records` |       |
| `width`, `height` | `INTEGER` NOT NULL       |       |
| `channels`        | `SMALLINT` NOT NULL      |       |
| `layers`          | `SMALLINT` NOT NULL      |       |

> The `WIDTH`/`HEIGHT`/`RATIO`/`NUM_PIXELS` sort types `COALESCE` dimensions across
> `image_metadata`, `video_metadata`, and `image_project_metadata`; each table's `record_id`
> primary key covers the joins, but the cross-table order still needs an explicit sort
> (migration 195).

---

## Relationships between records

### Archives

An archive record (e.g. a zip) groups member records.

- **`archives`** — `archive_id SERIAL PK`.
- **`archive_map`** — `(archive_id → archives, record_id → records)`, `UNIQUE (archive_id,
  record_id)`: members of an archive.
- **`archive_metadata`** — one-to-one with `records` (PK `record_id`, migration 173): the archive
  record's own info. `archive_id → archives`, `password_bytes bytea` (nullable), `encrypted BOOLEAN
  DEFAULT FALSE`.

### Alternatives and duplicates

- **`alternative_groups`** — `group_id SERIAL PK`.
- **`alternative_group_members`** — `(group_id → alternative_groups, record_id → records)`.
  `record_id` is globally UNIQUE (a record belongs to at most one alternatives group);
  `UNIQUE (group_id, record_id)`; index on `group_id`.
- **`duplicate_pairs`** — directed "worse → better" duplicate relationships.
  `worse_record_id` is UNIQUE (a record is the worse side of at most one pair);
  `CHECK (worse != better)`; index on `better_record_id`. The `insert_duplicate_pair(worse,
  better)` function (migration 130) enforces no re-insertion, no immediate cycle, and re-points any
  chain that previously ended at `worse` to `better`.

---

## Tag core

### `tag_namespaces` / `tag_subtags`

A tag's two components are interned separately.

- **`tag_namespaces`** — `namespace_id SERIAL PK`, `namespace_text TEXT UNIQUE NOT NULL`.
- **`tag_subtags`** — `subtag_id SERIAL PK`, `subtag_text TEXT UNIQUE NOT NULL`.

### `tags`

A tag is a `(namespace_id, subtag_id)` pair. The display string is a **stored generated column**.

| Column         | Type                                                                  | Notes                              |
|----------------|-----------------------------------------------------------------------|------------------------------------|
| `tag_id`       | `SERIAL` PK                                                           | `TagID`                            |
| `namespace_id` | `INTEGER` → `tag_namespaces`                                          |                                    |
| `subtag_id`    | `INTEGER` → `tag_subtags`                                             |                                    |
| `tag_text`     | `TEXT` GENERATED ALWAYS AS `tag_text(namespace_id, subtag_id)` STORED |                                    |
|                |                                                                       | UNIQUE `(namespace_id, subtag_id)` |

`tag_text` is built by the `concat_tag`/`tag_text` SQL helper functions (`namespace:subtag`, or just
`subtag` when the namespace is empty). Migration 196 rewrote these as `LANGUAGE sql IMMUTABLE` so the
planner can inline them (they run once per inserted tag via the stored column). A GIN trigram index
`tags(tag_text gin_trgm_ops)` (migration 115) backs autocomplete / `ILIKE` search.

### Tag domains {#tag-domains}

A tag domain is an independent namespace for mappings and relationships (analogous to a Hydrus tag
service).

**`tag_domains`** — `tag_domain_id SMALLSERIAL PK`, `domain_name TEXT UNIQUE NOT NULL`. A `default`
domain is inserted on creation (migration 80).

Inserting a domain fires `createtagdomainpartitions()` (BEFORE INSERT, migration 80), which creates
the list partitions `tag_mappings_domain_<id>`, `tag_aliases_domain_<id>`, and
`tag_parents_domain_<id>` for that domain. `ON DELETE CASCADE` on every `tag_domain_id` foreign key
means dropping a domain removes all of its mappings, aliases, parents, siblings, and counts.

---

## Tag mappings and resolution

This is the heart of the schema. Raw mappings are what the user applied; a chain of triggers derives
alias-resolved ("active") mappings, then parent-implied mappings, then counts. **The trigger
mechanics are documented in full in [tag-system-triggers.md](tag-system-triggers.md); this section
describes the tables only.**

### `tag_mappings` (raw)

What was directly applied to a record. `PARTITION BY LIST (tag_domain_id)`.

| Column          | Type                                                  | Notes                                   |
|-----------------|-------------------------------------------------------|-----------------------------------------|
| `record_id`     | `INTEGER` → `records` NOT NULL                        |                                         |
| `tag_id`        | `INTEGER` → `tags` NOT NULL                           |                                         |
| `tag_domain_id` | `SMALLINT` → `tag_domains` ON DELETE CASCADE NOT NULL | partition key                           |
|                 |                                                       | PK `(record_id, tag_id, tag_domain_id)` |

An AFTER INSERT statement-level trigger populates `active_tag_mappings`; a matching trigger on
`file_info` backfills active mappings when a file is imported for an already-tagged record.

### `active_tag_mappings` (alias-resolved)

Each raw mapping resolved to its ideal (alias target).

| Column             | Type                                                                   | Notes                                                                                                               |
|--------------------|------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------|
| `record_id`        | `INTEGER` → `records` NOT NULL                                         |                                                                                                                     |
| `tag_id`           | `INTEGER` → `tags` NOT NULL                                            | as applied                                                                                                          |
| `ideal_tag_id`     | `INTEGER` → `tags`                                                     | alias target, or NULL if not aliased                                                                                |
| `tag_domain_id`    | `SMALLINT` → `tag_domains` ON DELETE CASCADE NOT NULL                  |                                                                                                                     |
| `effective_tag_id` | `INTEGER` GENERATED ALWAYS AS `COALESCE(ideal_tag_id, tag_id)` VIRTUAL |                                                                                                                     |
|                    |                                                                        | PK `(record_id, tag_id, tag_domain_id)`; FK `(record_id, tag_id, tag_domain_id)` → `tag_mappings` ON DELETE CASCADE |

Indexes: `active_tag_mappings_coalesce_tag_id` on `(COALESCE(ideal_tag_id, tag_id), tag_domain_id)
INCLUDE (record_id)`, plus two partial indexes on `tag_id`/`ideal_tag_id` split by whether
`ideal_tag_id IS NULL` (migration 100).

### `active_tag_mappings_parents`

Tags implied by parent relationships. `internal_count` tracks how many parent chains contribute a
given implied tag so it can be reference-counted on delete.

| Column           | Type                                                         | Notes                                              |
|------------------|--------------------------------------------------------------|----------------------------------------------------|
| `record_id`      | `INTEGER` → `records` NOT NULL                               |                                                    |
| `tag_id`         | `INTEGER` → `tags` NOT NULL                                  | the implied (parent) tag, alias-resolved           |
| `origin_id`      | `INTEGER` → `tags` NOT NULL                                  | the child tag that implied it                      |
| `internal_count` | `INTEGER` NOT NULL DEFAULT 0                                 | number of parents contributing this row            |
| `tag_domain_id`  | `SMALLINT` → `tag_domains` ON DELETE CASCADE NOT NULL        |                                                    |
| `internal`       | `BOOLEAN` GENERATED ALWAYS AS `(internal_count > 0)` VIRTUAL | true ⇒ only indirectly implied                     |
|                  |                                                              | PK `(record_id, tag_id, origin_id, tag_domain_id)` |

Index on `(tag_id) INCLUDE (record_id)` (migration 100).

### `tag_aliases`

`aliased_id → alias_id` means "when `aliased_id` is applied, treat it as `alias_id`."
`ideal_alias_id` collapses chains to their endpoint. `PARTITION BY LIST (tag_domain_id)`.

| Column             | Type                                                                       | Notes                                                                                                                                                                                               |
|--------------------|----------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `aliased_id`       | `INTEGER` → `tags` NOT NULL                                                | the tag being aliased away                                                                                                                                                                          |
| `alias_id`         | `INTEGER` → `tags` NOT NULL                                                | its immediate target                                                                                                                                                                                |
| `ideal_alias_id`   | `INTEGER` → `tags`                                                         | chain endpoint, or NULL                                                                                                                                                                             |
| `tag_domain_id`    | `SMALLINT` → `tag_domains` ON DELETE CASCADE NOT NULL                      |                                                                                                                                                                                                     |
| `effective_tag_id` | `INTEGER` GENERATED ALWAYS AS `COALESCE(ideal_alias_id, alias_id)` VIRTUAL |                                                                                                                                                                                                     |
|                    |                                                                            | PK `(aliased_id, alias_id, tag_domain_id)`; UNIQUE `(tag_domain_id, aliased_id)` (a tag aliases to one target per domain); `CHECK (aliased_id != alias_id)`, `CHECK (aliased_id != ideal_alias_id)` |

Cycle rejection: the BEFORE INSERT trigger (migration 187) raises `Cycle detected: …` — matched by
the API to return 409 — before the backstop `CHECK` fires.

> In Hydrus terms, IDHAN **aliases** correspond to Hydrus **siblings**. IDHAN's `tag_siblings` is a
> separate concept with no Hydrus equivalent.

### `tag_parents`

`child_id` implies `parent_id`. Both sides carry an alias-resolved ideal (populated by the BEFORE
INSERT trigger in migration 189). `PARTITION BY LIST (tag_domain_id)`.

| Column            | Type                                                  | Notes                                     |
|-------------------|-------------------------------------------------------|-------------------------------------------|
| `parent_id`       | `INTEGER` → `tags` NOT NULL                           | implied tag                               |
| `ideal_parent_id` | `INTEGER` → `tags`                                    | alias-resolved parent                     |
| `child_id`        | `INTEGER` → `tags` NOT NULL                           | triggering tag                            |
| `ideal_child_id`  | `INTEGER` → `tags`                                    | alias-resolved child                      |
| `tag_domain_id`   | `SMALLINT` → `tag_domains` ON DELETE CASCADE NOT NULL |                                           |
|                   |                                                       | PK `(parent_id, child_id, tag_domain_id)` |

Cycle rejection: `check_parent_cycle()` (BEFORE INSERT, migration 105) walks ancestors recursively
and raises `Cycle detected: …`.

### `tag_siblings`

An ordered pairing with no alias/parent semantics (surfaced read-only via the API).

| Column          | Type                                                  | Notes                                                                        |
|-----------------|-------------------------------------------------------|------------------------------------------------------------------------------|
| `older_id`      | `INTEGER` → `tags` NOT NULL                           |                                                                              |
| `younger_id`    | `INTEGER` → `tags` NOT NULL                           |                                                                              |
| `tag_domain_id` | `SMALLINT` → `tag_domains` ON DELETE CASCADE NOT NULL |                                                                              |
|                 |                                                       | PK `(older_id, younger_id, tag_domain_id)`; `CHECK (older_id != younger_id)` |

### `tag_counts`

Per-`(tag, domain)` cached counts, maintained incrementally by triggers on `active_tag_mappings`
and `active_tag_mappings_parents` (`add_count`/`remove_count`, migrations 93 & 191).

| Column          | Type                                         | Notes                                                      |
|-----------------|----------------------------------------------|------------------------------------------------------------|
| `tag_id`        | `INTEGER` → `tags`                           |                                                            |
| `storage_count` | `INTEGER` NOT NULL DEFAULT 0                 | records with this tag as applied                           |
| `display_count` | `INTEGER` NOT NULL DEFAULT 0                 | records with this tag as effective (incl. implied parents) |
| `tag_domain_id` | `SMALLINT` → `tag_domains` ON DELETE CASCADE |                                                            |
|                 |                                              | PK `(tag_id, tag_domain_id)`                               |

> A global `total_tag_counts` table existed briefly and was dropped in migration 110; the
> `update_tag_counts()` function that referenced it was dropped in 188. Counts are per-domain only.

### View: `active_tag_mappings_final`

The single source of truth for "what tags does this record effectively have," fully alias-resolved,
combining direct active mappings and parent-implied mappings (migration 174).

```sql
SELECT record_id, COALESCE(ideal_tag_id, tag_id) AS tag_id, tag_domain_id
FROM active_tag_mappings
UNION ALL
SELECT atmp.record_id,
       COALESCE(ta.effective_tag_id, atmp.tag_id) AS tag_id,
       atmp.tag_domain_id
FROM active_tag_mappings_parents atmp
     LEFT JOIN tag_aliases ta
       ON ta.aliased_id = atmp.tag_id AND ta.tag_domain_id = atmp.tag_domain_id;
```

`SearchBuilder` binds `$1` to an array of `tag_domain_ids` and queries against these active tables.

---

## URLs

- **`url_domains`** — `url_domain_id SERIAL PK`, `url_domain TEXT UNIQUE NOT NULL`.
- **`urls`** — `url_id SERIAL PK`, `url TEXT UNIQUE NOT NULL`, `url_domain_id → url_domains NOT
  NULL`.
- **`url_mappings`** — `(record_id → records, url_id → urls)`, PK `(record_id, url_id)`. Both columns
  made NOT NULL in migration 171.

---

## Notes

Free-text notes attachable to records, with an optional label taxonomy.

- **`notes`** — `note_id SERIAL PK`, `note TEXT UNIQUE NOT NULL`. GIN trigram index on `note`.
- **`note_labels`** — `label_id SERIAL PK`, `label TEXT UNIQUE`. GIN trigram index on `label`.
- **`label_mappings`** — `(note_id → notes, label_id → note_labels)`, `UNIQUE (note_id, label_id)`.
  FK columns individually indexed (migration 184).
- **`record_notes`** — `(record_id → records, note_id → notes)`, `UNIQUE (record_id, note_id)`. FK
  columns individually indexed (migration 184).

---

## Authentication

- **`auth_keys`** — permanent API keys, presented directly on every authenticated request. `key_id
  SERIAL PK`, `key_hash BYTEA UNIQUE NOT NULL CHECK (length = 32)`.

---

## WebUI

- **`webui_layouts`** — server-side copies of named WebUI layouts (migration 193). There is no user
  system: a layout is identified solely by its client-generated `layout_id UUID PRIMARY KEY`
  (localStorage is the source of truth, so a push is an upsert to a known id). `name TEXT NOT NULL`,
  `document JSONB NOT NULL` (the whole `LayoutDocument` envelope), `schema_ver INT NOT NULL`,
  `created_at`/`updated_at TIMESTAMPTZ DEFAULT now()`. A case-insensitive UNIQUE index on
  `lower(name)` makes a colliding push return a clean 409.

---

## System / bookkeeping

- **`idhan_info`** — migration bookkeeping (migration 00). `table_name TEXT PK`, `last_migration_id
  INTEGER NOT NULL`, `queries TEXT[] NOT NULL`. Tracks how far each table has been migrated so
  startup only applies the migrations it hasn't yet.
- **Extensions** — `pgcrypto` (migration 02) and `pg_trgm` (migration 03) are enabled; `pg_trgm`
  backs every GIN trigram index above.

---

## Stored functions (index)

Behavioural detail for the tag-system triggers is in
[tag-system-triggers.md](tag-system-triggers.md). The non-trigger helper functions are:

| Function                                                 | Purpose                                                                      | Migration       |
|----------------------------------------------------------|------------------------------------------------------------------------------|-----------------|
| `concat_tag(ns, subtag)`                                 | join components into display text                                            | 20, inlined 196 |
| `tag_text(namespace_id, subtag_id)` / `tag_text(tag_id)` | resolve a tag's display text                                                 | 20, inlined 196 |
| `createbatchtags(namespaces[], subtags[])`               | race-safe bulk create of namespaces/subtags/tags, returns ids in input order | 85, 197, 198    |
| `insert_duplicate_pair(worse, better)`                   | insert a duplicate relationship with cycle/re-insertion guards               | 130             |
| `add_count` / `remove_count`                             | incremental `tag_counts` maintenance (used by triggers)                      | 93              |

> `createbatchtags` dropped its table-level `LOCK` guards in migration 197 in favour of
> `INSERT … ON CONFLICT DO NOTHING`, which is race-safe under READ COMMITTED via each table's UNIQUE
> constraint, so concurrent batches now run in parallel. Migration 198 added `#variable_conflict
> use_column` to disambiguate the `ON CONFLICT` targets against the function's OUT columns.

## Partitioning summary

Three tables are list-partitioned by `tag_domain_id`, one partition per domain, created
automatically by the `tag_domains` insert trigger:

| Parent table   | Partition naming           |
|----------------|----------------------------|
| `tag_mappings` | `tag_mappings_domain_<id>` |
| `tag_aliases`  | `tag_aliases_domain_<id>`  |
| `tag_parents`  | `tag_parents_domain_<id>`  |

When adding a migration that touches these, remember that DDL affecting the parent propagates to
partitions, and that a fresh domain needs its partitions to exist before rows can be inserted.
