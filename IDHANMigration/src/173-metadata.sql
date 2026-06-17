-- Promote UNIQUE constraints to explicit PRIMARY KEYs on metadata tables.
-- All of these have record_id as their sole unique key with no declared PK.

ALTER TABLE metadata
    DROP CONSTRAINT IF EXISTS metadata_record_id_key;
ALTER TABLE metadata
    ADD PRIMARY KEY (record_id);

ALTER TABLE image_metadata
    DROP CONSTRAINT IF EXISTS image_metadata_record_id_key;
ALTER TABLE image_metadata
    ADD PRIMARY KEY (record_id);

ALTER TABLE video_metadata
    DROP CONSTRAINT IF EXISTS video_metadata_record_id_key;
ALTER TABLE video_metadata
    ADD PRIMARY KEY (record_id);

ALTER TABLE image_project_metadata
    DROP CONSTRAINT IF EXISTS image_project_metadata_record_id_key;
ALTER TABLE image_project_metadata
    ADD PRIMARY KEY (record_id);

ALTER TABLE archive_metadata
    DROP CONSTRAINT IF EXISTS archive_metadata_record_id_key;
ALTER TABLE archive_metadata
    ADD PRIMARY KEY (record_id);
