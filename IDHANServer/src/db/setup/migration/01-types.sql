CREATE DOMAIN tagid AS BIGINT;

CREATE DOMAIN subtagid AS BIGINT CONSTRAINT subtag_id_greater_than_zero CHECK (value > 0);

CREATE DOMAIN namespaceid AS INTEGER CONSTRAINT namespace_id_greater_than_zero CHECK (value > 0);

CREATE DOMAIN recordid AS INTEGER CONSTRAINT record_id_greater_than_zero CHECK (value > 0);

CREATE DOMAIN tagdomainid AS SMALLINT CONSTRAINT tag_domain_id_greater_than_zero CHECK (value > 0);