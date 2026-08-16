CREATE OR REPLACE FUNCTION create_embedding_model_table()
    RETURNS TRIGGER AS
$$
BEGIN
    EXECUTE format(
        'CREATE TABLE IF NOT EXISTS embeddings_%s (
             record_id INTEGER PRIMARY KEY REFERENCES records (record_id) ON DELETE CASCADE,
             embedding halfvec(%s)               NOT NULL
         )', new.model_id, new.model_dimensions );

    IF new.model_dimensions <= 4000 THEN
        -- Not CONCURRENTLY: illegal inside a trigger, and the table is empty at this point anyway.
        EXECUTE format(
            'CREATE INDEX IF NOT EXISTS embeddings_%s_hnsw
                 ON embeddings_%s USING hnsw (embedding halfvec_cosine_ops)',
            new.model_id, new.model_id );
    ELSE
        RAISE WARNING 'embedding model % has % dimensions, above pgvector''s HNSW limit of 4000; searches over it will be unindexed',
            new.model_name, new.model_dimensions;
    END IF;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

DO
$$
    DECLARE
        model RECORD;
    BEGIN
        FOR model IN SELECT model_id, model_name, model_dimensions FROM embedding_models
            LOOP
                IF model.model_dimensions <= 4000 THEN
                    EXECUTE format(
                        'CREATE INDEX IF NOT EXISTS embeddings_%s_hnsw
                             ON embeddings_%s USING hnsw (embedding halfvec_cosine_ops)',
                        model.model_id, model.model_id );
                ELSE
                    RAISE WARNING 'embedding model % has % dimensions, above pgvector''s HNSW limit of 4000; leaving it unindexed',
                        model.model_name, model.model_dimensions;
                END IF;
            END LOOP;
    END
$$;
