-- The insert trigger creates embeddings_<model_id>; without a matching delete trigger, removing a
-- model leaves its table behind holding every vector it ever computed, invisible to the API and
-- impossible to reach except by hand. Worse, the SERIAL would eventually hand the same model_id to a
-- different model, and CREATE TABLE IF NOT EXISTS would silently adopt the stale table -- a new model
-- reading another model's vectors, with no error anywhere.
--
-- Done in a trigger rather than in the endpoint so the table cannot outlive its row whatever deletes
-- it: a manual DELETE, a cascade, or a future code path that has not been written yet. PostgreSQL
-- DDL is transactional, so a rollback takes the DROP with it.
CREATE OR REPLACE FUNCTION drop_embedding_model_table()
    RETURNS TRIGGER AS
$$
BEGIN
    EXECUTE format( 'DROP TABLE IF EXISTS embeddings_%s', old.model_id );
    RETURN old;
END;
$$ LANGUAGE plpgsql;

-- AFTER, to match the create side: a constraint failure on the delete must not have already dropped
-- the table.
CREATE TRIGGER trg_drop_embedding_model_table
    AFTER DELETE
    ON embedding_models
    FOR EACH ROW
EXECUTE FUNCTION drop_embedding_model_table();
