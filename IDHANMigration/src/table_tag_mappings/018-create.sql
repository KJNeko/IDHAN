CREATE TABLE tag_mappings
(
    record_id     INTEGER REFERENCES records (record_id)                            NOT NULL,
    tag_id        INTEGER REFERENCES tags (tag_id)                                  NOT NULL,
    tag_domain_id SMALLINT REFERENCES tag_domains (tag_domain_id) ON DELETE CASCADE NOT NULL,
    PRIMARY KEY (record_id, tag_id, tag_domain_id)
) PARTITION BY LIST (tag_domain_id);
