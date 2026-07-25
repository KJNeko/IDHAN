CREATE TABLE idhan_info
(
    object_name       TEXT PRIMARY KEY NOT NULL, -- migration folder / DB object, e.g. table_file_info
    last_migration_id INTEGER          NOT NULL, -- highest applied migration number for this object
    last_operation    TEXT             NOT NULL, -- operation of the latest applied file, e.g. create/alter/index
    queries           TEXT[]           NOT NULL  -- every query applied to this object, in order
);
