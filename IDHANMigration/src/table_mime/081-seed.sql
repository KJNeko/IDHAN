INSERT INTO mime (name, best_extension)
VALUES ('unknown/unknown', '')
ON CONFLICT DO NOTHING;
