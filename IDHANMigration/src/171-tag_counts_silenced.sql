CREATE OR REPLACE FUNCTION add_count(
    tag_id_i INTEGER,
    ideal_tag_id_i INTEGER,
    tag_domain_id_i SMALLINT,
    silenced BOOLEAN DEFAULT FALSE
) RETURNS VOID AS
$$
BEGIN
    LOCK TABLE tag_counts IN EXCLUSIVE MODE;

    IF tag_id_i IS NOT NULL THEN
        INSERT INTO tag_counts (tag_id, tag_domain_id, storage_count)
        VALUES (tag_id_i, tag_domain_id_i, 1)
        ON CONFLICT (tag_id, tag_domain_id)
            DO UPDATE SET storage_count = tag_counts.storage_count + 1;
    END IF;

    IF NOT silenced THEN
        INSERT INTO tag_counts (tag_id, tag_domain_id, display_count)
        VALUES (COALESCE(ideal_tag_id_i, tag_id_i), tag_domain_id_i, 1)
        ON CONFLICT (tag_id, tag_domain_id)
            DO UPDATE SET display_count = tag_counts.display_count + 1;
    END IF;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION remove_count(
    tag_id_i INTEGER,
    ideal_tag_id_i INTEGER,
    tag_domain_id_i SMALLINT,
    silenced BOOLEAN DEFAULT FALSE
) RETURNS VOID AS
$$
BEGIN
    LOCK TABLE tag_counts IN EXCLUSIVE MODE;

    UPDATE tag_counts SET storage_count = storage_count - 1 WHERE tag_id = tag_id_i AND tag_domain_id = tag_domain_id_i;

    IF NOT silenced THEN
        UPDATE tag_counts
        SET display_count = display_count - 1
        WHERE tag_id = COALESCE(ideal_tag_id_i, tag_id_i)
          AND tag_domain_id = tag_domain_id_i;
    END IF;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION accumulate_tag_count_storage() RETURNS TRIGGER AS
$$
BEGIN
    CASE tg_op
        WHEN 'INSERT'
            THEN PERFORM add_count(new.tag_id, new.ideal_tag_id, new.tag_domain_id, COALESCE(new.silenced, FALSE));
        WHEN 'UPDATE'
            THEN PERFORM remove_count(old.tag_id, old.ideal_tag_id, old.tag_domain_id, COALESCE(old.silenced, FALSE));
                 PERFORM add_count(new.tag_id, new.ideal_tag_id, new.tag_domain_id, COALESCE(new.silenced, FALSE));
        WHEN 'DELETE'
            THEN PERFORM remove_count(old.tag_id, old.ideal_tag_id, old.tag_domain_id, COALESCE(old.silenced, FALSE));
        END CASE;
    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION update_tag_counts(target_tag_id INTEGER) RETURNS VOID AS
$$
BEGIN
    LOCK TABLE tag_counts IN EXCLUSIVE MODE;

    INSERT INTO total_tag_counts (tag_id, storage_count, display_count)
    SELECT target_tag_id,
           COUNT(DISTINCT record_id) FILTER (WHERE atm.tag_id = target_tag_id)                        AS storage_count,
           COUNT(DISTINCT record_id)
           FILTER (WHERE NOT atm.silenced AND COALESCE(atm.ideal_tag_id, atm.tag_id) = target_tag_id) AS display_count
    FROM active_tag_mappings atm
    WHERE atm.tag_id = target_tag_id
       OR (NOT atm.silenced AND COALESCE(atm.ideal_tag_id, atm.tag_id) = target_tag_id)
    ON CONFLICT (tag_id) DO UPDATE
        SET storage_count = excluded.storage_count,
            display_count = excluded.display_count;

    INSERT INTO tag_counts (tag_id, tag_domain_id, storage_count, display_count)
    SELECT target_tag_id                                                                              AS tag_id,
           atm.tag_domain_id                                                                          AS tag_domain_id,
           COUNT(DISTINCT record_id) FILTER (WHERE atm.tag_id = target_tag_id)                        AS storage_count,
           COUNT(DISTINCT record_id)
           FILTER (WHERE NOT atm.silenced AND COALESCE(atm.ideal_tag_id, atm.tag_id) = target_tag_id) AS display_count
    FROM active_tag_mappings atm
    WHERE (atm.tag_id = target_tag_id OR (NOT atm.silenced AND COALESCE(atm.ideal_tag_id, atm.tag_id) = target_tag_id))
    GROUP BY atm.tag_domain_id
    ON CONFLICT (tag_id, tag_domain_id) DO UPDATE
        SET storage_count = excluded.storage_count,
            display_count = excluded.display_count;
END;
$$ LANGUAGE plpgsql;
