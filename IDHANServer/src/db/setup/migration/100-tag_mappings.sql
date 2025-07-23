CREATE TABLE tag_mappings
(
    record_id    INTEGER REFERENCES records (record_id)          NOT NULL,
    tag_id       INTEGER REFERENCES tags (tag_id)                NOT NULL,
    ideal_tag_id INTEGER REFERENCES tags (tag_id) DEFAULT NULL,
    domain_id    SMALLINT REFERENCES tag_domains (tag_domain_id) NOT NULL,
    PRIMARY KEY (record_id, tag_id, domain_id)
) PARTITION BY LIST (domain_id);