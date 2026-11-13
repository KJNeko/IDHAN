-- URL ids become INTEGER positions within their session. The old global ids stay in
-- legacy_url_id until the error log has been remapped.
ALTER TABLE download_session_urls
    DROP CONSTRAINT download_session_urls_parent_url_id_fkey;

ALTER TABLE download_session_urls
    DROP CONSTRAINT download_session_urls_pkey;

DROP INDEX download_session_urls_session_created_idx;
DROP INDEX download_session_urls_nonterminal_idx;

ALTER TABLE download_session_urls
    RENAME COLUMN download_session_url_id TO legacy_url_id;

ALTER TABLE download_session_urls
    RENAME COLUMN parent_url_id TO legacy_parent_url_id;

ALTER TABLE download_session_urls
    ADD COLUMN download_session_url_id INTEGER,
    ADD COLUMN parent_url_id           INTEGER;

-- Parents always received a lower global id than their children, so numbering by it keeps that order.
UPDATE download_session_urls AS target
SET download_session_url_id = numbered.position
FROM (SELECT legacy_url_id,
             row_number() OVER (PARTITION BY download_session_id ORDER BY legacy_url_id) AS position
      FROM download_session_urls) AS numbered
WHERE target.legacy_url_id = numbered.legacy_url_id;

UPDATE download_session_urls AS child
SET parent_url_id = parent.download_session_url_id
FROM download_session_urls AS parent
WHERE child.legacy_parent_url_id = parent.legacy_url_id;

ALTER TABLE download_session_urls
    DROP COLUMN legacy_parent_url_id;

ALTER TABLE download_session_urls
    ALTER COLUMN download_session_url_id SET NOT NULL;

ALTER TABLE download_session_urls
    ADD CONSTRAINT download_session_urls_pkey PRIMARY KEY (download_session_id, download_session_url_id);

ALTER TABLE download_session_urls
    ADD CONSTRAINT download_session_urls_parent_fkey
        FOREIGN KEY (download_session_id, parent_url_id)
            REFERENCES download_session_urls (download_session_id, download_session_url_id) ON DELETE CASCADE;

CREATE INDEX download_session_urls_parent_idx
    ON download_session_urls (download_session_id, parent_url_id, download_session_url_id)
    WHERE parent_url_id IS NOT NULL;

CREATE INDEX download_session_urls_nonterminal_idx
    ON download_session_urls (download_session_id, download_session_url_id)
    WHERE state IN ('pending', 'processing');
