ALTER TABLE urls
    ADD COLUMN url_type UrlType NOT NULL DEFAULT 'unknown';

-- anything with an extension is likely just a file.
UPDATE urls
SET url_type = 'file'
WHERE url ~* '\.[a-z]{3}$';

UPDATE urls
SET url_type = 'post'
WHERE url LIKE '%post%';