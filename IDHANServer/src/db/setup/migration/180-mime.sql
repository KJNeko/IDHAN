INSERT INTO mime(name, best_extension)
VALUES ('application/psd', 'psd'),
       ('image/gif', 'gif'),
       ('application/zip', 'zip'),
       ('application/octet-stream', 'bin')
ON CONFLICT DO NOTHING;