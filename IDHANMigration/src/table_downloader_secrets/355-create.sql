-- Secrets exposed to parsers through idhan.secret(). The server reads them from here, so a value
-- set through /downloader/secrets outlives the process that set it.
CREATE TABLE downloader_secrets
(
    name  TEXT NOT NULL PRIMARY KEY,
    value TEXT NOT NULL
);
