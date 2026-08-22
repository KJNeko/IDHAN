-- Every mime id stands on its own. Two ids may report the same name (a Pixiv Ugoira reports
-- "application/zip"), so the name is not unique and no id points at another. A lookup by name
-- answers with the lowest id carrying it, which is the generic type for that name.

DROP INDEX IF EXISTS mime_name_base_unique;

ALTER TABLE mime
    DROP COLUMN IF EXISTS base_mime_id;
