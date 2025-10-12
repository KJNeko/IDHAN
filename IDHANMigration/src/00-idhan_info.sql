CREATE TABLE idhan_info
(
    table_name        TEXT PRIMARY KEY UNIQUE NOT NULL,
    last_migration_id INTEGER                 NOT NULL,
    queries           TEXT[]                  NOT NULL
);

CREATE EXTENSION IF NOT EXISTS pgcrypto;
CREATE EXTENSION IF NOT EXISTS pg_trgm;