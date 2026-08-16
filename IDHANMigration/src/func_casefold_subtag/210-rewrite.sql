CREATE OR REPLACE FUNCTION casefold_subtag()
    RETURNS TRIGGER
AS
$$
BEGIN
    new.subtag_text = normalize(casefold(normalize(new.subtag_text, NFC)), NFC);
    RETURN new;
END;
$$ LANGUAGE plpgsql;
