CREATE TABLE tag_aliases
(
    alias_id   INTEGER REFERENCES tags (tag_id),
    aliased_id INTEGER REFERENCES tags (tag_id),
    domain_id  SMALLINT REFERENCES tag_domains (tag_domain_id),
    UNIQUE (alias_id, aliased_id, domain_id),
    UNIQUE (aliased_id, domain_id) -- Only allow for one alias per tag
) PARTITION BY LIST (domain_id);