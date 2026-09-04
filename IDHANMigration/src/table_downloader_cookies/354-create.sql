-- Cookies the downloader may reuse across sessions and restarts. Only cookies that carried an
-- Expires or Max-Age are ever written here; a session cookie belongs to the session that received
-- it and dies with it.
CREATE TABLE downloader_cookies
(
    name      TEXT        NOT NULL,
    domain    TEXT        NOT NULL,
    path      TEXT        NOT NULL,
    value     TEXT        NOT NULL,
    secure    BOOLEAN     NOT NULL DEFAULT FALSE,
    host_only BOOLEAN     NOT NULL DEFAULT FALSE,
    expires   TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (name, domain, path)
);

CREATE INDEX downloader_cookies_expires_idx ON downloader_cookies (expires);
