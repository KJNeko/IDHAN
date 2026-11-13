CREATE TABLE downloader_domain_secrets
(
    domain  TEXT  NOT NULL PRIMARY KEY,
    secrets JSONB NOT NULL CHECK (jsonb_typeof(secrets) = 'object')
);
