# Database initialization

New installations use the `idhan` PostgreSQL schema for application objects.
Startup creates that schema before applying migrations. The application schema
remains configurable through `database.schema`, `IDHAN_DATABASE_SCHEMA`, or
`--pg_schema`; the default is `idhan`. IDHAN treats the database as shared: other
applications may have their own schemas and tables, which startup leaves alone.

Migration and runtime connections use the same search path: the configured
application schema first, followed by the configured extension schema for types,
functions, and operators. Schema names are quoted as individual identifiers;
when both settings name the same schema, it appears only once. `public` is not
implicitly added when a different extension schema is selected.

## Existing installations

Startup checks for a table named `public.idhan_info` before creating the
application schema or running migrations. If it exists, startup fails even when
the configured application schema already has its own migration history, or
`database.schema` explicitly selects `public`.

Existing installations in `public` must be migrated manually into the configured
application schema before using this version. Alternatively, configure a new
database; startup will initialize its application schema after checking the
required extensions. IDHAN provides no automatic schema relocation option.

For a manual migration, preserve application data and migration history together,
including `idhan_info`, generated tag partitions, embedding tables, sequences,
views, functions, types, ownership, and permissions. Review schema references and
function search paths, then update `database.schema` and any environment or CLI
overrides to select the destination. The completed migration must leave no
`public.idhan_info` table. Existing installations already in a custom application
schema can retain that setting, provided `public.idhan_info` is absent.

## Required extensions

The extension schema defaults to `public`. Override it through
`database.extension_schema`, `IDHAN_DATABASE_EXTENSION_SCHEMA`, or
`--pg_extension_schema`, using the normal configuration precedence. For example:

```toml
[database]
schema = "idhan"
extension_schema = "extensions"
```

Startup creates the extension schema if it is missing and the database role has
permission, then attempts to create each missing extension there. With the
default setting it runs the equivalent of:

```sql
CREATE EXTENSION IF NOT EXISTS pgcrypto WITH SCHEMA public;
CREATE EXTENSION IF NOT EXISTS pg_trgm WITH SCHEMA public;
CREATE EXTENSION IF NOT EXISTS pg_stat_statements WITH SCHEMA public;
CREATE EXTENSION IF NOT EXISTS vector WITH SCHEMA public;
```

Startup checks all four extensions before creating the application schema or
running numbered migrations. Creation succeeds when the extension files are
installed on PostgreSQL and the configured role has the privileges required by
that extension and the selected extension schema. Extensions requiring superuser privileges
still need an administrator when IDHAN runs with a restricted role; IDHAN does
not elevate privileges.

Each creation attempt uses a savepoint. If an attempt fails, startup continues
checking the remaining dependencies and reports all failures with PostgreSQL's
reason and the corresponding installation command for an administrator. Failure
rolls back the entire bootstrap attempt, including extensions successfully
created earlier in that attempt. Existing data and extensions remain unchanged.
Numbered migrations are not run until every extension is available in a supported
schema. A newly created extension schema is also rolled back if bootstrap fails.

The database is treated as shared; extension creation does not depend on IDHAN
being its only application. The legacy `public.idhan_info` rejection runs before
any extension creation. IDHAN does not relocate or update installed extensions.

The `vector` extension must be in the configured extension schema. Existing
installations may keep `pgcrypto`, `pg_trgm`, and `pg_stat_statements` in the
configured application schema or extension schema. An extension already installed
elsewhere causes an error: select its schema in configuration or ask an
administrator to reconcile the layout manually. Changing the setting does not
move or update existing extensions. All four extensions must be reachable through
the resulting application/extension search path. The historical
extension migrations encounter already-installed extensions and perform no
installation.

Initialization runs in a transaction before numbered application migrations;
it also runs for databases whose migration history is already current. Runtime
connections apply the same search path whenever they are established. The
application role needs the appropriate permissions for its application schema
and migrations. Extension-installation privileges are only needed when required
extensions are missing; administrators can provision them in advance for a
restricted application role.
