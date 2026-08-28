CREATE TABLE audio_metadata
(
    record_id   INTEGER REFERENCES records (record_id) NOT NULL,
    duration    FLOAT                                  NOT NULL,
    bitrate     INTEGER                                NOT NULL,
    channels    SMALLINT                               NOT NULL,
    sample_rate INTEGER                                NOT NULL,
    PRIMARY KEY (record_id)
);
