ALTER TABLE active_tag_mappings
    ADD COLUMN effective_tag_id INTEGER GENERATED ALWAYS AS (COALESCE(ideal_tag_id, tag_id)) VIRTUAL;