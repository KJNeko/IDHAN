CREATE OR REPLACE FUNCTION drop_embedding_model_table()
    RETURNS TRIGGER AS
$$
BEGIN
    EXECUTE format( 'DROP TABLE IF EXISTS embeddings_%s', old.model_id );
    RETURN old;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_drop_embedding_model_table
    AFTER DELETE
    ON embedding_models
    FOR EACH ROW
EXECUTE FUNCTION drop_embedding_model_table();
