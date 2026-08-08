//
// Created by kj16609 on 7/20/26.
//

#include "SearchFixture.hpp"

MimeID SearchFixture::getMimeId( const std::string_view mime_name )
{
	pqxx::work tx { *conn };

	const auto result { tx.exec_params( "SELECT mime_id FROM mime WHERE name = $1", pqxx::params { mime_name } ) };

	if ( result.empty() ) throw std::runtime_error( "Unknown mime name in test fixture" );

	return result[ 0 ][ 0 ].as< MimeID >();
}

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

void SearchFixture::insertVideoMetadata(
	const RecordID record_id,
	const double duration,
	const double framerate,
	const int width,
	const int height,
	const int bitrate,
	const bool has_audio )
{
	pqxx::work tx { *conn };

	tx.exec_params(
		"INSERT INTO video_metadata (record_id, duration, framerate, width, height, bitrate, has_audio) "
		"VALUES ($1, $2, $3, $4, $5, $6, $7)",
		pqxx::params { record_id, duration, framerate, width, height, bitrate, has_audio } );

	tx.commit();
}

void SearchFixture::insertImageMetadata( const RecordID record_id, const int width, const int height, const int channels )
{
	pqxx::work tx { *conn };

	tx.exec_params(
		"INSERT INTO image_metadata (record_id, width, height, channels) VALUES ($1, $2, $3, $4)",
		pqxx::params { record_id, width, height, channels } );

	tx.commit();
}

void SearchFixture::insertImageProjectMetadata(
	const RecordID record_id,
	const int width,
	const int height,
	const int channels,
	const int layers )
{
	pqxx::work tx { *conn };

	tx.exec_params(
		"INSERT INTO image_project_metadata (record_id, width, height, channels, layers) VALUES ($1, $2, $3, $4, $5)",
		pqxx::params { record_id, width, height, channels, layers } );

	tx.commit();
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
	return runQuery( builder.browseQuery() );
}
