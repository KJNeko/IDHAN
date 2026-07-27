CREATE OR REPLACE FUNCTION check_parent_cycle()
    RETURNS trigger
    LANGUAGE plpgsql
    VOLATILE
AS
$$
BEGIN
    IF EXISTS (WITH RECURSIVE ancestors AS (SELECT tp.parent_id, tp.tag_domain_id
                                            FROM tag_parents tp
                                            WHERE tp.child_id = NEW.parent_id
                                              AND tp.tag_domain_id = NEW.tag_domain_id
                                            UNION
                                            SELECT tp.parent_id, tp.tag_domain_id
                                            FROM tag_parents tp
                                                     INNER JOIN ancestors a
                                                                ON tp.child_id = a.parent_id AND tp.tag_domain_id = a.tag_domain_id)
               SELECT 1
               FROM ancestors
               WHERE parent_id = NEW.child_id) THEN
        RAISE EXCEPTION 'Cycle detected: inserting parent % for child % would create a cycle in domain %',
            NEW.parent_id, NEW.child_id, NEW.tag_domain_id;
    END IF;
    RETURN NEW;
END;
$$;

CREATE TRIGGER trg_check_parent_cycle
    BEFORE INSERT
    ON tag_parents
    FOR EACH ROW
EXECUTE FUNCTION check_parent_cycle();
