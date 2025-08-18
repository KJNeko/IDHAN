CREATE TABLE tag_aliases
(
    aliased_id     tagid REFERENCES tags (tag_id)                  NOT NULL,
    alias_id       tagid REFERENCES tags (tag_id)                  NOT NULL,
    ideal_alias_id tagid REFERENCES tags (tag_id)                  NULL,
    tag_domain_id  SMALLINT REFERENCES tag_domains (tag_domain_id) NOT NULL,
    PRIMARY KEY (aliased_id, alias_id, tag_domain_id),
    UNIQUE (tag_domain_id, aliased_id),
    CHECK ( aliased_id != alias_id )
) PARTITION BY LIST (tag_domain_id);