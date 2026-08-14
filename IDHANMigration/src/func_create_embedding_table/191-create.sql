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
