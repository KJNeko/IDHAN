CREATE TABLE tag_aliases
(
    aliased_id       INTEGER REFERENCES tags (tag_id)                NOT NULL,
    alias_id         INTEGER REFERENCES tags (tag_id)                NOT NULL,
    ideal_alias_id   INTEGER REFERENCES tags (tag_id)                NULL,
    tag_domain_id    SMALLINT REFERENCES tag_domains (tag_domain_id) NOT NULL,
    effective_tag_id INTEGER GENERATED ALWAYS AS (COALESCE(ideal_alias_id, alias_id)) VIRTUAL,
    PRIMARY KEY (aliased_id, alias_id, tag_domain_id),
    UNIQUE (tag_domain_id, aliased_id),
    CHECK ( aliased_id != alias_id )
) PARTITION BY LIST (tag_domain_id);