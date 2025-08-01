//
// Created by kj16609 on 3/20/25.
//

#include "FileInfo.hpp"

#include "logging/log.hpp"
#include "metadata/FileMappedData.hpp"
#include "mime/MimeDatabase.hpp"

namespace idhan
{

drogon::Task<> setFileInfo( const RecordID record_id, const FileInfo info, const drogon::orm::DbClientPtr db )
{
	const trantor::Date date {
		std::chrono::duration_cast< std::chrono::microseconds >( info.store_time.time_since_epoch() ).count()
	};

	if ( info.mime_id == constants::INVALID_MIME_ID ) // if the mime is invalid (unknown)
	{
		// the extension is used so we can still find the file even with an invalid mime
		co_await db->execSqlCoro(
			"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time, extension) VALUES ($1, $2, NULL, $3, $4) ON CONFLICT (record_id) DO UPDATE SET size = $2, mime_id = NULL, cluster_store_time = $3, extension = $4",
			record_id,
			info.size,
			date,
			info.extension );
	}
	else
	{
		co_await db->execSqlCoro(
			"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time, extension) VALUES ($1, $2, $3, $4, NULL) ON CONFLICT (record_id) DO UPDATE SET size = $2, mime_id = $3, cluster_store_time = $4, extension = NULL",
			record_id,
			info.size,
			info.mime_id,
			date );
	}
}

drogon::Task< FileInfo >
	gatherFileInfo( const std::shared_ptr< FileMappedData > data, const drogon::orm::DbClientPtr db )
{
	FileInfo info {};
	info.size = data->length();
	const auto mime_string { mime::getInstance()->scan( data->data(), data->length() ) };

	if ( !mime_string )
	{
		throw mime_string.error();
	}

	// Get MIME ID from database
	const auto mime_search {
		co_await db->execSqlCoro( "SELECT mime_id FROM mime WHERE name = $1", mime_string.value() )
	};

	if ( mime_search.empty() )
	{
		info.mime_id = constants::INVALID_MIME_ID;
		info.extension = data->extension();
		// ensure that it doesn't start with `.`
		if ( info.extension.starts_with( '.' ) ) info.extension = info.extension.substr( 1 );
	}
	else
	{
		info.mime_id = mime_search[ 0 ][ 0 ].as< MimeID >();
		// extension is not needed if the mime_id is present
	}

	info.store_time = std::chrono::system_clock::now();

	co_return info;
}

} // namespace idhan
