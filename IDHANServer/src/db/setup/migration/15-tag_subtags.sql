CREATE TABLE tag_subtags
(
    subtag_id   BIGSERIAL PRIMARY KEY,
    subtag_text TEXT NOT NULL UNIQUE
);
