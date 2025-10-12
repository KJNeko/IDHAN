CREATE TABLE active_tag_mappings_parents
(
    record_id      INTEGER REFERENCES records (record_id)          NOT NULL,
    tag_id         BIGINT REFERENCES tags (tag_id)                 NOT NULL,
    origin_id      BIGINT REFERENCES tags (tag_id)                 NOT NULL,
    tag_domain_id  SMALLINT REFERENCES tag_domains (tag_domain_id) NOT NULL,
    internal       BOOLEAN DEFAULT FALSE                           NOT NULL,
    internal_count INTEGER DEFAULT 0                               NOT NULL,
    PRIMARY KEY (record_id, tag_id, origin_id, tag_domain_id)
);