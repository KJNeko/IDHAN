# Database initialization

New installations use the `idhan` PostgreSQL schema for application objects.
Startup creates that schema before applying migrations. The application schema
remains configurable through `database.schema`, `IDHAN_DATABASE_SCHEMA`, or
`--pg_schema`; the default is `idhan`. IDHAN treats the database as shared: other
applications may have their own schemas and tables, which startup leaves alone.

Migration and runtime connections use the same search path: the configured
application schema first, followed by `public` for extension types, functions,
and operators. Schema names are quoted as individual identifiers. If the
application schema is `public`, it appears only once.

## Existing installations

Existing objects are not moved. Installations using `public` should retain it
explicitly:

```toml
[database]
schema = "public"
```

Alternatively, set `IDHAN_DATABASE_SCHEMA=public` or pass `--pg_schema public`.
Keep an existing custom schema setting unchanged. If startup finds
`public.idhan_info` while the new default schema has no migration history, it
stops with these instructions instead of creating a second, empty installation.
Moving an existing installation into `idhan` requires a separate migration.

## Required extensions

A database administrator must provision these extensions before starting IDHAN:

```sql
CREATE EXTENSION IF NOT EXISTS pgcrypto WITH SCHEMA public;
CREATE EXTENSION IF NOT EXISTS pg_trgm WITH SCHEMA public;
CREATE EXTENSION IF NOT EXISTS pg_stat_statements WITH SCHEMA public;
CREATE EXTENSION IF NOT EXISTS vector WITH SCHEMA public;
```

Startup checks all four extensions before creating the application schema or
running numbered migrations. It reports every missing extension together with
installation commands, and reports extensions outside the supported search path.
This applies to empty databases and existing installations alike. IDHAN does not
install, relocate, or update extensions, and does not inspect unrelated schemas
or tables to decide whether it owns the database. The extension supporting files
must be installed on the PostgreSQL server before the administrator can run the
commands above.

The `vector` extension must be in `public`. Existing installations may keep
`pgcrypto`, `pg_trgm`, and `pg_stat_statements` in their configured application
schema or in `public`; installations in other schemas require administrator
configuration. Startup leaves installed extensions unchanged. The historical
extension migrations encounter already-installed extensions and perform no
installation.

Initialization runs in a transaction before numbered application migrations;
it also runs for databases whose migration history is already current. Runtime
connections apply the same search path whenever they are established. The
application role needs the appropriate permissions for its application schema
and migrations, but does not need extension-installation privileges.
