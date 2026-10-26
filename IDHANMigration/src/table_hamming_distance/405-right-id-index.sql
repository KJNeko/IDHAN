-- Distance cleanup probes both endpoints of moved records.
CREATE INDEX IF NOT EXISTS hamming_distance_right_id_idx ON hamming_distance (right_id);
