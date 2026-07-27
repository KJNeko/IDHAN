-- Supporting index for the creation-time sort (see the file_info search-sort indexes).
CREATE INDEX ON records (creation_time, record_id);
