CREATE TABLE embedding_models
(
    model_id         SERIAL PRIMARY KEY,
    model_name       TEXT                        NOT NULL UNIQUE,
    model_dimensions INTEGER                     NOT NULL,
    created_time     TIMESTAMP WITHOUT TIME ZONE NOT NULL DEFAULT now(),
    CHECK ( model_dimensions > 0 AND model_dimensions <= 16000 )
);
