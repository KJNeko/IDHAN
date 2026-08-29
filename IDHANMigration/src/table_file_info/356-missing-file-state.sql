ALTER TABLE file_info
    DROP CONSTRAINT file_info_check1;
ALTER TABLE file_info
    DROP CONSTRAINT file_info_check;

ALTER TABLE file_info
    ADD CONSTRAINT file_info_mime_check CHECK ( mime_id IS NOT NULL OR extension IS NOT NULL );