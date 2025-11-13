CREATE TABLE video_metadata
(
    record_id INTEGER REFERENCES records (record_id),
    duration  FLOAT   NOT NULL,
    framerate FLOAT   NOT NULL,
    width     INTEGER NOT NULL,
    height    INTEGER NOT NULL,
    bitrate   INTEGER NOT NULL,
    has_audio BOOLEAN NOT NULL,
    UNIQUE (record_id)
);