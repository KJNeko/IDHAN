-- Extensions are database-wide, not schema-scoped, and the migration runner executes the whole set
-- once per schema (migrations.cpp tracks last_migration_id per schema). A bare CREATE EXTENSION
-- therefore fails on whichever schema migrates second. Pinned to public so every schema resolves
-- halfvec through the search_path, which always ends in public.
CREATE EXTENSION IF NOT EXISTS vector WITH SCHEMA public;
