DO
$$
    DECLARE
        partition REGCLASS;
    BEGIN
        FOR partition IN SELECT relid FROM pg_partition_tree('active_tag_mappings_parents') WHERE isleaf
            LOOP
                PERFORM install_tag_count_collectors(partition, 'collect_tag_count_parents'::REGPROC);
            END LOOP;
    END;
$$;
