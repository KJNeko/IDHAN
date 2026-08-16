CREATE OR REPLACE FUNCTION casefold_subtag()
    RETURNS TRIGGER
AS
$$
BEGIN
    new.display_text = normalize(new.subtag_text, NFC);
    new.subtag_text = normalize(casefold(new.display_text), NFC);
    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER tag_subtags_before_insert
    BEFORE INSERT
    ON tag_subtags
    FOR EACH ROW
EXECUTE FUNCTION casefold_subtag();
