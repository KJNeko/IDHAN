//
// Created by kj16609 on 3/20/25.
//

#include "FileInfo.hpp"

#include "filesystem/IOUring.hpp"
#include "logging/log.hpp"
#include "metadata/FileMappedData.hpp"
#include "mime/MimeDatabase.hpp"

namespace idhan
{

drogon::Task<> setFileInfo( const RecordID record_id, const FileInfo info, const DbClientPtr db )
{
	const trantor::Date store_date {
		std::chrono::duration_cast< std::chrono::microseconds >( info.store_time.time_since_epoch() ).count()
	};

	const trantor::Date file_modified_date {
		std::chrono::duration_cast< std::chrono::microseconds >( info.modified_time.time_since_epoch() ).count()
	};

	std::optional< MimeID > mime_opt {
		info.mime_id != constants::INVALID_MIME_ID ? std::optional< MimeID >( info.mime_id ) : std::nullopt
	};
	std::optional< std::string > extension_opt {
		info.mime_id != constants::INVALID_MIME_ID ? std::optional< std::string >( info.extension ) : std::nullopt
	};

	// the extension is used so we can still find the file even with an invalid mime
	co_await db->execSqlCoro(
		"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time, modified_time, extension) VALUES ($1, $2, $3, $4, $5, $6)"
		"ON CONFLICT (record_id) DO UPDATE SET mime_id = $3, extension = $5",
		record_id,
		info.size,
		mime_opt,
		store_date,
		file_modified_date,
		extension_opt );
}

drogon::Task< std::expected< FileInfo, drogon::HttpResponsePtr > > gatherFileInfo(
	FileIOUring io_uring,
	const DbClientPtr db )
{
	FileInfo info {};
	info.size = io_uring.size();
	const auto mime_string { co_await mime::getMimeDatabase()->scan( io_uring ) };

	if ( !mime_string )
	{
		co_return std::unexpected( mime_string.error() );
	}

	// Get MIME ID from database
	const auto mime_search {
		co_await db->execSqlCoro( "SELECT mime_id FROM mime WHERE name = $1", mime_string.value() )
	};

	if ( mime_search.empty() )
	{
		info.mime_id = constants::INVALID_MIME_ID;
		info.extension = io_uring.path().extension();
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
