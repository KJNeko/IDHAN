//
// Created by kj16609 on 7/20/26.
//

#include "SearchFixture.hpp"

RecordID SearchFixture::createSearchableRecord(
	const std::string_view data,
	const std::int64_t size,
	const std::string_view mime_name,
	const std::int64_t time_offset_seconds )
{
	pqxx::work tx { *conn };

	const auto record_result { tx.exec_params(
		"INSERT INTO records (sha256, creation_time) VALUES (digest($1, 'sha256'), NOW() + ($2 * INTERVAL '1 second')) RETURNING record_id",
		pqxx::params { data, time_offset_seconds } ) };

	if ( record_result.empty() ) throw std::runtime_error( "Failed to create record" );

	const auto record_id { record_result[ 0 ][ 0 ].as< RecordID >() };

	tx.exec_params(
		"INSERT INTO file_info (size, record_id, mime_id, cluster_store_time, modified_time, extension) "
		"VALUES ($1, $2, (SELECT mime_id FROM mime WHERE name = $3), NOW() + ($4 * INTERVAL '1 second'), "
		"NOW() + ($4 * INTERVAL '1 second'), 'test')",
		pqxx::params { size, record_id, mime_name, time_offset_seconds } );

	tx.commit();

	return record_id;
}

std::vector< RecordID > SearchFixture::runQuery( const std::string& sql )
{
	pqxx::work tx { *conn };

	const auto result { tx.exec( sql ) };

	std::vector< RecordID > record_ids {};
	record_ids.reserve( static_cast< std::size_t >( result.size() ) );
	for ( const auto& row : result ) record_ids.push_back( row[ 0 ].as< RecordID >() );

	tx.commit();

	return record_ids;
}

std::vector< RecordID > SearchFixture::sortedIds( const idhan::SortType type, const idhan::SortOrder order )
{
	idhan::SearchBuilder builder {};
	builder.setSortType( type );
	builder.setSortOrder( order );
	return runQuery( builder.construct( true, false, false ) );
}
