CREATE TABLE tag_counts
(
    tag_id        INTEGER REFERENCES tags (tag_id),
    storage_count INTEGER NOT NULL DEFAULT 0,
    display_count INTEGER NOT NULL DEFAULT 0,
    tag_domain_id SMALLINT REFERENCES tag_domains (tag_domain_id) ON DELETE CASCADE,
    PRIMARY KEY (tag_id, tag_domain_id)
);
