

#include "records.hpp"

#include "crypto/sha256.hpp"
#include "fgl/defines.hpp"

namespace idhan::api
{

drogon::Task< RecordID > createRecord( const SHA256& sha256, drogon::orm::DbClientPtr db )
{
	// Here we do the ON CONFLICT DO NOTHING in order to prevent an exception from being thrown by drogon.
	const auto result {
		co_await db->execSqlCoro( "INSERT INTO records (sha256) VALUES ($1) ON CONFLICT DO NOTHING", sha256.toVec() )
	};

	RecordID record_id FGL_UNINITALIZED;

	// If the DO NOTHING clause is triggered, we'll have no result.
	if ( result.empty() )
	{
		const auto search_result { co_await searchRecord( sha256, db ) };

		//TODO: Proper exception
		if ( !search_result.has_value() ) throw std::runtime_error( "Failed to get record_id after inserting it" );

		co_return search_result.value();
	}
	else
	{
		co_return result[ 0 ][ 0 ].as< RecordID >();
	}
}

drogon::Task< std::optional< RecordID > > searchRecord( const SHA256& sha256, drogon::orm::DbClientPtr db )
{
	const auto search_result {
		co_await db->execSqlCoro( "SELECT record_id FROM records WHERE sha256 = $1", sha256.toVec() )
	};

	if ( search_result.empty() ) co_return std::nullopt;

	co_return search_result[ 0 ][ 0 ].as< RecordID >();
}

} // namespace idhan::api
