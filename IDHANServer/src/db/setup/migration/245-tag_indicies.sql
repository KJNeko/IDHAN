CREATE INDEX ON flattened_aliases (aliased_id, domain_id);
CREATE INDEX ON flattened_aliases (alias_id, domain_id);

CREATE INDEX ON tag_mappings (tag_id, domain_id);
CREATE INDEX ON tag_mappings (ideal_tag_id);
CREATE INDEX ON tag_mappings (record_id, domain_id);

CREATE INDEX idx_aliased_siblings_younger_domain ON aliased_siblings (younger_id, domain_id);
CREATE INDEX idx_aliased_siblings_older_domain ON aliased_siblings (older_id, domain_id);
CREATE INDEX idx_aliased_siblings_original_younger ON aliased_siblings (original_younger_id, domain_id);
CREATE INDEX idx_aliased_siblings_original_older ON aliased_siblings (original_older_id, domain_id);

CREATE INDEX idx_aliased_parents_child_domain ON aliased_parents (child_id, domain_id);
CREATE INDEX idx_aliased_parents_parent_domain ON aliased_parents (parent_id, domain_id);
CREATE INDEX idx_aliased_parents_original_parent ON aliased_parents (original_parent_id, domain_id);
CREATE INDEX idx_aliased_parents_original_child ON aliased_parents (original_child_id, domain_id);

CREATE INDEX idx_records_sha256 ON records (sha256);