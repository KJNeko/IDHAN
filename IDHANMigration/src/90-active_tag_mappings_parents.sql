CREATE TABLE active_tag_mappings_parents
(
    record_id      INTEGER REFERENCES records (record_id)          NOT NULL,
    tag_id         INTEGER REFERENCES tags (tag_id)                NOT NULL,
    origin_id      INTEGER REFERENCES tags (tag_id)                NOT NULL,
    internal_count INTEGER DEFAULT 0                               NOT NULL,   -- The number of parents that are adding this tag
    tag_domain_id  SMALLINT REFERENCES tag_domains (tag_domain_id) NOT NULL,
    internal       BOOLEAN GENERATED ALWAYS AS ( internal_count > 0 ) VIRTUAL, -- Set to true if another parent is what added this tag
    PRIMARY KEY (record_id, tag_id, origin_id, tag_domain_id)
);