-- Append grouped contributions without contending on shared counter rows.
CREATE TABLE tag_count_delta
(
    tag_id        INTEGER  NOT NULL REFERENCES tags (tag_id) ON DELETE CASCADE,
    tag_domain_id SMALLINT NOT NULL REFERENCES tag_domains (tag_domain_id) ON DELETE CASCADE,
    storage_delta BIGINT   NOT NULL DEFAULT 0,
    display_delta BIGINT   NOT NULL DEFAULT 0,
    CHECK (storage_delta <> 0 OR display_delta <> 0)
);
