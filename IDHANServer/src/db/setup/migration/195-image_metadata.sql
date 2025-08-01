CREATE TABLE image_metadata
(
    record_id INTEGER REFERENCES records (record_id) NOT NULL,
    width     INTEGER                                NOT NULL,
    height    INTEGER                                NOT NULL,
    channels  SMALLINT                               NOT NULL,
    UNIQUE (record_id)
);