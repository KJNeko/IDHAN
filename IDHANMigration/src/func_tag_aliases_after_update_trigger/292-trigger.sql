-- The cascade re-sets ideal_alias_id on rows that already hold the value, and each of those no-op
-- updates re-ran the whole propagation from that row again.
DROP TRIGGER IF EXISTS tag_aliases_after_update ON tag_aliases;

CREATE TRIGGER tag_aliases_after_update
    AFTER UPDATE
    ON tag_aliases
    FOR EACH ROW
    WHEN ( old.aliased_id IS DISTINCT FROM new.aliased_id OR
           COALESCE(old.ideal_alias_id, old.alias_id) IS DISTINCT FROM COALESCE(new.ideal_alias_id, new.alias_id) )
EXECUTE FUNCTION tag_aliases_after_update_trigger();
