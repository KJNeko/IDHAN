ALTER TABLE file_info
    DROP CONSTRAINT file_info_reorder_mime_id_fkey,
    ADD CONSTRAINT file_info_mime_id_fkey FOREIGN KEY (mime_id) REFERENCES mime (mime_id) ON UPDATE CASCADE;
