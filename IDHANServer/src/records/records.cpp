#include "records.hpp"

#include "../api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"
#include "db/drogonArrayBind.hpp"
#include "fgl/defines.hpp"

namespace idhan::helpers
{

drogon::Task< std::vector< RecordID > > massCreateRecord( const std::vector< SHA256 >& sha256s, DbClientPtr db )
{
	if ( sha256s.empty() ) co_return {};

	auto copy { sha256s };
	auto copy2 { sha256s };

	co_await db->execSqlCoro< std::vector< idhan::SHA256 > >(
		"INSERT INTO records (sha256) VALUES (UNNEST($1::BYTEA[])) ON CONFLICT DO NOTHING", std::move( copy ) );

	std::vector< RecordID > record_ids {};
	record_ids.reserve( sha256s.size() );

	const auto result { co_await db->execSqlCoro< std::vector< SHA256 > >(
		"SELECT record_id, sha256 FROM records WHERE sha256 = ANY($1::BYTEA[])", std::move( copy2 ) ) };

	std::unordered_map< SHA256, RecordID > record_id_map {};

	for ( const auto& row : result )
	{
		const auto sha256 { SHA256::fromPgCol( row[ 1 ] ) };
		const auto record_id { row[ 0 ].as< RecordID >() };

		record_id_map[ sha256 ] = record_id;
	}

	for ( const auto& in_sha256 : sha256s )
	{
		const auto record_id { record_id_map[ in_sha256 ] };
		record_ids.emplace_back( record_id );
	}

	if ( record_ids.size() != sha256s.size() ) co_return {};

	co_return record_ids;
}

drogon::Task< std::expected< RecordID, drogon::HttpResponsePtr > > createRecord( const SHA256& sha256, DbClientPtr db )
{
	const auto result { co_await findRecord( sha256, db ) };

	if ( result ) [[likely]]
		co_return result.value();

	std::size_t tries { 0 };

	do
	{
		tries += 1;
		if ( tries > 16 )
			co_return std::unexpected( createInternalError( "Failed to insert record {}", sha256.hex() ) );

		const auto insert { co_await db->execSqlCoro(
			"INSERT INTO records (sha256) VALUES ($1) ON CONFLICT DO NOTHING RETURNING record_id", sha256.toVec() ) };

		if ( insert.empty() ) continue;

		co_return insert[ 0 ][ 0 ].as< RecordID >();
	}
	while ( tries < 16 );

	co_return std::unexpected( createBadRequest( "Failed to create record" ) );
}

drogon::Task< std::optional< RecordID > > findRecord( const SHA256& sha256, DbClientPtr db )
{
	const auto search_result {
		co_await db->execSqlCoro( "SELECT record_id FROM records WHERE sha256 = $1", sha256.toVec() )
	};

	if ( search_result.empty() ) [[unlikely]]
		co_return std::nullopt;

	co_return search_result[ 0 ][ 0 ].as< RecordID >();
}

} // namespace idhan::helpers
