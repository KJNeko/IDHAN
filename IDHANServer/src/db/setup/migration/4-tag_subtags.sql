CREATE TABLE tag_subtags
(
	subtag_id   SERIAL PRIMARY KEY,
	subtag_text TEXT UNIQUE NOT NULL
);