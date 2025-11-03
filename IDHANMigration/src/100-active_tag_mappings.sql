CREATE INDEX ON active_tag_mappings (tag_id) INCLUDE (record_id) WHERE ideal_tag_id IS NULL;
CREATE INDEX ON active_tag_mappings (ideal_tag_id) INCLUDE (record_id) WHERE ideal_tag_id IS NOT NULL;

CREATE INDEX ON active_tag_mappings_parents (tag_id) INCLUDE (record_id);