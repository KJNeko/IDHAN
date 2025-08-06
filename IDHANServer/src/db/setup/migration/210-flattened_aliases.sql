CREATE TABLE flattened_aliases
(
    aliased_id        INTEGER REFERENCES tags (tag_id)                NOT NULL,
    original_alias_id INTEGER REFERENCES tags (tag_id)                NOT NULL,
    alias_id INTEGER REFERENCES tags (tag_id) NULL,
    domain_id         SMALLINT REFERENCES tag_domains (tag_domain_id) NOT NULL,
    chain             INTEGER[]                                       NOT NULL,
    UNIQUE (aliased_id, alias_id, domain_id),
    UNIQUE (aliased_id, domain_id)
);

-- Function for handling inserts
CREATE OR REPLACE FUNCTION update_flattened_aliases_insert()
    RETURNS TRIGGER
    LANGUAGE plpgsql AS
$$
BEGIN
    -- If there is a record that exists
    IF (new.aliased_id = ANY (SELECT alias_id
                              FROM flattened_aliases
                              WHERE aliased_id = new.alias_id
                                AND domain_id = new.domain_id)) THEN
        RAISE EXCEPTION 'Recursive alias inserted for aliased (%) "%" -> alias (%) "%" in domain %. chain %',
            new.aliased_id, tag_text(new.aliased_id),
            new.alias_id, tag_text(new.alias_id),
            new.domain_id,
            (SELECT chain::text
             FROM flattened_aliases
             WHERE aliased_id = new.alias_id
               AND domain_id = new.domain_id
             LIMIT 1);
    END IF;

    RAISE DEBUG 'Inserted alias mapping: aliased_id=%, original_alias_id=%, domain_id=%',
        new.aliased_id, new.alias_id, new.domain_id;

    INSERT INTO flattened_aliases (aliased_id, alias_id, original_alias_id, domain_id, chain)
    VALUES (new.aliased_id, new.alias_id, new.alias_id, new.domain_id, ARRAY [new.alias_id]);

    RAISE DEBUG 'Fixing aliases for % to point to %', new.aliased_id, new.alias_id;
    -- Update any existing records ( if `toujou koneko` points to `koneko toujou`, then an insertion of `koneko toujou` pointing `character:toujou koneko` should make `toujou koneko` also point to it)
    UPDATE flattened_aliases
    SET alias_id = new.alias_id,
        chain    = ARRAY_APPEND(chain, new.alias_id)
    WHERE alias_id = new.aliased_id
      AND domain_id = new.domain_id;

    -- Just inserted `toujou_koneko` -> `toujou koneko`
    -- Fix any chains, `toujou_koneko` -> `toujou koneko` -> `character:toujou koneko`
    -- fa1.chain = {toujou_koneko, toujou koneko}
    -- fa2.chian = {toujou koneko, character:toujou koneko}
    RAISE DEBUG 'Repairing chains for %', new.aliased_id;
    UPDATE flattened_aliases fa1
    SET alias_id = fa2.alias_id,
        chain    = ARRAY_CAT(fa1.chain, fa2.chain)
    FROM flattened_aliases fa2
    WHERE fa1.domain_id = fa2.domain_id
      AND fa1.alias_id = fa2.aliased_id
      AND fa2.aliased_id = new.alias_id
      AND fa2.domain_id = new.domain_id;

    RETURN new;
END;
$$;

ALTER TABLE flattened_aliases
    ADD CONSTRAINT chain_size_limit CHECK (ARRAY_LENGTH(chain, 1) <= 128);

ALTER TABLE flattened_aliases
    ADD CHECK ( aliased_id != alias_id );

ALTER TABLE flattened_aliases
    ADD CHECK ( aliased_id != original_alias_id );

-- Function for handling deletes
CREATE FUNCTION update_flattened_aliases_delete()
    RETURNS TRIGGER AS
$$
BEGIN
    DELETE
    FROM flattened_aliases
    WHERE aliased_id = old.aliased_id
      AND original_alias_id = old.alias_id
      AND domain_id = old.domain_id;

    -- Log the deleted records
    RAISE DEBUG 'Deleted alias mapping: aliased_id=%, original_alias_id=%, domain_id=%',
        old.aliased_id, old.alias_id, old.domain_id;

    UPDATE flattened_aliases
    SET alias_id = chain[array_position(chain, old.alias_id) - 1],
        chain    = chain[1:array_position(chain, old.alias_id) - 1]
    WHERE old.alias_id = ANY (chain);

    RETURN old;
END;
$$ LANGUAGE plpgsql;

-- Create triggers
CREATE TRIGGER update_flattened_aliases_on_insert
    AFTER INSERT
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION update_flattened_aliases_insert();

CREATE TRIGGER update_flattened_aliases_on_delete
    AFTER DELETE
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION update_flattened_aliases_delete();

-- Add unique constraint to tag_aliases to prevent duplicate aliased_id within same domain
ALTER TABLE tag_aliases
    ADD CONSTRAINT unique_aliased_domain UNIQUE (aliased_id, domain_id);

ALTER TABLE tag_aliases
    ADD CHECK (aliased_id != alias_id);