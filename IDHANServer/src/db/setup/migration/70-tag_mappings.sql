CREATE TABLE tag_mappings
(
    record_id     recordid REFERENCES records (record_id)         NOT NULL,
    tag_id        tagid REFERENCES tags (tag_id)                  NOT NULL,
    tag_domain_id SMALLINT REFERENCES tag_domains (tag_domain_id) NOT NULL,
    PRIMARY KEY (record_id, tag_id, tag_domain_id)
) PARTITION BY LIST (tag_domain_id);

CREATE TABLE active_tag_mappings
(

    record_id     recordid REFERENCES records (record_id)         NOT NULL,
    tag_id        tagid REFERENCES tags (tag_id)                  NOT NULL,
    ideal_tag_id  tagid REFERENCES tags (tag_id)                  NULL,
    tag_domain_id SMALLINT REFERENCES tag_domains (tag_domain_id) NOT NULL,
    PRIMARY KEY (record_id, tag_id, tag_domain_id),
    FOREIGN KEY (record_id, tag_id, tag_domain_id) REFERENCES tag_mappings (record_id, tag_id, tag_domain_id) ON DELETE CASCADE
);

CREATE INDEX active_tag_mappings_coalesce_tag_id ON active_tag_mappings (COALESCE(ideal_tag_id, tag_id), tag_domain_id) INCLUDE (record_id);
