CREATE TABLE image_project_metadata
(
    record_id INTEGER REFERENCES records (record_id),
    width     INTEGER  NOT NULL,
    height    INTEGER  NOT NULL,
    channels  SMALLINT NOT NULL,
    layers    SMALLINT NOT NULL,
    PRIMARY KEY (record_id)
);
