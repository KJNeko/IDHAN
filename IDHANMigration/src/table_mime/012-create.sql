CREATE TABLE mime
(
    mime_id        SERIAL PRIMARY KEY,
    name           TEXT UNIQUE NOT NULL,
    best_extension TEXT        NOT NULL
);

INSERT INTO mime (name, best_extension)
VALUES ('image/jpeg', 'jpg'),
       ('image/gif', 'gif'),
       ('image/apng', 'png'),
       ('image/avif', 'avif'),
       ('image/webp', 'webp'),
       ('image/png', 'png'),
       ('video/mp4', 'mp4'),
       ('video/mpeg', 'mpeg'),
       ('video/webm', 'webm'),
       ('application/psd', 'psd'),
       ('application/zip', 'zip')
ON CONFLICT DO NOTHING;