CREATE TABLE hamming_distance_queue
(
    record_id INTEGER REFERENCES records (record_id) PRIMARY KEY
);
