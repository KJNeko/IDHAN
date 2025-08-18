CREATE TABLE archive_metadata
(
    password_bytes    bytea     NULL,                  -- Password encoded into bytes, no password if null, or unknown
    contained_records INTEGER[] NOT NULL,              -- List of all the records contained within this archive
    requires_password BOOLEAN   NOT NULL DEFAULT FALSE -- Password required to access this archive
);