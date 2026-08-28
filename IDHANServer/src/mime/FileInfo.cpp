#include "FileInfo.hpp"

#include "filesystem/io/IOUring.hpp"
#include "logging/log.hpp"
#include "mime/identifyMime.hpp"

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
		"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time, modified_time, extension) VALUES ($1, $2, $3, $4, $5, $6) "
		"ON CONFLICT (record_id) DO UPDATE SET mime_id = EXCLUDED.mime_id, extension = EXCLUDED.extension",
		record_id,
		info.size,
		mime_opt,
		store_date,
		file_modified_date,
		extension_opt );
}

drogon::Task< FileInfo > gatherFileInfo( std::shared_ptr< FileIOUring > io_uring )
{
	FileInfo info {};
	info.size = io_uring->size();

	const auto mime_id { co_await mime::identifyMimeForPath( io_uring->path() ) };

	if ( mime_id == mime_ids::UNKNOWN )
	{
		info.mime_id = constants::INVALID_MIME_ID;
		info.extension = io_uring->path().extension();
		if ( info.extension.starts_with( '.' ) ) info.extension = info.extension.substr( 1 );
	}
	else
	{
		info.mime_id = mime_id;
	}

	info.store_time = std::chrono::system_clock::now();

	co_return info;
}

} // namespace idhan
