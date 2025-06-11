INSERT INTO mime(name, best_extension)
VALUES ('application/octet-stream', 'bin')
ON CONFLICT DO NOTHING;
