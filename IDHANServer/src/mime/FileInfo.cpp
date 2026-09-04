#include "FileInfo.hpp"

#include "filesystem/filesystem.hpp"
#include "filesystem/io/IOUring.hpp"
#include "logging/log.hpp"
#include "mime/identifyMime.hpp"

namespace idhan
{

drogon::Task<> setFileInfo( const RecordID record_id, const FileInfo info, const DbClientPtr db )
{
	const trantor::Date store_date { filesystem::toMicroseconds( info.store_time ) };
	const trantor::Date file_mtime_date { filesystem::toMicroseconds( info.file_mtime ) };
	const auto file_ctime_date { filesystem::toMicroseconds( info.file_ctime )
		                             .transform( []( const std::int64_t us ) { return trantor::Date { us }; } ) };

	std::optional< MimeID > mime_opt {
		info.mime_id != constants::INVALID_MIME_ID ? std::optional< MimeID >( info.mime_id ) : std::nullopt
	};
	std::optional< std::string > extension_opt {
		info.mime_id != constants::INVALID_MIME_ID ? std::optional< std::string >( info.extension ) : std::nullopt
	};

	// the extension is used so we can still find the file even with an invalid mime
	co_await db->execSqlCoro(
		"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time, file_mtime, file_ctime, extension) VALUES ($1, $2, $3, $4, $5, $6, $7) "
		"ON CONFLICT (record_id) DO UPDATE SET mime_id = EXCLUDED.mime_id, extension = EXCLUDED.extension",
		record_id,
		info.size,
		mime_opt,
		store_date,
		file_mtime_date,
		file_ctime_date,
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

	if ( const auto times { filesystem::getFileTimes( io_uring->path() ) } )
	{
		info.file_mtime = times->mtime;
		info.file_ctime = times->btime;
	}

	co_return info;
}

} // namespace idhan
