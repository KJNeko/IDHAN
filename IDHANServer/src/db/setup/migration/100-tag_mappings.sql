CREATE TABLE tag_mappings
(
    record_id INTEGER REFERENCES records (record_id),
    tag_id    INTEGER REFERENCES tags (tag_id),
    domain_id SMALLINT REFERENCES tag_domains (tag_domain_id),
    UNIQUE (record_id, tag_id, domain_id)
) PARTITION BY LIST (domain_id);