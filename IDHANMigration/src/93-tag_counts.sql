CREATE TABLE tag_counts
(
    tag_id        INTEGER REFERENCES tags (tag_id),
    storage_count INTEGER NOT NULL DEFAULT 0,
    display_count INTEGER NOT NULL DEFAULT 0,
    tag_domain_id SMALLINT REFERENCES tag_domains (tag_domain_id),
    PRIMARY KEY (tag_id, tag_domain_id)
);

CREATE TABLE total_tag_counts
(
    tag_id        INTEGER REFERENCES tags (tag_id),
    storage_count INTEGER NOT NULL DEFAULT 0,
    display_count INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (tag_id)
);

CREATE OR REPLACE FUNCTION update_tag_counts(target_tag_id INTEGER) RETURNS VOID AS
$$
BEGIN
    -- Lock the tag_counts table to prevent concurrent updates
    LOCK TABLE tag_counts IN EXCLUSIVE MODE;

    -- Update/Insert counts using a single UPSERT operation
    INSERT INTO total_tag_counts (tag_id, storage_count, display_count)
    SELECT target_tag_id,
           COUNT(DISTINCT record_id) FILTER (WHERE atm.tag_id = target_tag_id)   AS storage_count,
           COUNT(DISTINCT record_id)
           FILTER (WHERE COALESCE(atm.ideal_tag_id, atm.tag_id) = target_tag_id) AS display_count
    FROM active_tag_mappings atm
    WHERE atm.tag_id = target_tag_id
       OR COALESCE(atm.ideal_tag_id, atm.tag_id) = target_tag_id
    ON CONFLICT (tag_id) DO UPDATE
        SET storage_count = excluded.storage_count,
            display_count = excluded.display_count;

    -- Update/Insert counts per domain using a single UPSERT operation
    INSERT INTO tag_counts (tag_id, tag_domain_id, storage_count, display_count)
    SELECT target_tag_id                                                         AS tag_id,
           atm.tag_domain_id                                                     AS tag_domain_id,
           COUNT(DISTINCT record_id) FILTER (WHERE atm.tag_id = target_tag_id)   AS storage_count,
           COUNT(DISTINCT record_id)
           FILTER (WHERE COALESCE(atm.ideal_tag_id, atm.tag_id) = target_tag_id) AS display_count
    FROM active_tag_mappings atm
    WHERE (atm.tag_id = target_tag_id OR COALESCE(atm.ideal_tag_id, atm.tag_id) = target_tag_id)
    GROUP BY atm.tag_domain_id
    ON CONFLICT (tag_id, tag_domain_id) DO UPDATE
        SET storage_count = excluded.storage_count,
            display_count = excluded.display_count;
END;
$$ LANGUAGE plpgsql;

CREATE FUNCTION add_count(tag_id_i INTEGER, ideal_tag_id_i INTEGER, tag_domain_id_i SMALLINT) RETURNS VOID AS
$$
BEGIN

    LOCK TABLE tag_counts IN EXCLUSIVE MODE;

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

CREATE FUNCTION remove_count(tag_id_i INTEGER, ideal_tag_id_i INTEGER, tag_domain_id_i SMALLINT) RETURNS VOID AS
$$
BEGIN

    LOCK TABLE tag_counts IN EXCLUSIVE MODE;

    UPDATE tag_counts SET storage_count = storage_count - 1 WHERE tag_id = tag_id_i AND tag_domain_id = tag_domain_id_i;
    UPDATE tag_counts
    SET display_count = display_count - 1
    WHERE tag_id = COALESCE(ideal_tag_id_i, tag_id_i)
      AND tag_domain_id = tag_domain_id_i;
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


