//
// Created by kj16609 on 7/24/24.
//

#include "Database.hpp"

#include "logging/log.hpp"

namespace idhan
{

	bool tableExists( pqxx::nontransaction& tx, const std::string_view name )
	{
		const pqxx::result table_result {
			tx.exec_params( "SELECT table_name FROM information_schema.tables WHERE table_name = $1", name )
		};

		return table_result.size() > 0;
	}

	//! Returns the table version.
	std::uint16_t getTableVersion( pqxx::nontransaction& tx, const std::string_view name )
	{
		auto result { tx.exec_params( "SELECT table_version FROM idhan_info WHERE table_name = $1", name ) };

		if ( result.size() == 0 ) return 0;

		return std::get< 0 >( result.at( 0 ).as< std::uint16_t >() );
	}

	void addTableToInfo( pqxx::nontransaction& tx, const std::string_view name, const std::string_view creation_query )
	{
		tx.exec_params(
			"INSERT INTO idhan_info ( table_version, table_name, creation_query ) VALUES ($1, $2, $3)",
			1,
			name,
			creation_query );
	}

	void updateTableVersion( pqxx::nontransaction& tx, const std::string_view name, const std::uint16_t version )
	{
		tx.exec_params( "UPDATE idhan_info SET table_version = $1 WHERE table_name = $2", version, name );
	}

	// clang-format off
	constexpr std::array< std::tuple< std::string_view, std::string_view >, 14 > table_creation_sql {
	{
		{
			"idhan_info",
			"CREATE TABLE idhan_info (table_version INTEGER NOT NULL, table_name TEXT UNIQUE NOT NULL, creation_query TEXT NOT NULL)"
		},
		{
			"file_records",
			"CREATE TABLE file_records (file_record_id BIGSERIAL PRIMARY KEY, sha256 BYTEA UNIQUE NOT NULL, creation_time TIMESTAMP WITHOUT TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP)" },
		{
			"tag_namespaces",
			"CREATE TABLE tag_namespaces (namespace_id SERIAL PRIMARY KEY, namespace_text TEXT UNIQUE NOT NULL)" },
		{
			"tag_subtags",
			"CREATE TABLE tag_subtags (subtag_id SERIAL PRIMARY KEY, subtag_text TEXT UNIQUE NOT NULL)"
		},
		{
			"tag_domains",
			"CREATE TABLE tag_domains (tag_domain_id SMALLSERIAL PRIMARY KEY, domain_name TEXT UNIQUE NOT NULL)"
		},
		{
			"tags",
			"CREATE TABLE tags (tag_domain_id SMALLINT NOT NULL REFERENCES tag_domains (tag_domain_id), tag_id BIGSERIAL PRIMARY KEY, namespace_id INTEGER REFERENCES tag_namespaces(namespace_id), subtag_id INTEGER REFERENCES tag_subtags(subtag_id), UNIQUE(namespace_id, subtag_id))"
		},
		{
			"url_domains",
			"CREATE TABLE url_domains (url_domain_id SERIAL PRIMARY KEY, url_domain TEXT UNIQUE NOT NULL)"
		},
		{
			"urls",
			"CREATE TABLE urls (url_id SERIAL PRIMARY KEY, url_domain_id INTEGER NOT NULL REFERENCES url_domains(url_domain_id), url TEXT UNIQUE NOT NULL)"
		},
		{
			"url_map",
			"CREATE TABLE url_map (file_record_id BIGINT REFERENCES file_records(file_record_id), url_id INTEGER REFERENCES urls(url_id))"
		},
		{
			"deleted_files",
			"CREATE TABLE deleted_files (file_record_id BIGINT REFERENCES file_records(file_record_id), deleted_time TIMESTAMP WITHOUT TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP)"
		},
		{
			"file_notes",
			"CREATE TABLE file_notes (file_record_id BIGINT REFERENCES file_records(file_record_id), note TEXT NOT NULL)"
		},
		{
			"tag_aliases",
			"CREATE TABLE tag_aliases (alias_id BIGINT REFERENCES tags(tag_id), aliased_id BIGINT REFERENCES tags(tag_id) UNIQUE)"
		},
		{
			"tag_parents",
			"CREATE TABLE tag_parents (parent_id BIGINT REFERENCES tags(tag_id), child_id BIGINT REFERENCES tags(tag_id), UNIQUE(parent_id, child_id))"
		},
		{
			"tag_siblings",
			"CREATE TABLE tag_siblings (older_id BIGINT REFERENCES tags(tag_id), younger_id BIGINT REFERENCES tags(tag_id), UNIQUE(older_id, younger_id))" }
		 }
	};
	// clang-format on

	void Database::initalSetup( pqxx::nontransaction& tx )
	{
		log::info( "Starting inital table setup" );

		for ( const auto& [ table_name, query ] : table_creation_sql )
		{
			log::info( "Running {}", query );
			tx.exec( query );
			addTableToInfo( tx, table_name, query );
		}

		tx.commit();
	}

	void Database::importHydrus( const ConnectionArguments& connection_arguments )
	{}

	Database::Database( const ConnectionArguments& arguments ) : connection( arguments.format() )
	{
		log::info( "Postgres connection made: {}", connection.dbname() );

		// Determine if we should do our inital setup (if the idhan_info table is missing then we should do our setup)
		{
			pqxx::nontransaction tx { connection };

			tx.exec(
				"DROP TABLE IF EXISTS idhan_info, url_map, urls, url_domains, deleted_files, file_notes, file_records, tag_aliases, tag_parents, tag_siblings, tags, tag_namespaces, tag_subtags, tag_domains cascade;" );

			if ( !tableExists( tx, "idhan_info" ) )
			{
				initalSetup( tx );
			}
		}

		log::info( "Database loading finished" );
	}

	std::string ConnectionArguments::format() const
	{
		std::string str;
		if ( hostname.empty() ) throw std::runtime_error( "Hostname empty" );

		if ( port == std::numeric_limits< std::uint16_t >::quiet_NaN() ) throw std::runtime_error( "Port not set" );

		str += std::format( "host={} ", hostname );
		str += std::format( "port={} ", port );
		str += std::format( "dbname={} ", dbname );
		str += std::format( "user={} ", user );

		return str;
	}
} // namespace idhan
