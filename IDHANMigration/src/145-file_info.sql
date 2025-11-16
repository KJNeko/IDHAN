DELETE
FROM file_info
WHERE mime_id IS NULL
  AND extension IS NULL;


ALTER TABLE file_info
    ADD CONSTRAINT file_infocheck_mime_or_extension
        CHECK (NOT (mime_id IS NULL AND extension IS NULL));