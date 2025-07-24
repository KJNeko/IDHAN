CREATE INDEX ON flattened_aliases (original_alias_id, domain_id);
CREATE INDEX ON flattened_aliases (alias_id, domain_id);
CREATE INDEX ON flattened_aliases (aliased_id, domain_id);
CREATE INDEX ON flattened_aliases (COALESCE(alias_id, original_alias_id), domain_id);

CREATE INDEX ON tag_mappings (tag_id, domain_id);
CREATE INDEX ON tag_mappings (COALESCE(ideal_tag_id, tag_id));

CREATE INDEX ON aliased_siblings (original_younger_id, domain_id);
CREATE INDEX ON aliased_siblings (original_older_id, domain_id);
CREATE INDEX ON aliased_siblings (COALESCE(younger_id, original_younger_id), domain_id);
CREATE INDEX ON aliased_siblings (COALESCE(older_id, original_older_id), domain_id);

CREATE INDEX ON aliased_parents (original_parent_id, domain_id);
CREATE INDEX ON aliased_parents (original_child_id, domain_id);
CREATE INDEX ON aliased_parents (COALESCE(child_id, original_child_id), domain_id);
CREATE INDEX ON aliased_parents (COALESCE(parent_id, original_parent_id), domain_id);

CREATE INDEX ON records (sha256);

CREATE INDEX ON tag_mappings_virtual (record_id, tag_id);