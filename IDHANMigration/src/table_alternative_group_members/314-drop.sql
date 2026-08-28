-- Group membership was transitive, so a record could end up an alternative of its own duplicate.
-- The pairs it held are not carried into alternative_records.
DROP TABLE alternative_group_members;
