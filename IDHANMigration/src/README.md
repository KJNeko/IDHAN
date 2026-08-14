# Migrations

Each database object lives in its own directory, and every migration file changes **exactly one
object**. This makes an object's whole history visible at a glance.

## Layout

Directories are named `<type>_<object>`:

| Prefix          | Holds                                                                                                                                                             |
|-----------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `table_<name>/` | A table and everything that modifies it: `CREATE TABLE`, `ALTER TABLE`, its indexes, data seeds, `DROP TABLE`.                                                    |
| `view_<name>/`  | A view and its rebuilds.                                                                                                                                          |
| `func_<name>/`  | One function and all its `CREATE OR REPLACE` revisions. A trigger function's `CREATE TRIGGER` binding lives here too (a trigger exists only to run its function). |
| `misc_<name>/`  | Things that are not a domain object `misc_extensions/` (pgcrypto, pg_trgm), `misc_idhan_info/` (migration bookkeeping).                                           |

Inside a directory, each file is one change: `NNN-<description>.sql`, e.g. `013-create.sql`,
`051-alter.sql`, `090-index.sql`, `070-rewrite.sql`.

## Ordering

`NNN` is a **global, zero-padded** migration number. Files run in ascending numeric order across
**all** directories. Ordering is computed from the numeric prefix of the filename (see
`../cmake_modules/GenerateMigrations.cmake`), Two files sharing a number is invalid. Non-Concurrent numbers are allowed.

## Adding a migration

1. Find the current highest number across every directory.
2. Create your file as `<max+1>-<description>.sql` inside the target object's directory (creating
   the directory if the object is new).
3. Keep it to **one object**. If a change touches two tables, it is two migrations.

## Rules

- One migration per file. Do not do two 'CREATE X' in a single file.
- Triggers should be setup in the file they are being triggered against (put it in the table's folder)
