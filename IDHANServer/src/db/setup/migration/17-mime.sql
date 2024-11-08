CREATE TABLE mime
(
	mime_id        SERIAL PRIMARY KEY,
	http_mime      TEXT UNIQUE NOT NULL,
	best_extension TEXT        NOT NULL
);