-- Search wants a nearest-neighbour index, and the cheapest moment to have one is from the table's
-- first row: an index built later has to be remembered, and a model that is registered but
-- unsearchable is a worse failure than a backfill that runs slower. The cost is real -- every
-- backfill INSERT now maintains the graph -- and is accepted deliberately.
--
-- halfvec_cosine_ops: vectors are stored L2-normalised, so cosine and inner product rank
-- identically. Cosine is chosen because it stays correct if a norm ever drifts.
--
-- 4000 is pgvector's HNSW ceiling for halfvec, while embedding_models allows up to 16000. A model
-- above the ceiling still registers and still works -- unindexed and slow -- rather than failing to
-- register at all.
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

-- Models registered before this migration already have tables built by the previous version of the
-- function, which created no index. Without this block the feature would only ever work for models
-- registered after the upgrade -- which, on any existing install, means none of them.
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
