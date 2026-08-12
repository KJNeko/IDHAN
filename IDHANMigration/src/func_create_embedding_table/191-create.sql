-- A halfvec(N) column is fixed-width per table, so one shared embeddings table cannot hold vectors
-- from models of differing dimensionality -- a model_id column on such a table is a promise it can
-- never keep. Registering a model instead creates that model's own table, following the same
-- runtime-DDL-from-a-trigger pattern as createtagdomainpartitions.
--
-- The per-model table carries no model_id: the table name is the model. That makes record_id the
-- natural primary key, which gives idempotent ON CONFLICT (record_id) DO UPDATE writes (so an
-- interrupted backfill is safe to re-run) and is exactly the index the "records with no embedding
-- yet" anti-join wants.
CREATE OR REPLACE FUNCTION create_embedding_model_table()
    RETURNS TRIGGER AS
$$
BEGIN
    EXECUTE format(
            'CREATE TABLE IF NOT EXISTS embeddings_%s (
                 record_id INTEGER PRIMARY KEY REFERENCES records (record_id) ON DELETE CASCADE,
                 embedding halfvec(%s)               NOT NULL
             )', new.model_id, new.model_dimensions);
    RETURN new;
END;
$$ LANGUAGE plpgsql;

-- AFTER rather than BEFORE: a constraint failure on the insert must not leave an orphan table.
CREATE TRIGGER trg_create_embedding_model_table
    AFTER INSERT
    ON embedding_models
    FOR EACH ROW
EXECUTE FUNCTION create_embedding_model_table();
