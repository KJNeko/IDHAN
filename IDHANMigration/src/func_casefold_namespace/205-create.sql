CREATE OR REPLACE FUNCTION casefold_namespace()
    RETURNS TRIGGER
AS
$$
BEGIN
    new.namespace_text = casefold(normalize(new.namespace_text, NFC));
    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER tag_namespaces_before_insert
    BEFORE INSERT
    ON tag_namespaces
    FOR EACH ROW
EXECUTE FUNCTION casefold_namespace();
