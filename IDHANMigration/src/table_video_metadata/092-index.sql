-- Supporting indexes for the video sort types (see the file_info search-sort indexes).
CREATE INDEX ON video_metadata (duration, record_id);
CREATE INDEX ON video_metadata (framerate, record_id);
CREATE INDEX ON video_metadata (has_audio, record_id);
