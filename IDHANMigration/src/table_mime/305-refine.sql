-- Mime ids are a closed set defined in IDHAN/include/MimeIDs.hpp and seeded by syncMimeTable() at
-- boot. No mime is named here: this migration only reshapes the table so a refined type can share
-- its base's name and so ids can be renumbered onto their pinned values.

ALTER TABLE mime
    DROP CONSTRAINT IF EXISTS mime_name_key,
    ADD COLUMN base_mime_id INTEGER REFERENCES mime (mime_id) ON UPDATE CASCADE;

ALTER TABLE file_info
    DROP CONSTRAINT IF EXISTS file_info_mime_id_fkey,
    ADD CONSTRAINT file_info_mime_id_fkey FOREIGN KEY (mime_id) REFERENCES mime (mime_id) ON UPDATE CASCADE;

CREATE UNIQUE INDEX mime_name_base_unique ON mime (name) WHERE base_mime_id IS NULL;
