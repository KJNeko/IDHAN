# Migrations

Each database object lives in its own directory, and every migration file changes **exactly one
object**. This makes an object's whole history visible at a glance — `ls table_file_info/` shows
every change ever made to `file_info`, in order.

## Layout

Directories are named `<type>_<object>`:

| Prefix          | Holds                                                                                                                                                             |
|-----------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `table_<name>/` | A table and everything that modifies it: `CREATE TABLE`, `ALTER TABLE`, its indexes, data seeds, `DROP TABLE`.                                                    |
| `view_<name>/`  | A view and its rebuilds.                                                                                                                                          |
| `func_<name>/`  | One function and all its `CREATE OR REPLACE` revisions. A trigger function's `CREATE TRIGGER` binding lives here too (a trigger exists only to run its function). |
| `misc_<name>/`  | Things that are not a domain object — `misc_extensions/` (pgcrypto, pg_trgm), `misc_idhan_info/` (migration bookkeeping).                                         |

Inside a directory, each file is one change: `NNN-<description>.sql`, e.g. `013-create.sql`,
`051-alter.sql`, `090-index.sql`, `070-rewrite.sql`.

## Ordering

`NNN` is a **global, zero-padded** migration number. Files run in ascending numeric order across
**all** directories — the directory a file lives in never affects when it runs, only the number
does. Ordering is computed from the numeric prefix of the filename (see
`../cmake_modules/GenerateMigrations.cmake`), so two files must never share a number. Numbers need
not be contiguous: collapsing or removing a migration leaves a gap, and that is fine — the
generator only rejects duplicates, and the applied-state chain steps over gaps harmlessly.

## Adding a migration

1. Find the current highest number across every directory.
2. Create your file as `<max+1>-<description>.sql` inside the target object's directory (creating
   the directory if the object is new).
3. Keep it to **one object**. If a change touches two tables, it is two migrations.

## Rules

- **One migration, one object.** Never `CREATE`/`ALTER` two tables in a single file.
- **Triggers travel with their function**, not the table they fire on.
- **No create-then-drop.** These migrations assume a *fresh* setup: never add an object (or
  column, index, constraint) that a later migration removes — bake the final form into the
  original `CREATE`. Likewise, do not add one-time data-repair/backfill blocks; on a fresh
  database they are no-ops.
- **No `CREATE OR REPLACE` churn.** A function or view should appear once, in its final form,
  in its own `NNN-create.sql`. Don't keep intermediate `-rewrite`/`-fix` revisions: on a fresh
  setup only the last body survives, so bake it into the create.
- **Order objects by their creation-time dependencies.** An object's slot must come after
  everything its `CREATE` statement is validated against: FK targets, a view's referenced
  tables/columns, a generated column's function, and — for `LANGUAGE sql` functions — every
  table/function the body references (plpgsql defers body resolution to first call, so plpgsql
  bodies may forward-reference; sql bodies may not). When one function has overloads with
  conflicting constraints, split them across slots: e.g. `tag_text`'s two-arg overload precedes
  `tags` (its generated column needs it) while the one-arg overload reads `tags` and so lives in
  `func_tag_text_by_id/` after it.
- Numbers are permanent once released. To change an object, add a new higher-numbered file in its
  directory — do not edit or renumber existing ones.
