-- Alias repair looks up original tags even when they already have an ideal tag.
-- The display lookup index restricted to ideal_tag_id IS NULL cannot serve this query.
CREATE INDEX IF NOT EXISTS active_tag_mappings_alias_repair_idx
    ON active_tag_mappings (tag_id, tag_domain_id);
