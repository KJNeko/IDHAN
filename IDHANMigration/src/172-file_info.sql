-- The original file_info table declared both UNIQUE and PRIMARY KEY on record_id,
-- creating a redundant constraint/index alongside file_info_pkey.
-- Migration 101 dropped the manually-created file_info_record_id_idx;
-- this drops the remaining constraint-backed UNIQUE index.
ALTER TABLE file_info
    DROP CONSTRAINT IF EXISTS file_info_record_id_key;
