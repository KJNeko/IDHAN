CREATE TABLE animation_metadata
(
    record_id   INTEGER REFERENCES records (record_id) NOT NULL,
    width       INTEGER                                NOT NULL,
    height      INTEGER                                NOT NULL,
    frame_count INTEGER                                NOT NULL,
    duration    FLOAT                                  NOT NULL,
    loops       BOOLEAN                                NOT NULL,
    PRIMARY KEY (record_id)
);
