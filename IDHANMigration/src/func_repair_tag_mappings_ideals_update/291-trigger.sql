-- Alias cascades re-set ideal_alias_id to the value a row already held, and each of those no-op
-- updates rewrote every active_tag_mappings row carrying the tag.
DROP TRIGGER IF EXISTS trg_repair_tag_mappings_ideals_update ON tag_aliases;

CREATE TRIGGER trg_repair_tag_mappings_ideals_update
    AFTER UPDATE
    ON tag_aliases
    FOR EACH ROW
    WHEN ( old.aliased_id IS DISTINCT FROM new.aliased_id OR
           COALESCE(old.ideal_alias_id, old.alias_id) IS DISTINCT FROM COALESCE(new.ideal_alias_id, new.alias_id) )
EXECUTE FUNCTION repair_tag_mappings_ideals_update();
