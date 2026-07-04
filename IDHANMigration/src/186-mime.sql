INSERT INTO mime (name, best_extension)
VALUES ('unknown/unknown', 'bin')
ON CONFLICT DO NOTHING;
