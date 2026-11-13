-- Why a failed URL failed. Removed together with the URL, and cleared when the URL is retried.
CREATE TABLE download_session_url_failures
(
    download_session_id     BIGINT  NOT NULL,
    download_session_url_id INTEGER NOT NULL,
    -- Unset for failures recorded before stages existed.
    stage                   TEXT CHECK (stage IN ('route', 'download', 'parser', 'import', 'closed')),
    message                 TEXT    NOT NULL CHECK (message <> ''),
    PRIMARY KEY (download_session_id, download_session_url_id),
    FOREIGN KEY (download_session_id, download_session_url_id)
        REFERENCES download_session_urls (download_session_id, download_session_url_id) ON DELETE CASCADE
);

INSERT INTO download_session_url_failures (download_session_id, download_session_url_id, message)
SELECT download_session_id, download_session_url_id, error
FROM download_session_urls
WHERE state = 'failed'
  AND error IS NOT NULL
  AND error <> '';
