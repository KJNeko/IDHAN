CREATE TABLE file_clusters
(
	cluster_id         SMALLSERIAL PRIMARY KEY,
	cluster_path       TEXT UNIQUE NOT NULL,
	percentage_allowed SMALLINT    NOT NULL,
	readonly           BOOLEAN     NOT NULL
);