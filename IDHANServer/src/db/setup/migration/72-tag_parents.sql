CREATE TABLE tag_parents
(
    parent_id       tagid REFERENCES tags (tag_id)                  NOT NULL,
    ideal_parent_id tagid REFERENCES tags (tag_id)                  NULL,
    child_id        tagid REFERENCES tags (tag_id)                  NOT NULL,
    ideal_child_id  tagid REFERENCES tags (tag_id)                  NULL,
    tag_domain_id   SMALLINT REFERENCES tag_domains (tag_domain_id) NOT NULL,
    PRIMARY KEY (parent_id, child_id, tag_domain_id)
) PARTITION BY LIST (tag_domain_id);

