-- Supports bounded URL merges and foreign-key checks when removing merged URLs.
CREATE INDEX url_mappings_url_record_idx ON url_mappings (url_id, record_id);
