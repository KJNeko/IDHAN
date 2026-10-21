CREATE TRIGGER trg_file_info_cluster_usage_insert
    AFTER INSERT
    ON file_info
    REFERENCING NEW TABLE AS new_rows
    FOR EACH STATEMENT
EXECUTE FUNCTION func_file_info_cluster_usage();

CREATE TRIGGER trg_file_info_cluster_usage_update
    AFTER UPDATE
    ON file_info
    REFERENCING OLD TABLE AS old_rows NEW TABLE AS new_rows
    FOR EACH STATEMENT
EXECUTE FUNCTION func_file_info_cluster_usage();

CREATE TRIGGER trg_file_info_cluster_usage_delete
    AFTER DELETE
    ON file_info
    REFERENCING OLD TABLE AS old_rows
    FOR EACH STATEMENT
EXECUTE FUNCTION func_file_info_cluster_usage();
