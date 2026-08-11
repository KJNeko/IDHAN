-- One row per embedding model the server can route to. model_name is the routing key reported by a
-- module's manifest, so it must be unique: startup registration upserts on it, and without the
-- constraint every restart would insert a duplicate model.
--
-- model_dimensions is what the per-model table's halfvec(N) column is built from (see the trigger in
-- func_create_embedding_table). 16000 is pgvector's storage ceiling for halfvec.
CREATE TABLE embedding_models
(
    model_id         SERIAL PRIMARY KEY,
    model_name       TEXT                        NOT NULL UNIQUE,
    model_dimensions INTEGER                     NOT NULL,
    created_time     TIMESTAMP WITHOUT TIME ZONE NOT NULL DEFAULT now(),
    CHECK ( model_dimensions > 0 AND model_dimensions <= 16000 )
);
