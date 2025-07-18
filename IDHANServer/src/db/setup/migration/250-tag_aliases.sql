ALTER TABLE tag_aliases
    ADD CONSTRAINT check_different_ids
        CHECK (aliased_id != alias_id);