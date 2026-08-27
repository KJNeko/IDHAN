CREATE FUNCTION func_clear_missing_file_on_save()
    RETURNS TRIGGER AS
$$
BEGIN
    DELETE FROM missing_files WHERE record_id = new.record_id;
    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_file_info_clear_missing_file_on_save
    AFTER UPDATE OF cluster_id, cluster_store_time
    ON file_info
    FOR EACH ROW
    WHEN (new.cluster_id IS NOT NULL)
EXECUTE FUNCTION func_clear_missing_file_on_save();
