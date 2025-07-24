

#include "records.hpp"

#include "crypto/SHA256.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"

namespace idhan::api::helpers
{

drogon::Task< std::vector< RecordID > >
	massCreateRecord( const std::vector< SHA256 >& sha256s, drogon::orm::DbClientPtr db )
{
	FGL_ASSERT( sha256s.size() <= 100, "Too many hashes" );

	//TODO: Add in check for uniqueness

	std::string query {};
	query += "SELECT * FROM insertMultipleRecords(";

	for ( const auto& sha256 : sha256s )
	{
		query += format_ns::format( "\'\\x{}\'::bytea", sha256.hex() );
		query += ",";
	}

	// remove last `,`
	query.pop_back();

	query += ");";

	try
	{
		const auto result { co_await db->execSqlCoro( query ) };

		std::vector< RecordID > record_ids {};
		record_ids.reserve( sha256s.size() );

		for ( const auto& row : result )
		{
			record_ids.push_back( row[ 0 ].as< RecordID >() );
		}

		co_return record_ids;
	}
	catch ( std::exception& e )
	{
		log::error( "Error with {}", query );

		std::rethrow_exception( std::current_exception() );
	}
}

drogon::Task< RecordID > createRecord( const SHA256& sha256, drogon::orm::DbClientPtr db )
{
	// Here we do the ON CONFLICT DO NOTHING in order to prevent an exception from being thrown by drogon.
	const auto result { co_await db->execSqlCoro(
		"INSERT INTO records (sha256) VALUES ($1) ON CONFLICT (sha256) DO UPDATE SET sha256 = excluded.sha256 RETURNING record_id",
		sha256.toVec() ) };

	if ( result.empty() ) [[unlikely]]
	{
		const auto search_result { co_await findRecord( sha256, db ) };

		//TODO: Proper exception
		if ( !search_result.has_value() ) throw std::runtime_error( "Optional had no result" );

		co_return search_result.value();
	}

	co_return result[ 0 ][ 0 ].as< RecordID >();
}

drogon::Task< std::optional< RecordID > > findRecord( const SHA256& sha256, drogon::orm::DbClientPtr db )
{
	const auto search_result {
		co_await db->execSqlCoro( "SELECT record_id FROM records WHERE sha256 = $1", sha256.toVec() )
	};

	if ( search_result.empty() ) [[unlikely]]
		co_return std::nullopt;

	co_return search_result[ 0 ][ 0 ].as< RecordID >();
}

} // namespace idhan::api::helpers
