//
// Created by kj16609 on 3/20/25.
//

#include "FileInfo.hpp"

#include "logging/log.hpp"
#include "metadata/FileMappedData.hpp"
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

drogon::Task< FileInfo > gatherFileInfo( std::shared_ptr< FileMappedData > data, drogon::orm::DbClientPtr db )
{
	FileInfo info {};
	info.size = data->length();
	const auto mime_string { mime::getInstance()->scan( data->data(), data->length() ) };

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
		info.mime_id = constants::INVALID_MIME_ID;
		log::warn( "Found file where mime is not registered in IDHAN: Mime: {}", mime_string.value() );
	}
	else
		info.mime_id = mime_search[ 0 ][ 0 ].as< MimeID >();

	info.store_time = std::chrono::system_clock::now();

	co_return info;
}

} // namespace idhan
