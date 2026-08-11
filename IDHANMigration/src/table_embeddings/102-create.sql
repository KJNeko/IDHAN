CREATE TABLE embeddings
(
    record_id INTEGER REFERENCES records (record_id) ON DELETE CASCADE,
    embedding halfvec(1536) NOT NULL
);