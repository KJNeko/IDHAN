CREATE OR REPLACE TRIGGER tag_subtags_before_insert
    BEFORE INSERT
    ON tags
    FOR EACH ROW
EXECUTE FUNCTION casefold_subtag();
