CREATE SEQUENCE tag_domains_tag_domain_id_seq;

CREATE TABLE tag_domains
(
    tag_domain_id tagdomainid PRIMARY KEY DEFAULT NEXTVAL('tag_domains_tag_domain_id_seq'),
    domain_name   TEXT UNIQUE NOT NULL
);