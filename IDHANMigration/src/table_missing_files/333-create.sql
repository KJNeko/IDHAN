CREATE TABLE missing_files
(
    record_id INTEGER PRIMARY KEY REFERENCES file_info (record_id) ON DELETE CASCADE
);
