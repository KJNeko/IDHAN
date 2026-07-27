-- Hydrus stores unknown-filetype files with no extension (APPLICATION_UNKNOWN -> ''),
-- unlike application/octet-stream which uses 'bin'.
INSERT INTO mime (name, best_extension)
VALUES ('unknown/unknown', '')
ON CONFLICT DO NOTHING;
