ALTER TABLE file_info
    ADD CONSTRAINT file_info_size_non_negative CHECK (size >= 0);
