CREATE OR REPLACE FUNCTION process_record_sibling_silencing(
    p_record_id INTEGER,
    p_tag_domain_id SMALLINT
)
    RETURNS VOID
AS
$$
BEGIN
    -- Silence younger siblings whose older sibling exists in active_tag_mappings
    UPDATE active_tag_mappings atm
    SET silenced = TRUE
    FROM aliased_siblings asib
    WHERE asib.younger_id = atm.tag_id
      AND asib.tag_domain_id = atm.tag_domain_id
      AND atm.record_id = p_record_id
      AND atm.tag_domain_id = p_tag_domain_id
      AND EXISTS (SELECT 1
                  FROM active_tag_mappings atm2
                  WHERE atm2.record_id = p_record_id
                    AND atm2.tag_domain_id = p_tag_domain_id
                    AND atm2.tag_id = asib.older_id
                    AND NOT atm2.silenced)
      AND NOT atm.silenced;

    -- Silence younger siblings whose older sibling exists in active_tag_mappings_parents
    UPDATE active_tag_mappings atm
    SET silenced = TRUE
    FROM aliased_siblings asib
    WHERE asib.younger_id = atm.tag_id
      AND asib.tag_domain_id = atm.tag_domain_id
      AND atm.record_id = p_record_id
      AND atm.tag_domain_id = p_tag_domain_id
      AND EXISTS (SELECT 1
                  FROM active_tag_mappings_parents atmp
                  WHERE atmp.record_id = p_record_id
                    AND atmp.tag_domain_id = p_tag_domain_id
                    AND atmp.tag_id = asib.older_id)
      AND NOT atm.silenced;

    -- Unsilence tags whose older sibling is no longer present
    -- Only unsilence if NO older sibling (from any aliased pair) is present
    UPDATE active_tag_mappings atm
    SET silenced = FALSE
    WHERE atm.record_id = p_record_id
      AND atm.tag_domain_id = p_tag_domain_id
      AND atm.silenced
      AND NOT EXISTS (SELECT 1
                      FROM aliased_siblings asib
                      WHERE asib.younger_id = atm.tag_id
                        AND asib.tag_domain_id = atm.tag_domain_id
                        AND (EXISTS (SELECT 1
                                     FROM active_tag_mappings atm2
                                     WHERE atm2.record_id = p_record_id
                                       AND atm2.tag_domain_id = p_tag_domain_id
                                       AND atm2.tag_id = asib.older_id
                                       AND NOT atm2.silenced)
                          OR EXISTS (SELECT 1
                                     FROM active_tag_mappings_parents atmp
                                     WHERE atmp.record_id = p_record_id
                                       AND atmp.tag_domain_id = p_tag_domain_id
                                       AND atmp.tag_id = asib.older_id)));
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION func_tag_mappings_after_delete_sibling()
    RETURNS TRIGGER
AS
$$
BEGIN
    PERFORM process_record_sibling_silencing(old.record_id, old.tag_domain_id);

    RETURN old;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_tag_mappings_after_delete_sibling
    AFTER DELETE
    ON tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION func_tag_mappings_after_delete_sibling();
