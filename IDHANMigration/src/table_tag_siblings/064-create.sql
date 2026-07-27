CREATE TABLE tag_siblings
(
    older_id      INTEGER REFERENCES tags (tag_id)                                  NOT NULL,
    younger_id    INTEGER REFERENCES tags (tag_id)                                  NOT NULL,
    tag_domain_id SMALLINT REFERENCES tag_domains (tag_domain_id) ON DELETE CASCADE NOT NULL,
    PRIMARY KEY (older_id, younger_id, tag_domain_id),
    CHECK (older_id != younger_id)
);