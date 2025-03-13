CREATE TABLE tag_mappings
(
    record_id INTEGER,
    tag_id    INTEGER REFERENCES tags (tag_id),
    domain_id SMALLINT REFERENCES tag_domains (tag_domain_id)
);