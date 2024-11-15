ALTER TABLE tag_namespaces
    ADD COLUMN color BYTEA CHECK (color IS NULL OR octet_length(color) = 3);