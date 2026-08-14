
CREATE OR REPLACE FUNCTION repropagate_parents_on_ideal_change()
    RETURNS TRIGGER AS
$$
DECLARE
    old_effective INTEGER := COALESCE(OLD.ideal_tag_id, OLD.tag_id);
    new_effective INTEGER := COALESCE(NEW.ideal_tag_id, NEW.tag_id);
BEGIN
    IF old_effective IS NOT DISTINCT FROM new_effective THEN
        RETURN NEW;
    END IF;

    IF NOT EXISTS (
        SELECT 1
        FROM active_tag_mappings other
        WHERE other.record_id = OLD.record_id
          AND other.tag_domain_id = OLD.tag_domain_id
          AND COALESCE(other.ideal_tag_id, other.tag_id) = old_effective
    ) THEN
        DELETE FROM active_tag_mappings_parents
        WHERE record_id = OLD.record_id
          AND origin_id = old_effective
          AND tag_domain_id = OLD.tag_domain_id
          AND NOT internal;
    END IF;

    INSERT INTO active_tag_mappings_parents (record_id, tag_id, origin_id, tag_domain_id)
    SELECT NEW.record_id,
           COALESCE(tp.ideal_parent_id, tp.parent_id),
           new_effective,
           NEW.tag_domain_id
    FROM tag_parents tp
    WHERE tp.tag_domain_id = NEW.tag_domain_id
      AND COALESCE(tp.ideal_child_id, tp.child_id) = new_effective
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id) DO NOTHING;
    -- trg_atmp_internal_insert cascades this further up automatically.

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_repropagate_parents_on_ideal_change
    AFTER UPDATE
    ON active_tag_mappings
    FOR EACH ROW
    WHEN ( COALESCE( OLD.ideal_tag_id, OLD.tag_id ) IS DISTINCT FROM COALESCE( NEW.ideal_tag_id, NEW.tag_id ) )
EXECUTE FUNCTION repropagate_parents_on_ideal_change();
