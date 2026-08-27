CREATE INDEX active_tag_mappings_tag_id_tag_domain_id_idx
    ON active_tag_mappings (tag_id, tag_domain_id) INCLUDE (record_id)
    WHERE ideal_tag_id IS NULL;

CREATE INDEX active_tag_mappings_ideal_tag_id_tag_domain_id_idx
    ON active_tag_mappings (ideal_tag_id, tag_domain_id) INCLUDE (record_id)
    WHERE ideal_tag_id IS NOT NULL;

DROP INDEX IF EXISTS active_tag_mappings_tag_id_record_id_idx;
DROP INDEX IF EXISTS active_tag_mappings_ideal_tag_id_record_id_idx;
