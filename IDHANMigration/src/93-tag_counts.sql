CREATE TABLE tag_counts
(
    tag_id        BIGINT REFERENCES tags (tag_id),
    storage_count INTEGER NOT NULL DEFAULT 0,
    display_count INTEGER NOT NULL DEFAULT 0,
    tag_domain_id SMALLINT REFERENCES tag_domains (tag_domain_id),
    PRIMARY KEY (tag_id, tag_domain_id)
);

CREATE FUNCTION add_count(tag_id_i BIGINT, ideal_tag_id_i BIGINT, tag_domain_id_i SMALLINT) RETURNS VOID AS
$$
BEGIN

    IF tag_id_i IS NOT NULL THEN
        INSERT INTO tag_counts (tag_id, tag_domain_id, storage_count)
        VALUES (tag_id_i, tag_domain_id_i, 1)
        ON CONFLICT (tag_id, tag_domain_id)
            DO UPDATE SET storage_count = tag_counts.storage_count + 1;
    END IF;

    INSERT INTO tag_counts (tag_id, tag_domain_id, display_count)
    VALUES (COALESCE(ideal_tag_id_i, tag_id_i), tag_domain_id_i, 1)
    ON CONFLICT (tag_id, tag_domain_id)
        DO UPDATE SET display_count = tag_counts.display_count + 1;
END;
$$ LANGUAGE plpgsql;

CREATE FUNCTION remove_count(tag_id_i BIGINT, ideal_tag_id_i BIGINT, tag_domain_id_i SMALLINT) RETURNS VOID AS
$$
BEGIN
    UPDATE tag_counts SET storage_count = storage_count - 1 WHERE tag_id = tag_id_i AND tag_domain_id = tag_domain_id_i;
    UPDATE tag_counts SET display_count = display_count - 1 WHERE tag_id = COALESCE(ideal_tag_id_i, tag_id_i) AND tag_domain_id = tag_domain_id_i;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION accumulate_tag_count_storage() RETURNS TRIGGER AS
$$
BEGIN

    CASE tg_op
        WHEN 'INSERT' THEN EXECUTE add_count(new.tag_id, new.ideal_tag_id, new.tag_domain_id);

        WHEN 'UPDATE' THEN EXECUTE remove_count(old.tag_id, old.ideal_tag_id, old.tag_domain_id);
                           EXECUTE add_count(new.tag_id, new.ideal_tag_id, new.tag_domain_id);

        WHEN 'DELETE' THEN EXECUTE remove_count(old.tag_id, old.ideal_tag_id, old.tag_domain_id);

        END CASE;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER accumulate_tag_count_trigger
    AFTER INSERT OR UPDATE OR DELETE
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE PROCEDURE accumulate_tag_count_storage();

SELECT add_count(tag_id, ideal_tag_id, tag_domain_id)
FROM active_tag_mappings;

SELECT add_count(NULL, tag_id, tag_domain_id)
FROM active_tag_mappings_parents;


