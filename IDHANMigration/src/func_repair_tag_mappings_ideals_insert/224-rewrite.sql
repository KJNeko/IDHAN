-- effective_tag_id is a virtual generated column, and postgres leaves those NULL in trigger records.
CREATE OR REPLACE FUNCTION repair_tag_mappings_ideals_insert()
    RETURNS TRIGGER AS
$$
BEGIN
    UPDATE active_tag_mappings
    SET ideal_tag_id = COALESCE(new.ideal_alias_id, new.alias_id)
    WHERE tag_id = new.aliased_id
      AND tag_domain_id = new.tag_domain_id;

    RETURN new;
END;
$$ LANGUAGE plpgsql;
