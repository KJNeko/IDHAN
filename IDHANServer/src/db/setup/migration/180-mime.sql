INSERT INTO mime(name, best_extension)
VALUES ('application/psd', 'psd'),
       ('animated/gif', 'gif'),
       ('application/zip', 'zip')
ON CONFLICT DO NOTHING;