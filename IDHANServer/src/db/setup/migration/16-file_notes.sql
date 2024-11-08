CREATE TABLE file_notes
(
	record_id SERIAL REFERENCES records (record_id),
	note      TEXT NOT NULL
);