//
// Created by kj16609 on 9/8/24.
//

#include <pqxx/nontransaction>

#include <array>
#include <string_view>
#include <tuple>

#include "logging/log.hpp"
#include "management.hpp"

namespace idhan::db
{

	// Inital tables
	// clang-format off
	constexpr std::array< std::tuple< std::string_view, std::string_view >, 20 > table_creation_sql {
		{
			{
				"idhan_info",
				R"(
					CREATE TABLE idhan_info (
						table_version	INTEGER	NOT NULL,
						table_name		TEXT	UNIQUE NOT NULL,
						creation_query	TEXT	NOT NULL
					)
				)"
			},
			{
			"records",
				R"(
					CREATE TABLE records (
						record_id		SERIAL		PRIMARY KEY,
						sha256			BYTEA		UNIQUE NOT NULL,
						creation_time	TIMESTAMP	WITHOUT TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP
					)
				)"
			},
			{
				"file_domains",
				R"(
					CREATE TABLE file_domains (

				)"
			},
			/*======================== TAGS =======================================*/
			{
				"tag_namespaces",
				R"(
					CREATE TABLE tag_namespaces (
						namespace_id	SERIAL	PRIMARY KEY,
						namespace_text	TEXT	UNIQUE NOT NULL
					)
				)"
			},
			{
				"tag_subtags",
				R"(
					CREATE TABLE tag_subtags (
						subtag_id	SERIAL	PRIMARY KEY,
						subtag_text TEXT	UNIQUE NOT NULL
					)
				)"
			},
			{
				"tag_domains",
				R"(
					CREATE TABLE tag_domains (
						tag_domain_id	SMALLSERIAL		PRIMARY KEY,
						domain_name		TEXT			UNIQUE NOT NULL
					)
				)"
			},
			{
				"tags",
				R"(
					CREATE TABLE tags (
						tag_id			BIGSERIAL	PRIMARY KEY,
						namespace_id	INTEGER		REFERENCES tag_namespaces(namespace_id),
						subtag_id		INTEGER		REFERENCES tag_subtags(subtag_id),
						UNIQUE(namespace_id, subtag_id)
					)
				)"
			},
			{
				"tag_aliases",
				R"(
					CREATE TABLE tag_aliases (
						alias_id	INTEGER		REFERENCES tags(tag_id),
						aliased_id	INTEGER		REFERENCES tags(tag_id) UNIQUE,
						domain_id	SMALLINT	REFERENCES tag_domains(tag_domain_id)
					)
				)"
			},
			{
				"tag_parents",
				R"(
					CREATE TABLE tag_parents (
						parent_id	INTEGER		REFERENCES tags(tag_id),
						child_id	INTEGER		REFERENCES tags(tag_id),
						domain_id	SMALLINT	REFERENCES tag_domains(tag_domain_id),
						UNIQUE(domain_id, parent_id, child_id)
					)
				)"
			},
			{
				"tag_siblings",
				R"(
					CREATE TABLE tag_siblings (
						older_id	INTEGER		REFERENCES tags(tag_id),
						younger_id	INTEGER		REFERENCES tags(tag_id),
						domain_id	SMALLINT	REFERENCES tag_domains(tag_domain_id),
						UNIQUE(domain_id, older_id, younger_id)
					)
				)"
			},
			{
				"tag_mappings",
				R"(
					CREATE TABLE tag_mappings (
						record_id	INTEGER		REFERENCES records(record_id),
						tag_id		INTEGER		REFERENCES tags(tag_id),
						domain_id	SMALLINT	REFERENCES tag_domains(tag_domain_id),
						UNIQUE(domain_id, record_id, tag_id)
					)
				)"
			},
			/*========================== URLS =====================================*/
			{
				"url_domains",
				R"(
					CREATE TABLE url_domains (
						url_domain_id	SERIAL	PRIMARY KEY,
						url_domain		TEXT	UNIQUE NOT NULL
					)
				)"
			},
			{
				"urls",
				R"(
					CREATE TABLE urls (
						url_id			SERIAL	PRIMARY KEY,
						url_domain_id	INTEGER NOT NULL REFERENCES url_domains(url_domain_id),
						url				TEXT	UNIQUE NOT NULL
					)
				)"
			},
			{
				"url_map",
				R"(
					CREATE TABLE url_map (
						record_id	INTEGER	REFERENCES records(record_id),
						url_id			INTEGER REFERENCES urls(url_id)
					)
				)"
			},
			/*========================== CLUSTERS ==================================*/
			{
				"file_clusters",
				R"(
					CREATE TABLE file_clusters (
						cluster_id		SMALLSERIAL		PRIMARY KEY,
						cluster_path	TEXT			UNIQUE NOT NULL
					)
				)"
			},
			{
				"file_meta",
				R"(
					CREATE TABLE cluster_info (
						cluster_id		SMALLINT	REFERENCES file_clusters(cluster_id),
						record_id		INTEGER		REFERENCES records(record_id),
						file_size		BIGINT,
						obtained		TIMESTAMP	WITHOUT TIME ZONE
					)
				)"
			},
			/*========================== META =====================================*/
			{
				"deleted_files",
				R"(
					CREATE TABLE deleted_files (
						record_id		INTEGER		REFERENCES records(record_id),
						deleted_time	TIMESTAMP	WITHOUT TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP
					)
				)"
			},
			{
				"file_notes",
				R"(
					CREATE TABLE file_notes (
						record_id	SERIAL	REFERENCES records(record_id),
						note			TEXT	NOT NULL
					)
				)"
			},
			/*========================== MIME ======================================*/
			{
				"mime",
				R"(
					CREATE TABLE mime (
						mime_id			SERIAL	PRIMARY KEY,
						http_mime		TEXT	UNIQUE NOT NULL,
						best_extension	TEXT	NOT NULL
					)
				)"
			},
			{
				"record_mime",
				R"(
					CREATE TABLE record_mime (
						record_id		INTEGER	REFERENCES records(record_id),
						mime_id			INTEGER	REFERENCES mime(mime_id),
						UNIQUE(record_id)
					)
				)"
			}
		}
	};
	// clang-format on

	/*
	constexpr std::array< std::string_view, 2 > table_creation_sql
	{
		// Tag creation/Selection
		R"()",
		R"()"
	};
	*/

} // namespace idhan::db
