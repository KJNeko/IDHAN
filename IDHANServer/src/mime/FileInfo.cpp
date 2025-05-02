//
// Created by kj16609 on 3/20/25.
//

#include "FileInfo.hpp"

#include "logging/log.hpp"
#include "mime/MimeDatabase.hpp"

namespace idhan
{

drogon::Task< void > setFileInfo( const RecordID record_id, const FileInfo& info, drogon::orm::DbClientPtr db )
{
	const trantor::Date date {
		std::chrono::duration_cast< std::chrono::microseconds >( info.store_time.time_since_epoch() ).count()
	};

	co_await db->execSqlCoro(
		"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time, extension) VALUES ($1, $2, $3, $4, $5) ON CONFLICT (record_id) DO UPDATE SET size = $2, mime_id = $3, cluster_store_time = $4",
		record_id,
		info.size,
		info.mime_id,
		date,
		info.extension );
}

drogon::Task< FileInfo > gatherFileInfo( const std::byte* data, const std::size_t size, drogon::orm::DbClientPtr db )
{
	FileInfo info {};
	info.size = size;
	const auto mime_string { mime::getInstance()->scan( data, size ) };

	if ( !mime_string.has_value() )
	{
		throw mime_string.error();
	}

	// Get MIME ID from database
	const auto mime_search {
		co_await db->execSqlCoro( "SELECT mime_id FROM mime WHERE name = $1", mime_string.value() )
	};

	if ( mime_search.empty() )
	{
		info.mime_id = 0;
		log::warn( "Found file where mime is not registered in IDHAN: Mime: {}", mime_string.value() );
	}
	else
		info.mime_id = mime_search[ 0 ][ 0 ].as< MimeID >();

	info.store_time = std::chrono::system_clock::now();

	co_return info;
}

} // namespace idhan
